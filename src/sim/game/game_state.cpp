#include "sim/game/game_state.hpp"

#include <algorithm>

namespace sim {

using core::Fixed;
using core::Vec2F;

namespace {

int64_t dist_sq_raw(Vec2F a, Vec2F b) {
    const int64_t dx = a.x.raw - b.x.raw;
    const int64_t dy = a.y.raw - b.y.raw;
    return dx * dx + dy * dy;
}

}  // namespace

GameState::GameState(const data::DataRegistry& data, const GameConfig& config)
    : data_(data),
      config_(config),
      rng_(config.seed),
      map_(config.map_width, config.map_height),
      spatial_(config.map_width, config.map_height, 4) {
    config_.num_players = std::clamp(config_.num_players, 1, static_cast<int>(data_.start().players.size()));
    setup_players();
    generate_map();
    apply_pending();
}

// ─── Tick ────────────────────────────────────────────────────────────────────

void GameState::step(std::span<const Command> commands) {
    for (const Command& cmd : commands) apply_command(cmd);
    production_system();
    action_system();
    apply_pending();
    ++tick_;
}

// ─── Consultas ───────────────────────────────────────────────────────────────

bool GameState::can_afford(data::PlayerId p, const data::ResourceArray& cost) const {
    const auto& res = players_.at(p).resources;
    for (std::size_t i = 0; i < data::kResourceCount; ++i) {
        if (res[i] < cost[i]) return false;
    }
    return true;
}

bool GameState::can_place(data::TypeId building, core::TilePos tile) const {
    const auto& s = data_.sheet(building);
    return s.is_building() && map_.is_area_free(tile.x, tile.y, s.stats.footprint_w, s.stats.footprint_h);
}

// ─── Comandos ────────────────────────────────────────────────────────────────

void GameState::apply_command(const Command& cmd) {
    if (cmd.issuer == data::kGaia || cmd.issuer > config_.num_players) return;

    // Solo actores vivos que controla quien da la orden; orden por índice (determinista).
    std::vector<core::EntityHandle> actors;
    for (core::EntityHandle h : cmd.actors) {
        const SpawnableEntity* e = entities_.get(h);
        if (e && e->current_player == cmd.issuer) actors.push_back(h);
    }
    std::sort(actors.begin(), actors.end(), [](auto a, auto b) { return a.index < b.index; });
    actors.erase(std::unique(actors.begin(), actors.end()), actors.end());
    if (actors.empty()) return;

    auto movable = [&](core::EntityHandle h) { return sheet_of(*entities_.get(h)).can_move(); };

    switch (cmd.type) {
    case CommandType::Stop:
        for (auto h : actors) entities_.get(h)->action.reset();
        break;

    case CommandType::Move: {
        std::vector<core::EntityHandle> units;
        std::copy_if(actors.begin(), actors.end(), std::back_inserter(units), movable);
        command_move(units, cmd.target_pos);
        break;
    }

    case CommandType::Gather: {
        const SpawnableEntity* node = entities_.get(cmd.target);
        if (!node || !node->resource_node) break;
        const data::ResourceTag res = node->resource_node->resource;
        std::vector<core::EntityHandle> others;
        for (auto h : actors) {
            SpawnableEntity& e = *entities_.get(h);
            const auto& s = sheet_of(e);
            if (s.stats.has(data::Capability::Gather) && s.stats.gather_per_tick[data::index_of(res)] > Fixed{}) {
                e.action = std::make_unique<GatherAction>(cmd.target, res);
            } else if (s.can_move()) {
                others.push_back(h);
            }
        }
        command_move(others, node->position);
        break;
    }

    case CommandType::Build: {
        const SpawnableEntity* site = entities_.get(cmd.target);
        if (!site || site->is_complete() || site->current_player != cmd.issuer) break;
        for (auto h : actors) {
            SpawnableEntity& e = *entities_.get(h);
            if (sheet_of(e).stats.has(data::Capability::Build)) e.action = std::make_unique<BuildAction>(cmd.target);
        }
        break;
    }

    case CommandType::PlaceBuilding:
        command_place_building(cmd, actors);
        break;

    case CommandType::Train: {
        const auto& unit = data_.sheet(cmd.type_arg);
        for (auto h : actors) {
            SpawnableEntity& b = *entities_.get(h);
            if (!b.is_complete() || !sheet_of(b).can_train(cmd.type_arg)) continue;
            if (b.production.size() >= kMaxQueue || !can_afford(cmd.issuer, unit.stats.cost)) continue;
            auto& res = players_[cmd.issuer].resources;
            for (std::size_t i = 0; i < data::kResourceCount; ++i) res[i] -= unit.stats.cost[i];
            const uint32_t t = std::max<uint32_t>(1, unit.stats.train_ticks);
            b.production.push_back({cmd.type_arg, t, t, unit.stats.cost});
            break;  // una orden de entrenar = una unidad, en el primer edificio válido
        }
        break;
    }

    case CommandType::CancelTrain: {
        SpawnableEntity& b = *entities_.get(actors.front());
        if (cmd.index < 0 || static_cast<std::size_t>(cmd.index) >= b.production.size()) break;
        const auto it = b.production.begin() + cmd.index;
        auto& res = players_[cmd.issuer].resources;
        for (std::size_t i = 0; i < data::kResourceCount; ++i) res[i] += it->paid[i];
        b.production.erase(it);
        break;
    }

    case CommandType::SetRally:
        for (auto h : actors) {
            SpawnableEntity& b = *entities_.get(h);
            if (!sheet_of(b).trains.empty()) b.rally_point = cmd.target_pos;
        }
        break;
    }
}

void GameState::command_move(const std::vector<core::EntityHandle>& actors, Vec2F target) {
    // Formación en cuadrícula alrededor del destino para que no se apilen.
    const int n = static_cast<int>(actors.size());
    if (n == 0) return;
    int side = 1;
    while (side * side < n) ++side;
    const int rows = (n + side - 1) / side;
    const Fixed spacing = Fixed::ratio(3, 4);
    const Fixed max_x = Fixed::from_int(map_.width()) - Fixed::ratio(1, 2);
    const Fixed max_y = Fixed::from_int(map_.height()) - Fixed::ratio(1, 2);
    for (int i = 0; i < n; ++i) {
        const Fixed ox = spacing * Fixed::ratio(2 * (i % side) - (side - 1), 2);
        const Fixed oy = spacing * Fixed::ratio(2 * (i / side) - (rows - 1), 2);
        const Vec2F t{std::clamp(target.x + ox, Fixed::ratio(1, 2), max_x), std::clamp(target.y + oy, Fixed::ratio(1, 2), max_y)};
        entities_.get(actors[static_cast<std::size_t>(i)])->action = std::make_unique<MoveAction>(t);
    }
}

void GameState::command_place_building(const Command& cmd, const std::vector<core::EntityHandle>& actors) {
    const auto& sheet = data_.sheet(cmd.type_arg);
    std::vector<core::EntityHandle> builders;
    for (auto h : actors) {
        if (sheet_of(*entities_.get(h)).can_build(cmd.type_arg)) builders.push_back(h);
    }
    if (builders.empty() || !can_place(cmd.type_arg, cmd.tile) || !can_afford(cmd.issuer, sheet.stats.cost)) return;

    auto& res = players_[cmd.issuer].resources;
    for (std::size_t i = 0; i < data::kResourceCount; ++i) res[i] -= sheet.stats.cost[i];

    // Los comandos se aplican fuera de la iteración de entidades: aquí se puede crear ya.
    const core::EntityHandle site = spawn_now({cmd.type_arg, cmd.issuer, cmd.tile, true});
    if (!site) return;
    displace_units_from(cmd.tile, sheet.stats.footprint_w, sheet.stats.footprint_h);
    for (auto h : builders) entities_.get(h)->action = std::make_unique<BuildAction>(site);
}

// ─── Sistemas ────────────────────────────────────────────────────────────────

void GameState::production_system() {
    for (int p = 1; p <= config_.num_players; ++p) {
        PlayerState& owner = players_[static_cast<std::size_t>(p)];
        // Por índice: spawn_now añade a owner.units, no a owner.buildings
        for (std::size_t i = 0; i < owner.buildings.size(); ++i) {
            SpawnableEntity* b = entities_.get(owner.buildings[i]);
            if (!b || !b->is_complete() || b->production.empty()) continue;
            ProductionJob& job = b->production.front();
            if (job.ticks_remaining > 0) --job.ticks_remaining;
            if (job.ticks_remaining > 0) continue;

            const auto& unit = data_.sheet(job.type);
            if (owner.population_current + unit.stats.population_cost > owner.population_cap) continue;  // falta población

            const auto& bs = sheet_of(*b);
            const core::TilePos exit{b->anchor.x + bs.stats.footprint_w, b->anchor.y + bs.stats.footprint_h};
            const std::optional<Vec2F> rally = b->rally_point;
            const data::TypeId type = job.type;
            b->production.pop_front();
            // spawn_now puede reubicar el SlotMap: no usar `b` después
            const core::EntityHandle u = spawn_now({type, owner.id, exit, false});
            if (u && rally) command_move({u}, *rally);
        }
    }
}

void GameState::action_system() {
    entities_.for_each([&](core::EntityHandle h, SpawnableEntity& e) {
        e.prev_position = e.position;
        if (!e.action) return;
        const Vec2F before = e.position;
        if (e.action->tick(e, *this) == ActionStatus::Done) e.action.reset();
        if (!(before == e.position)) spatial_.move(h, before, e.position);
    });
}

// ─── Movimiento ──────────────────────────────────────────────────────────────

std::optional<MovePlan> GameState::plan_path(const SpawnableEntity& e, Vec2F target) {
    auto result = pathfinder_.find(map_, core::tile_of(e.position), core::tile_of(target));
    if (!result) return std::nullopt;

    // Centros de tile como puntos de paso; el último es el destino exacto si es alcanzable.
    std::vector<Vec2F> points;
    points.reserve(result->tiles.size() + 1);
    for (core::TilePos t : result->tiles) points.push_back(core::tile_center(t));
    const bool exact = result->reached_goal && core::tile_of(target) == result->goal;
    if (points.empty()) points.push_back(exact ? target : core::tile_center(result->goal));
    else if (exact) points.back() = target;

    // Suavizado: saltar puntos mientras la línea recta siga libre (máx. 16 por salto).
    MovePlan plan;
    Vec2F from = e.position;
    std::size_t i = 0;
    while (i < points.size()) {
        std::size_t best = i;
        const std::size_t limit = std::min(points.size(), i + 16);
        for (std::size_t j = i + 1; j < limit; ++j) {
            if (line_is_clear(map_, from, points[j])) best = j;
        }
        plan.waypoints.push_back(points[best]);
        from = points[best];
        i = best + 1;
    }
    return plan;
}

std::optional<MovePlan> GameState::plan_path_to(const SpawnableEntity& e, const SpawnableEntity& target) {
    if (in_reach(e, target)) return MovePlan{};
    const auto& ts = sheet_of(target);
    const int x0 = target.anchor.x - 1, y0 = target.anchor.y - 1;
    const int x1 = target.anchor.x + ts.stats.footprint_w, y1 = target.anchor.y + ts.stats.footprint_h;
    // Tile libre del anillo alrededor del footprint más cercano a la unidad
    std::optional<core::TilePos> best;
    int64_t best_d = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (x != x0 && x != x1 && y != y0 && y != y1) continue;
            if (!map_.is_passable(x, y)) continue;
            const int64_t d = dist_sq_raw(core::tile_center({x, y}), e.position);
            if (!best || d < best_d) {
                best = core::TilePos{x, y};
                best_d = d;
            }
        }
    }
    if (!best) return std::nullopt;
    return plan_path(e, core::tile_center(*best));
}

StepResult GameState::step_along(SpawnableEntity& e, MovePlan& plan) {
    Fixed step = sheet_of(e).stats.move_speed / Fixed::from_int(kTicksPerSecond);
    while (step > Fixed{} && !plan.done()) {
        const Vec2F d = plan.current() - e.position;
        const Fixed dist = core::length(d);
        const bool reaches = dist <= step;
        const Vec2F candidate = reaches ? plan.current() : e.position + Vec2F{d.x * step / dist, d.y * step / dist};
        const core::TilePos from = core::tile_of(e.position);
        const core::TilePos to = core::tile_of(candidate);
        if (!(from == to) && !map_.is_passable(to)) return StepResult::Blocked;  // un edificio nuevo cortó el camino
        e.position = candidate;
        if (reaches) {
            step -= dist;
            ++plan.next;
        } else {
            step = Fixed{};
        }
    }
    return plan.done() ? StepResult::Arrived : StepResult::Moving;
}

bool GameState::in_reach(const SpawnableEntity& e, const SpawnableEntity& target) const {
    const auto& ts = sheet_of(target);
    const Fixed x0 = Fixed::from_int(target.anchor.x), y0 = Fixed::from_int(target.anchor.y);
    const Fixed x1 = x0 + Fixed::from_int(ts.stats.footprint_w), y1 = y0 + Fixed::from_int(ts.stats.footprint_h);
    const Fixed dx = std::max({x0 - e.position.x, Fixed{}, e.position.x - x1});
    const Fixed dy = std::max({y0 - e.position.y, Fixed{}, e.position.y - y1});
    return core::length({dx, dy}) <= Fixed::ratio(4, 5);
}

// ─── Economía ────────────────────────────────────────────────────────────────

core::EntityHandle GameState::nearest_dropoff(data::PlayerId p, data::ResourceTag r, Vec2F from) const {
    core::EntityHandle best;
    int64_t best_d = 0;
    for (core::EntityHandle h : players_.at(p).buildings) {
        const SpawnableEntity* b = entities_.get(h);
        if (!b || !b->is_complete() || !sheet_of(*b).stats.dropoff.test(data::index_of(r))) continue;
        const int64_t d = dist_sq_raw(b->position, from);
        if (!best || d < best_d) {
            best = h;
            best_d = d;
        }
    }
    return best;
}

core::EntityHandle GameState::nearest_resource(data::ResourceTag r, Vec2F from, int max_tiles) const {
    std::vector<core::EntityHandle> found;
    const Fixed m = Fixed::from_int(max_tiles);
    spatial_.query_rect({from.x - m, from.y - m}, {from.x + m, from.y + m}, found);
    core::EntityHandle best;
    int64_t best_d = 0;
    for (core::EntityHandle h : found) {
        const SpawnableEntity* n = entities_.get(h);
        if (!n || !n->resource_node || n->resource_node->resource != r || n->resource_node->amount_remaining <= 0) continue;
        const int64_t d = dist_sq_raw(n->position, from);
        if (d > static_cast<int64_t>(m.raw) * m.raw) continue;
        if (!best || d < best_d || (d == best_d && h.index < best.index)) {
            best = h;
            best_d = d;
        }
    }
    return best;
}

bool GameState::contribute_build(SpawnableEntity& site) {
    if (!site.construction) return true;
    ConstructionData& c = *site.construction;
    // Rendimiento decreciente: el primer constructor del tick aporta 1, los demás 1/3
    c.progress += (c.last_tick == tick_) ? Fixed::ratio(1, 3) : Fixed::from_int(1);
    c.last_tick = tick_;
    const auto& s = sheet_of(site);
    const Fixed total = Fixed::from_int(static_cast<int32_t>(c.total_ticks));
    if (c.progress >= total) {
        site.construction.reset();
        site.hp = s.stats.max_hp;
        PlayerState& owner = players_[site.current_player];
        owner.population_cap = std::min(kMaxPopulation, owner.population_cap + s.stats.population_provided);
        return true;
    }
    site.hp = std::max<int32_t>(1, static_cast<int32_t>(static_cast<int64_t>(s.stats.max_hp) * c.progress.raw / total.raw));
    return false;
}

// ─── Entidades ───────────────────────────────────────────────────────────────

core::EntityHandle GameState::spawn_now(const SpawnRequest& req) {
    const data::ObjSheet& sheet = data_.sheet(req.type);
    SpawnableEntity e;
    e.type = req.type;
    e.classes = sheet.stats.classes;
    e.original_player = e.current_player = req.owner;
    e.hp = sheet.stats.max_hp;

    if (sheet.is_unit()) {
        auto tile = Pathfinder::nearest_passable(map_, req.tile, 24);
        if (!tile) return {};
        e.anchor = *tile;
        e.position = core::tile_center(*tile);
    } else {
        const int w = sheet.stats.footprint_w;
        const int h = sheet.stats.footprint_h;
        if (!map_.is_area_free(req.tile.x, req.tile.y, w, h)) return {};
        e.anchor = req.tile;
        e.position = {Fixed::from_int(req.tile.x) + Fixed::ratio(w, 2), Fixed::from_int(req.tile.y) + Fixed::ratio(h, 2)};
        map_.add_blocker(req.tile.x, req.tile.y, w, h);
    }
    if (req.as_construction_site && sheet.is_building()) {
        e.construction = ConstructionData{Fixed{}, std::max<uint32_t>(1, sheet.stats.build_ticks)};
        e.hp = 1;
    }
    e.prev_position = e.position;
    if (sheet.resource) e.resource_node = ResourceNodeData{sheet.resource->resource, sheet.resource->amount, sheet.resource->amount};

    const Vec2F pos = e.position;
    const bool complete = !e.construction;
    const core::EntityHandle handle = entities_.insert(std::move(e));
    entities_.get(handle)->self = handle;
    spatial_.insert(handle, pos);

    PlayerState& owner = players_[req.owner];
    if (sheet.is_unit()) owner.units.push_back(handle);
    if (sheet.is_building()) owner.buildings.push_back(handle);
    owner.population_current += sheet.stats.population_cost;
    if (complete) owner.population_cap = std::min(kMaxPopulation, owner.population_cap + sheet.stats.population_provided);
    return handle;
}

void GameState::despawn_now(core::EntityHandle h) {
    SpawnableEntity* e = entities_.get(h);
    if (!e) return;
    const data::ObjSheet& sheet = data_.sheet(e->type);
    if (!sheet.is_unit()) map_.remove_blocker(e->anchor.x, e->anchor.y, sheet.stats.footprint_w, sheet.stats.footprint_h);
    spatial_.remove(h, e->position);
    PlayerState& owner = players_[e->current_player];
    std::erase(owner.units, h);
    std::erase(owner.buildings, h);
    owner.population_current -= sheet.stats.population_cost;
    if (e->is_complete()) owner.population_cap -= sheet.stats.population_provided;
    entities_.erase(h);
}

void GameState::apply_pending() {
    // Un mismo handle puede pedirse dos veces en un tick (p. ej. dos aldeanos agotan el mismo árbol)
    for (core::EntityHandle h : pending_despawns_) despawn_now(h);
    pending_despawns_.clear();
    for (const SpawnRequest& req : pending_spawns_) spawn_now(req);
    pending_spawns_.clear();
}

void GameState::displace_units_from(core::TilePos anchor, int w, int h) {
    query_.clear();
    spatial_.query_rect({Fixed::from_int(anchor.x - 1), Fixed::from_int(anchor.y - 1)},
                        {Fixed::from_int(anchor.x + w + 1), Fixed::from_int(anchor.y + h + 1)}, query_);
    for (core::EntityHandle u : query_) {
        SpawnableEntity* e = entities_.get(u);
        if (!e || !sheet_of(*e).is_unit()) continue;
        const core::TilePos t = core::tile_of(e->position);
        if (t.x < anchor.x || t.y < anchor.y || t.x >= anchor.x + w || t.y >= anchor.y + h) continue;
        if (auto free = Pathfinder::nearest_passable(map_, t, 8)) {
            const Vec2F before = e->position;
            e->position = e->prev_position = core::tile_center(*free);
            spatial_.move(u, before, e->position);
        }
    }
}

// ─── Hash ────────────────────────────────────────────────────────────────────

uint64_t GameState::state_hash() const {
    uint64_t h = 1469598103934665603ULL;  // FNV-1a
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xffu;
            h *= 1099511628211ULL;
        }
    };
    mix(tick_);
    for (int p = 1; p <= config_.num_players; ++p) {
        for (int32_t r : players_[static_cast<std::size_t>(p)].resources) mix(static_cast<uint32_t>(r));
    }
    entities_.for_each([&](core::EntityHandle handle, const SpawnableEntity& e) {
        mix(handle.index);
        mix(handle.generation);
        mix(e.type.value);
        mix(e.current_player);
        mix(static_cast<uint32_t>(e.position.x.raw));
        mix(static_cast<uint32_t>(e.position.y.raw));
        mix(static_cast<uint32_t>(e.hp));
        mix(static_cast<uint32_t>(e.carry.amount.raw));
        if (e.resource_node) mix(static_cast<uint32_t>(e.resource_node->amount_remaining));
    });
    return h;
}

}  // namespace sim
