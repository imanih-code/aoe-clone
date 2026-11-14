#include "sim/game/game_state.hpp"

#include <algorithm>
#include <cstdlib>

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
    for (auto& ps : players_) ps.visibility.reset(map_.width(), map_.height());
    carcass_type_ = data_.find_type("carcass");
    apply_pending();
    visibility_system(true);
}

// ─── Tick ────────────────────────────────────────────────────────────────────

void GameState::step(std::span<const Command> commands) {
    // Orden fijo de sistemas (architecture.md, Diagrama 7)
    for (const Command& cmd : commands) apply_command(cmd);
    production_system();
    animal_system();       // animales: huir, deambular, ganado que cambia de dueño
    skill_system();        // enfriamientos, auras y efectos temporales que caducan
    heal_system();         // sanadores ociosos atienden a los heridos
    action_system();       // mover, recolectar, construir, atacar (el daño se acumula)
    projectile_system();   // las flechas en vuelo avanzan e impactan
    resolve_damage();      // se aplica todo el daño a la vez: dos que se matan, mueren los dos
    auto_attack_system();  // unidades ociosas agresivas buscan objetivo
    visibility_system(false);  // niebla de guerra (cada 4 ticks)
    victory_system();
    apply_pending();       // altas y bajas al final del tick
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

namespace {

Placement placement_of(const data::ObjSheet& s) {
    switch (s.stats.placement) {
    case data::PlacementRule::Water: return Placement::Water;
    case data::PlacementRule::Shore: return Placement::Shore;
    case data::PlacementRule::Land: break;
    }
    return Placement::Land;
}

}  // namespace

bool GameState::can_place(data::TypeId building, core::TilePos tile) const {
    const auto& s = data_.sheet(building);
    return s.is_building() && map_.is_area_free(tile.x, tile.y, s.stats.footprint_w, s.stats.footprint_h, placement_of(s));
}

PathQuery GameState::path_query(const SpawnableEntity& e) const {
    PathQuery q;
    q.layer = move_layer(e);
    // Gaia (animales) conoce el terreno; los jugadores solo lo que han explorado
    if (e.current_player != data::kGaia) q.fog = &players_[e.current_player].visibility;
    return q;
}

// ─── Comandos ────────────────────────────────────────────────────────────────

std::unique_ptr<IAction> GameState::make_action(SpawnableEntity& e, const Order& order) {
    if (e.garrisoned_in) return nullptr;  // dentro de un edificio no se hace nada
    switch (order.type) {
    case CommandType::Move:
        if (can_move(e)) return std::make_unique<MoveAction>(order.pos);
        break;
    case CommandType::AttackMove:
        if (can_move(e)) return std::make_unique<AttackMoveAction>(order.pos);
        break;
    case CommandType::Gather: {
        const SpawnableEntity* t = entities_.get(order.target);
        if (!t || !has_cap(e, data::Capability::Gather)) break;
        const auto& st = stats_of(e);
        if (t->resource_node) {
            const auto res = t->resource_node->resource;
            if (st.gather_per_tick[data::index_of(res)] > Fixed{}) return std::make_unique<GatherAction>(order.target, res);
        } else if (is_huntable(*t) && st.gather_per_tick[data::index_of(data::ResourceTag::Food)] > Fixed{} && can_target(e, *t)) {
            return std::make_unique<GatherAction>(order.target, data::ResourceTag::Food);  // cazar y luego recolectar
        }
        break;
    }
    case CommandType::Build: {
        const SpawnableEntity* t = entities_.get(order.target);
        if (t && !t->is_complete() && t->current_player == e.current_player && has_cap(e, data::Capability::Build)) {
            return std::make_unique<BuildAction>(order.target);
        }
        break;
    }
    case CommandType::Attack: {
        const SpawnableEntity* t = entities_.get(order.target);
        if (t && can_attack(e) && can_target(e, *t)) return std::make_unique<AttackAction>(order.target, false);
        break;
    }
    case CommandType::Convert: {
        const SpawnableEntity* t = entities_.get(order.target);
        if (t && can_convert(e, *t)) return std::make_unique<ConvertAction>(order.target);
        break;
    }
    case CommandType::Heal: {
        const SpawnableEntity* t = entities_.get(order.target);
        if (t && can_heal(e, *t)) return std::make_unique<HealAction>(order.target);
        break;
    }
    case CommandType::Garrison: {
        const SpawnableEntity* t = entities_.get(order.target);
        if (t && can_garrison(e, *t)) return std::make_unique<GarrisonAction>(order.target);
        break;
    }
    default: break;
    }
    return nullptr;
}

void GameState::give_order(SpawnableEntity& e, const Order& order, bool queued) {
    if (queued && (e.action || !e.orders.empty())) {
        e.orders.push_back(order);  // Shift: a la cola, en orden de llegada
        return;
    }
    e.orders.clear();
    e.action = make_action(e, order);
}

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

    auto movable = [&](core::EntityHandle h) { return can_move(*entities_.get(h)); };

    switch (cmd.type) {
    case CommandType::Stop:
        for (auto h : actors) {
            entities_.get(h)->action.reset();
            entities_.get(h)->orders.clear();
        }
        break;

    case CommandType::Move:
    case CommandType::AttackMove: {
        std::vector<core::EntityHandle> units;
        std::copy_if(actors.begin(), actors.end(), std::back_inserter(units), movable);
        command_move(units, cmd.target_pos, cmd.type == CommandType::AttackMove, cmd.queued);
        break;
    }

    case CommandType::Gather: {
        const SpawnableEntity* node = entities_.get(cmd.target);
        if (!node) break;
        if (node->resource_node) {
            if (!is_known_to(cmd.issuer, *node)) break;
            if (node->current_player != data::kGaia && (node->current_player != cmd.issuer || !node->is_complete())) break;  // granja ajena o en obra
        } else if (!is_huntable(*node) || !is_visible_to(cmd.issuer, *node)) {
            break;
        }
        std::vector<core::EntityHandle> others;
        const Order order{CommandType::Gather, cmd.target, node->position, {}};
        for (auto h : actors) {
            SpawnableEntity& e = *entities_.get(h);
            auto probe = make_action(e, order);  // ¿puede recolectar esto?
            if (probe) give_order(e, order, cmd.queued);
            else if (can_move(e)) others.push_back(h);
        }
        command_move(others, node->position, false, cmd.queued);
        break;
    }

    case CommandType::Build: {
        const SpawnableEntity* site = entities_.get(cmd.target);
        if (!site || site->is_complete() || site->current_player != cmd.issuer) break;
        for (auto h : actors) {
            SpawnableEntity& e = *entities_.get(h);
            if (has_cap(e, data::Capability::Build)) give_order(e, {CommandType::Build, cmd.target, site->position, {}}, cmd.queued);
        }
        break;
    }

    case CommandType::PlaceBuilding:
        command_place_building(cmd, actors);
        break;

    case CommandType::Train: {
        const auto& unit = data_.sheet(cmd.type_arg);
        if (players_[cmd.issuer].age < unit.stats.min_age) break;
        for (auto h : actors) {
            SpawnableEntity& b = *entities_.get(h);
            if (!b.is_complete()) continue;
            const auto options = trainable_at(cmd.issuer, b.type);  // incluye civ y líneas de mejora
            if (std::find(options.begin(), options.end(), cmd.type_arg) == options.end()) continue;
            if (b.production.size() >= kMaxQueue || !can_afford(cmd.issuer, unit.stats.cost)) continue;
            auto& res = players_[cmd.issuer].resources;
            for (std::size_t i = 0; i < data::kResourceCount; ++i) res[i] -= unit.stats.cost[i];
            const uint32_t t = std::max<uint32_t>(1, unit.stats.train_ticks);
            b.production.push_back({cmd.type_arg, std::nullopt, t, t, unit.stats.cost});
            break;  // una orden de entrenar = una unidad, en el primer edificio válido
        }
        break;
    }

    case CommandType::Research: {
        if (cmd.index < 0 || static_cast<std::size_t>(cmd.index) >= data_.techs().size()) break;
        const auto& tech = data_.tech(static_cast<data::TechId>(cmd.index));
        SpawnableEntity& b = *entities_.get(actors.front());
        if (!b.is_complete() || b.type != tech.researched_at || b.production.size() >= kMaxQueue) break;
        if (tech_status(cmd.issuer, tech.id) != TechStatus::Available || !can_afford(cmd.issuer, tech.cost)) break;
        auto& res = players_[cmd.issuer].resources;
        for (std::size_t i = 0; i < data::kResourceCount; ++i) res[i] -= tech.cost[i];
        b.production.push_back({tech.researched_at, tech.id, tech.research_ticks, tech.research_ticks, tech.cost});
        break;
    }

    case CommandType::Attack: {
        const SpawnableEntity* target = entities_.get(cmd.target);
        if (!target || !is_visible_to(cmd.issuer, *target)) break;  // no se ataca lo que no se ve
        std::vector<core::EntityHandle> others;
        const Order order{CommandType::Attack, cmd.target, target->position, {}};
        for (auto h : actors) {
            SpawnableEntity& e = *entities_.get(h);
            if (can_attack(e) && can_target(e, *target)) give_order(e, order, cmd.queued);
            else if (can_move(e)) others.push_back(h);
        }
        command_move(others, target->position, false, cmd.queued);
        break;
    }

    case CommandType::Convert:
    case CommandType::Heal: {
        const SpawnableEntity* target = entities_.get(cmd.target);
        if (!target || !is_visible_to(cmd.issuer, *target)) break;
        const Order order{cmd.type, cmd.target, target->position, {}};
        for (auto h : actors) {
            SpawnableEntity& e = *entities_.get(h);
            if (make_action(e, order)) give_order(e, order, cmd.queued);
        }
        break;
    }

    case CommandType::Garrison: {
        const SpawnableEntity* building = entities_.get(cmd.target);
        if (!building) break;
        const Order order{CommandType::Garrison, cmd.target, building->position, {}};
        for (auto h : actors) give_order(*entities_.get(h), order, cmd.queued);
        break;
    }

    case CommandType::Ungarrison:
        for (auto h : actors) ungarrison(h);
        break;

    case CommandType::UseSkill:
        for (auto h : actors) use_skill(*entities_.get(h), static_cast<std::size_t>(std::max(0, cmd.index)));
        break;

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
            if (!trainable_at(cmd.issuer, b.type).empty()) b.rally_point = cmd.target_pos;
        }
        break;
    }
}

void GameState::command_move(const std::vector<core::EntityHandle>& actors, Vec2F target, bool attack_move, bool queued) {
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
        give_order(*entities_.get(actors[static_cast<std::size_t>(i)]),
                   {attack_move ? CommandType::AttackMove : CommandType::Move, {}, t, {}}, queued);
    }
}

void GameState::command_place_building(const Command& cmd, const std::vector<core::EntityHandle>& actors) {
    const auto& sheet = data_.sheet(cmd.type_arg);
    if (players_[cmd.issuer].age < sheet.stats.min_age || is_disabled(cmd.issuer, cmd.type_arg)) return;
    std::vector<core::EntityHandle> builders;
    for (auto h : actors) {
        if (can_build(*entities_.get(h), cmd.type_arg)) builders.push_back(h);
    }
    if (builders.empty() || !can_place(cmd.type_arg, cmd.tile) || !can_afford(cmd.issuer, sheet.stats.cost)) return;
    // Solo se construye en terreno explorado (las dos esquinas del footprint)
    const VisibilityMap& fog = players_[cmd.issuer].visibility;
    if (!fog.explored(cmd.tile) || !fog.explored({cmd.tile.x + sheet.stats.footprint_w - 1, cmd.tile.y + sheet.stats.footprint_h - 1})) return;

    auto& res = players_[cmd.issuer].resources;
    for (std::size_t i = 0; i < data::kResourceCount; ++i) res[i] -= sheet.stats.cost[i];

    // Los comandos se aplican fuera de la iteración de entidades: aquí se puede crear ya.
    // Con Shift, el cimiento aparece ya y la obra se añade a la cola de cada constructor.
    SpawnRequest req;
    req.type = cmd.type_arg;
    req.owner = cmd.issuer;
    req.tile = cmd.tile;
    req.as_construction_site = true;
    const core::EntityHandle site = spawn_now(req);
    if (!site) return;
    if (!sheet.stats.walkable) displace_units_from(cmd.tile, sheet.stats.footprint_w, sheet.stats.footprint_h);
    const Vec2F pos = entities_.get(site)->position;
    for (auto h : builders) give_order(*entities_.get(h), {CommandType::Build, site, pos, {}}, cmd.queued);
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

            if (job.tech) {
                const data::TechId tech = *job.tech;
                b->production.pop_front();
                complete_research(owner.id, tech);  // puede cambiar tipos: no usar `b` después
                continue;
            }

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

int GameState::gatherers_on(core::EntityHandle node) const {
    return node.index < gather_claims_.size() ? gather_claims_[node.index] : 0;
}

void GameState::claim_gather(core::EntityHandle node) {
    if (node.index >= gather_claims_.size()) gather_claims_.resize(node.index + 1, 0);
    ++gather_claims_[node.index];
}

void GameState::action_system() {
    // Quién está recolectando qué (para no amontonar más de los sitios que tiene cada recurso).
    // Se cuenta al principio del tick a partir del estado; quien entra durante el tick suma su plaza.

    std::fill(gather_claims_.begin(), gather_claims_.end(), 0);
    entities_.for_each([&](core::EntityHandle, const SpawnableEntity& e) {
        if (!e.action || e.action->kind() != ActionKind::Gather) return;
        const auto& gather = static_cast<const GatherAction&>(*e.action);
        if (gather.is_gathering()) claim_gather(gather.target());
    });

    entities_.for_each([&](core::EntityHandle h, SpawnableEntity& e) {
        e.prev_position = e.position;
        if (e.garrisoned_in) return;  // dentro de un edificio: quieta
        // Sin acción pero con órdenes en cola: empezar la siguiente (las inválidas se descartan)
        while (!e.action && !e.orders.empty()) {
            const Order next = e.orders.front();
            e.orders.pop_front();
            e.action = make_action(e, next);
        }
        if (!e.action) return;
        const Vec2F before = e.position;
        if (e.action->tick(e, *this) == ActionStatus::Done) {
            e.action.reset();
            while (!e.action && !e.orders.empty()) {
                const Order next = e.orders.front();
                e.orders.pop_front();
                e.action = make_action(e, next);
            }
        }
        // Si acaba de entrar en un edificio ya no está en el índice espacial: no volver a meterla
        if (!(before == e.position) && !e.garrisoned_in) spatial_.move(h, before, e.position);
    });
}

void GameState::continue_after_build(SpawnableEntity& builder, core::EntityHandle site_handle) {
    if (!builder.orders.empty()) return;  // tiene más trabajo en cola: respetarlo
    const SpawnableEntity* site = entities_.get(site_handle);
    if (!site || !stats_of(builder).has(data::Capability::Gather)) return;
    const auto& ss = sheet_of(*site);
    // Granja: el que la construye la trabaja
    if (site->resource_node && ss.stats.walkable) {
        builder.orders.push_front({CommandType::Gather, site_handle, site->position, {}});
        return;
    }
    // Depósito: el recurso que acepta más cercano, si hay alguno libre cerca (comida, madera, oro, piedra)
    for (std::size_t r = 0; r < data::kResourceCount; ++r) {
        if (!ss.stats.dropoff.test(r) || stats_of(builder).gather_per_tick[r] <= Fixed{}) continue;
        // La plaza mayor acepta todo: ahí no tiene sentido elegir uno al azar
        if (ss.stats.dropoff.count() == data::kResourceCount) return;
        const core::EntityHandle node = nearest_resource(static_cast<data::ResourceTag>(r), site->position, 7, builder.current_player,
                                                         /*skip_full=*/true, move_layer(builder));
        if (node) {
            builder.orders.push_front({CommandType::Gather, node, entities_.get(node)->position, {}});
            return;
        }
    }
}

// ─── Movimiento ──────────────────────────────────────────────────────────────

std::optional<MovePlan> GameState::plan_path(const SpawnableEntity& e, Vec2F target) {
    const PathQuery query = path_query(e);
    auto result = pathfinder_.find(map_, core::tile_of(e.position), core::tile_of(target), query);
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
            if (line_is_clear(map_, from, points[j], query)) best = j;
        }
        plan.waypoints.push_back(points[best]);
        from = points[best];
        i = best + 1;
    }
    return plan;
}

std::optional<MovePlan> GameState::plan_path_to(const SpawnableEntity& e, const SpawnableEntity& target) {
    const auto& ts = sheet_of(target);
    // Recursos y edificios transitables (granja, carne): se va hasta dentro, no basta con llegar al borde
    if (ts.stats.walkable) return plan_path(e, target.position);
    if (in_reach(e, target)) return MovePlan{};
    const PathQuery query = path_query(e);
    const int x0 = target.anchor.x - 1, y0 = target.anchor.y - 1;
    const int x1 = target.anchor.x + ts.stats.footprint_w, y1 = target.anchor.y + ts.stats.footprint_h;
    // Tile libre del anillo alrededor del footprint más cercano a la unidad
    std::optional<core::TilePos> best;
    int64_t best_d = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (x != x0 && x != x1 && y != y0 && y != y1) continue;
            if (!query.believes_passable(map_, {x, y})) continue;
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
    const MoveLayer layer = move_layer(e);
    // Cada medio segundo, comprobar el tramo siguiente con lo que se ha descubierto (niebla):
    // si ahora se ve que hay un lago o un bosque en medio, replanificar.
    if ((tick_ + e.self.index) % 10 == 0 && !plan.done() && e.current_player != data::kGaia) {
        const PathQuery query = path_query(e);
        if (!line_is_clear(map_, e.position, plan.current(), query)) return StepResult::Blocked;
    }
    Fixed step = stats_of(e).move_speed / Fixed::from_int(kTicksPerSecond);
    while (step > Fixed{} && !plan.done()) {
        const Vec2F d = plan.current() - e.position;
        const Fixed dist = core::length(d);
        const bool reaches = dist <= step;
        const Vec2F candidate = reaches ? plan.current() : e.position + Vec2F{d.x * step / dist, d.y * step / dist};
        const core::TilePos from = core::tile_of(e.position);
        const core::TilePos to = core::tile_of(candidate);
        if (!(from == to) && !map_.is_passable(to, layer)) return StepResult::Blocked;  // obstáculo real (edificio nuevo, agua...)
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

core::EntityHandle GameState::nearest_dropoff(data::PlayerId p, data::ResourceTag r, Vec2F from, MoveLayer layer) const {
    core::EntityHandle best;
    int64_t best_d = 0;
    for (core::EntityHandle h : players_.at(p).buildings) {
        const SpawnableEntity* b = entities_.get(h);
        if (!b || !b->is_complete() || !sheet_of(*b).stats.dropoff.test(data::index_of(r))) continue;
        // Los barcos solo pueden descargar en edificios del agua (muelle)
        if (layer == MoveLayer::Water && sheet_of(*b).stats.placement == data::PlacementRule::Land) continue;
        const int64_t d = dist_sq_raw(b->position, from);
        if (!best || d < best_d) {
            best = h;
            best_d = d;
        }
    }
    return best;
}

core::EntityHandle GameState::nearest_resource(data::ResourceTag r, Vec2F from, int max_tiles, data::PlayerId for_player, bool skip_full,
                                               std::optional<MoveLayer> reach, core::EntityHandle exclude) const {
    std::vector<core::EntityHandle> found;
    const Fixed m = Fixed::from_int(max_tiles);
    spatial_.query_rect({from.x - m, from.y - m}, {from.x + m, from.y + m}, found);
    core::EntityHandle best;
    int64_t best_d = 0;
    for (core::EntityHandle h : found) {
        if (h == exclude) continue;
        const SpawnableEntity* n = entities_.get(h);
        if (!n || !n->resource_node || n->resource_node->resource != r || n->resource_node->amount_remaining <= 0) continue;
        if (n->current_player != data::kGaia && (n->current_player != for_player || !n->is_complete())) continue;  // granjas ajenas o en obra
        const auto& ns = sheet_of(*n);
        if (reach) {
            const bool deep = ns.stats.placement == data::PlacementRule::Water;
            const bool in_water = ns.stats.placement != data::PlacementRule::Land;
            if (*reach == MoveLayer::Land && deep) continue;       // aguas profundas: solo barcos
            if (*reach == MoveLayer::Water && !in_water) continue;  // los barcos no talan árboles
        }
        if (skip_full && gatherers_on(h) >= stats_of(*n).gather_slots) continue;
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
    const int32_t max_hp = stats_of(site).max_hp;
    const Fixed total = Fixed::from_int(static_cast<int32_t>(c.total_ticks));
    if (c.progress >= total) {
        site.construction.reset();
        site.hp = max_hp;
        PlayerState& owner = players_[site.current_player];
        owner.population_cap = std::min(kMaxPopulation, owner.population_cap + s.stats.population_provided);
        return true;
    }
    site.hp = std::max<int32_t>(1, static_cast<int32_t>(static_cast<int64_t>(max_hp) * c.progress.raw / total.raw));
    return false;
}

// ─── Entidades ───────────────────────────────────────────────────────────────

core::EntityHandle GameState::spawn_now(const SpawnRequest& req) {
    const data::ObjSheet& sheet = data_.sheet(req.type);
    SpawnableEntity e;
    e.type = req.type;
    e.classes = sheet.stats.classes;
    e.current_player = req.owner;
    e.original_player = req.original_owner.value_or(req.owner);  // un trigger puede heredar el original
    e.hp = stats_for(e.original_player, req.type).max_hp;
    if (req.hp_fraction) e.hp = std::max<int32_t>(1, (Fixed::from_int(e.hp) * *req.hp_fraction).floor_int());

    if (sheet.is_projectile()) {
        // Proyectil: sin footprint, no bloquea ni cuenta para ningún jugador
        e.position = req.exact_position.value_or(core::tile_center(req.tile));
        e.prev_position = e.position;
        e.projectile = req.projectile;
        const Vec2F pos = e.position;
        const core::EntityHandle handle = entities_.insert(std::move(e));
        entities_.get(handle)->self = handle;
        spatial_.insert(handle, pos);
        return handle;
    }

    if (sheet.is_unit()) {
        PathQuery q;
        q.layer = sheet.stats.naval ? MoveLayer::Water : MoveLayer::Land;
        auto tile = Pathfinder::nearest_passable(map_, req.tile, 24, q);
        if (!tile) return {};
        e.anchor = *tile;
        e.position = core::tile_center(*tile);
    } else {
        const int w = sheet.stats.footprint_w;
        const int h = sheet.stats.footprint_h;
        const Placement rule = placement_of(sheet);
        core::TilePos at = req.tile;
        if (!map_.is_area_free(at.x, at.y, w, h, rule)) {
            if (!req.nearby_ok) return {};
            // Buscar un hueco cercano (la carne de un animal que cayó junto a un árbol)
            bool found = false;
            for (int r = 1; r <= 3 && !found; ++r) {
                for (int dy = -r; dy <= r && !found; ++dy) {
                    for (int dx = -r; dx <= r && !found; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                        if (map_.is_area_free(req.tile.x + dx, req.tile.y + dy, w, h, rule)) {
                            at = {req.tile.x + dx, req.tile.y + dy};
                            found = true;
                        }
                    }
                }
            }
            if (!found) return {};
        }
        e.anchor = at;
        e.position = {Fixed::from_int(at.x) + Fixed::ratio(w, 2), Fixed::from_int(at.y) + Fixed::ratio(h, 2)};
        map_.add_blocker(at.x, at.y, w, h, !sheet.stats.walkable);
    }
    if (req.as_construction_site && sheet.is_building()) {
        e.construction = ConstructionData{Fixed{}, std::max<uint32_t>(1, sheet.stats.build_ticks)};
        e.hp = 1;
    }
    e.prev_position = e.position;
    if (sheet.resource) {
        const int32_t amount = req.resource_amount.value_or(sheet.resource->amount);
        e.resource_node = ResourceNodeData{sheet.resource->resource, amount, amount};
    }

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
    // Trigger al nacer (sus efectos se aplican al final del tick)
    if (sheet.is_unit() || sheet.is_building()) fire(data::TriggerEvent::OnSpawn, *entities_.get(handle), {});
    return handle;
}

void GameState::despawn_now(core::EntityHandle h) {
    SpawnableEntity* e = entities_.get(h);
    if (!e) return;
    // Un edificio que cae suelta a los de dentro; una unidad que muere dentro sale de la lista
    if (!e->garrison.empty()) {
        ungarrison(h);
        e = entities_.get(h);
    }
    if (SpawnableEntity* holder = entities_.get(e->garrisoned_in)) std::erase(holder->garrison, h);
    const data::ObjSheet& sheet = data_.sheet(e->type);
    if (!sheet.is_unit() && !sheet.is_projectile()) {
        map_.remove_blocker(e->anchor.x, e->anchor.y, sheet.stats.footprint_w, sheet.stats.footprint_h, !sheet.stats.walkable);
    }
    spatial_.remove(h, e->position);
    PlayerState& owner = players_[e->current_player];
    std::erase(owner.units, h);
    std::erase(owner.buildings, h);
    if (!sheet.is_projectile()) {
        owner.population_current -= sheet.stats.population_cost;
        if (e->is_complete()) owner.population_cap -= sheet.stats.population_provided;
    }
    entities_.erase(h);
}

void GameState::apply_pending() {
    // Un mismo handle puede pedirse dos veces en un tick (p. ej. dos aldeanos agotan el mismo árbol)
    for (core::EntityHandle h : pending_despawns_) despawn_now(h);
    pending_despawns_.clear();
    // Se vacía antes: si un spawn dispara on_spawn y pide más, van al tick siguiente (sin cadenas infinitas)
    std::vector<SpawnRequest> spawns;
    spawns.swap(pending_spawns_);
    for (const SpawnRequest& req : spawns) spawn_now(req);
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
        PathQuery q;
        q.layer = move_layer(*e);
        if (auto free = Pathfinder::nearest_passable(map_, t, 8, q)) {
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
        const PlayerState& ps = players_[static_cast<std::size_t>(p)];
        for (int32_t r : ps.resources) mix(static_cast<uint32_t>(r));
        mix(ps.age);
        mix(ps.modifiers.size());
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
        mix(e.garrisoned_in.index);
        if (e.resource_node) mix(static_cast<uint32_t>(e.resource_node->amount_remaining));
    });
    return h;
}

}  // namespace sim
