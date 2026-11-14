#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "core/rng.hpp"
#include "core/slot_map.hpp"
#include "data/data_registry.hpp"
#include "sim/commands/command.hpp"
#include "sim/entities/spawnable_entity.hpp"
#include "sim/player/player_state.hpp"
#include "sim/world/pathfinder.hpp"
#include "sim/world/spatial_grid.hpp"
#include "sim/world/tile_map.hpp"

namespace sim {

inline constexpr int kTicksPerSecond = 20;

struct GameConfig {
    uint32_t seed = 1;
    int map_width = 96;
    int map_height = 96;
    int num_players = 2;
};

struct SpawnRequest {
    data::TypeId type;
    data::PlayerId owner = data::kGaia;
    core::TilePos tile;  // unidades: tile deseado; edificios/recursos: esquina del footprint
};

class GameState {
public:
    GameState(const data::DataRegistry& data, const GameConfig& config);
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;

    // Avanza un tick fijo aplicando los comandos de ese tick.
    void step(std::span<const Command> commands);

    uint32_t tick() const { return tick_; }
    const GameConfig& config() const { return config_; }
    const data::DataRegistry& data() const { return data_; }
    const TileMap& map() const { return map_; }
    const SpatialGrid& spatial() const { return spatial_; }
    const core::SlotMap<SpawnableEntity>& entities() const { return entities_; }
    const SpawnableEntity* entity(core::EntityHandle h) const { return entities_.get(h); }
    const PlayerState& player(data::PlayerId id) const { return players_.at(id); }
    int num_players() const { return config_.num_players; }
    core::Vec2F start_position(data::PlayerId id) const { return start_positions_.at(id); }

    // Hash del estado: dos simulaciones con la misma seed y comandos deben coincidir.
    uint64_t state_hash() const;

private:
    // ─── Montaje de la partida (map_generation.cpp) ──────────────────────
    void setup_players();
    void generate_map();
    void place_start_positions();
    void generate_lakes();
    void place_player_bases();
    void place_near_base_resources();
    void place_map_wide_resources();
    int place_cluster(data::TypeId type, core::TilePos origin, int count, int min_dist_from_start);
    int distance_to_nearest_start(core::TilePos t) const;

    // ─── Ciclo de vida de entidades ──────────────────────────────────────
    core::EntityHandle spawn_now(const SpawnRequest& req);
    void despawn_now(core::EntityHandle h);
    void apply_pending();

    // ─── Sistemas ────────────────────────────────────────────────────────
    void apply_command(const Command& cmd);
    void order_move(SpawnableEntity& e, core::Vec2F target);
    void movement_system();

    const data::DataRegistry& data_;
    GameConfig config_;
    uint32_t tick_ = 0;
    core::Rng rng_;

    TileMap map_;
    SpatialGrid spatial_;
    Pathfinder pathfinder_;
    core::SlotMap<SpawnableEntity> entities_;
    std::array<PlayerState, data::kMaxPlayers> players_{};
    std::array<core::Vec2F, data::kMaxPlayers> start_positions_{};

    std::vector<SpawnRequest> pending_spawns_;
    std::vector<core::EntityHandle> pending_despawns_;
};

}  // namespace sim
