#include "sim/game/game_state.hpp"

#include <algorithm>

namespace sim {

using core::Fixed;
using core::Vec2F;

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
    movement_system();
    apply_pending();
    ++tick_;
}

void GameState::apply_command(const Command& cmd) {
    if (cmd.issuer == data::kGaia || cmd.issuer > config_.num_players) return;

    // Solo actores vivos que controla quien da la orden; orden por índice (determinista).
    std::vector<core::EntityHandle> actors;
    for (core::EntityHandle h : cmd.actors) {
        const SpawnableEntity* e = entities_.get(h);
        if (e && e->current_player == cmd.issuer && data_.sheet(e->type).can_move()) actors.push_back(h);
    }
    std::sort(actors.begin(), actors.end(), [](auto a, auto b) { return a.index < b.index; });
    actors.erase(std::unique(actors.begin(), actors.end()), actors.end());

    switch (cmd.type) {
    case CommandType::Stop:
        for (auto h : actors) entities_.get(h)->move.reset();
        break;

    case CommandType::Move: {
        // Formación en cuadrícula alrededor del destino para que no se apilen.
        const int n = static_cast<int>(actors.size());
        int side = 1;
        while (side * side < n) ++side;
        const int rows = (n + side - 1) / side;
        const Fixed spacing = Fixed::ratio(3, 4);
        const Fixed max_x = Fixed::from_int(map_.width()) - Fixed::ratio(1, 2);
        const Fixed max_y = Fixed::from_int(map_.height()) - Fixed::ratio(1, 2);
        for (int i = 0; i < n; ++i) {
            const Fixed ox = spacing * Fixed::ratio(2 * (i % side) - (side - 1), 2);
            const Fixed oy = spacing * Fixed::ratio(2 * (i / side) - (rows - 1), 2);
            Vec2F target{std::clamp(cmd.target_pos.x + ox, Fixed::ratio(1, 2), max_x),
                         std::clamp(cmd.target_pos.y + oy, Fixed::ratio(1, 2), max_y)};
            order_move(*entities_.get(actors[i]), target);
        }
        break;
    }
    }
}

void GameState::order_move(SpawnableEntity& e, Vec2F target) {
    auto result = pathfinder_.find(map_, core::tile_of(e.position), core::tile_of(target));
    if (!result) {
        e.move.reset();
        return;
    }
    // Centros de tile como puntos de paso; el último es el destino exacto si es alcanzable.
    std::vector<Vec2F> points;
    points.reserve(result->tiles.size() + 1);
    for (core::TilePos t : result->tiles) points.push_back(core::tile_center(t));
    const bool exact = result->reached_goal && core::tile_of(target) == result->goal;
    if (points.empty()) points.push_back(exact ? target : core::tile_center(result->goal));
    else if (exact) points.back() = target;

    // Suavizado: saltar puntos mientras la línea recta siga libre (máx. 16 por salto).
    MoveOrder order;
    Vec2F from = e.position;
    std::size_t i = 0;
    while (i < points.size()) {
        std::size_t best = i;
        const std::size_t limit = std::min(points.size(), i + 16);
        for (std::size_t j = i + 1; j < limit; ++j) {
            if (line_is_clear(map_, from, points[j])) best = j;
        }
        order.waypoints.push_back(points[best]);
        from = points[best];
        i = best + 1;
    }
    e.move = std::move(order);
}

void GameState::movement_system() {
    const Fixed ticks = Fixed::from_int(kTicksPerSecond);
    entities_.for_each([&](core::EntityHandle h, SpawnableEntity& e) {
        e.prev_position = e.position;
        if (!e.move) return;
        Fixed step = data_.sheet(e.type).stats.move_speed / ticks;
        const Vec2F before = e.position;
        while (step > Fixed{} && e.move && !e.move->done()) {
            const Vec2F d = e.move->current() - e.position;
            const Fixed dist = core::length(d);
            if (dist <= step) {
                e.position = e.move->current();
                step -= dist;
                ++e.move->next;
            } else {
                e.position = e.position + Vec2F{d.x * step / dist, d.y * step / dist};
                step = Fixed{};
            }
        }
        if (e.move && e.move->done()) e.move.reset();
        spatial_.move(h, before, e.position);
    });
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
    e.prev_position = e.position;
    if (sheet.resource) e.resource_node = ResourceNodeData{sheet.resource->resource, sheet.resource->amount, sheet.resource->amount};

    const Vec2F pos = e.position;
    const core::EntityHandle handle = entities_.insert(std::move(e));
    entities_.get(handle)->self = handle;
    spatial_.insert(handle, pos);

    PlayerState& owner = players_[req.owner];
    if (sheet.is_unit()) owner.units.push_back(handle);
    if (sheet.is_building()) owner.buildings.push_back(handle);
    owner.population_current += sheet.stats.population_cost;
    owner.population_cap = std::min(200, owner.population_cap + sheet.stats.population_provided);
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
    owner.population_cap -= sheet.stats.population_provided;
    entities_.erase(h);
}

void GameState::apply_pending() {
    for (core::EntityHandle h : pending_despawns_) despawn_now(h);
    pending_despawns_.clear();
    for (const SpawnRequest& req : pending_spawns_) spawn_now(req);
    pending_spawns_.clear();
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
    entities_.for_each([&](core::EntityHandle handle, const SpawnableEntity& e) {
        mix(handle.index);
        mix(handle.generation);
        mix(e.type.value);
        mix(e.current_player);
        mix(static_cast<uint32_t>(e.position.x.raw));
        mix(static_cast<uint32_t>(e.position.y.raw));
        mix(static_cast<uint32_t>(e.hp));
    });
    return h;
}

}  // namespace sim
