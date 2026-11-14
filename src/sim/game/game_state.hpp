#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/rng.hpp"
#include "core/slot_map.hpp"
#include "data/data_registry.hpp"
#include "sim/actions/action.hpp"
#include "sim/commands/command.hpp"
#include "sim/entities/spawnable_entity.hpp"
#include "sim/player/player_state.hpp"
#include "sim/world/pathfinder.hpp"
#include "sim/world/spatial_grid.hpp"
#include "sim/world/tile_map.hpp"

namespace sim {

inline constexpr int kTicksPerSecond = 20;
inline constexpr std::size_t kMaxQueue = 15;
inline constexpr int32_t kMaxPopulation = 200;

struct GameConfig {
    uint32_t seed = 1;
    int map_width = 96;
    int map_height = 96;
    int num_players = 2;
    std::vector<std::string> civs;  // key por jugador; vacío = la de data/start.json
};

struct SpawnRequest {
    data::TypeId type;
    data::PlayerId owner = data::kGaia;
    core::TilePos tile;  // unidades: tile deseado; edificios/recursos: esquina del footprint
    bool as_construction_site = false;
    std::optional<core::Vec2F> exact_position;  // proyectiles
    std::optional<ProjectileData> projectile;
};

struct DamageEvent {
    core::EntityHandle target;
    core::EntityHandle source;
    int32_t amount = 0;
    data::PlayerId attacker_player = data::kGaia;
    data::TypeId attacker_type;
};

enum class TechStatus { Available, Researched, Queued, NeedsAge, NeedsTech, Disabled };

class GameState {
public:
    GameState(const data::DataRegistry& data, const GameConfig& config);
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;

    // Avanza un tick fijo aplicando los comandos de ese tick.
    void step(std::span<const Command> commands);

    // ─── Consultas (render, UI, IA, tests) ───────────────────────────────
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
    const data::ObjSheet& sheet_of(const SpawnableEntity& e) const { return data_.sheet(e.type); }

    bool can_afford(data::PlayerId p, const data::ResourceArray& cost) const;
    bool can_place(data::TypeId building, core::TilePos tile) const;

    // ─── Stats efectivos (ficha + bonus de civ + tecnologías del jugador) ─
    const data::EntityStats& stats_for(data::PlayerId p, data::TypeId t) const;
    // Siempre contra original_player (architecture.md §3)
    const data::EntityStats& stats_of(const SpawnableEntity& e) const { return stats_for(e.original_player, e.type); }

    // ─── Lo que un jugador puede hacer (civ, mejoras, edad) ─────────────
    std::vector<data::TypeId> trainable_at(data::PlayerId p, data::TypeId building) const;
    std::vector<data::TypeId> buildable_by(data::PlayerId p, const data::ObjSheet& unit) const;
    std::vector<data::TechId> techs_at(data::PlayerId p, data::TypeId building) const;
    TechStatus tech_status(data::PlayerId p, data::TechId t) const;
    bool is_disabled(data::PlayerId p, data::TypeId t) const;

    // ─── Combate ─────────────────────────────────────────────────────────
    bool is_enemy(const SpawnableEntity& a, const SpawnableEntity& b) const;
    core::Fixed distance_to(const SpawnableEntity& from, const SpawnableEntity& to) const;  // al borde del objetivo
    bool in_attack_range(const SpawnableEntity& attacker, const SpawnableEntity& target) const;
    core::EntityHandle find_enemy_near(const SpawnableEntity& e, int radius_tiles) const;

    // ─── Para tests y futuras herramientas (editor, escenarios) ──────────
    core::EntityHandle debug_spawn(data::TypeId type, data::PlayerId owner, core::TilePos tile);
    void debug_complete_research(data::PlayerId p, data::TechId t) { complete_research(p, t); }
    void debug_set_resources(data::PlayerId p, const data::ResourceArray& r) { players_.at(p).resources = r; }

    // Hash del estado: dos simulaciones con la misma seed y comandos deben coincidir.
    uint64_t state_hash() const;

    // ─── API interna para las acciones (sim/actions) ─────────────────────
    SpawnableEntity* entity_mut(core::EntityHandle h) { return entities_.get(h); }
    PlayerState& player_mut(data::PlayerId id) { return players_.at(id); }
    std::optional<MovePlan> plan_path(const SpawnableEntity& e, core::Vec2F target);
    // Camino hasta quedar junto al footprint de `target`. Plan vacío si ya está al lado.
    std::optional<MovePlan> plan_path_to(const SpawnableEntity& e, const SpawnableEntity& target);
    StepResult step_along(SpawnableEntity& e, MovePlan& plan);
    bool in_reach(const SpawnableEntity& e, const SpawnableEntity& target) const;
    core::EntityHandle nearest_dropoff(data::PlayerId p, data::ResourceTag r, core::Vec2F from) const;
    core::EntityHandle nearest_resource(data::ResourceTag r, core::Vec2F from, int max_tiles) const;
    // Devuelve true si la obra queda terminada con esta aportación.
    bool contribute_build(SpawnableEntity& site);
    void request_despawn(core::EntityHandle h) { pending_despawns_.push_back(h); }
    // Golpe cuerpo a cuerpo (daño acumulado) o disparo (proyectil que se crea al final del tick)
    void perform_attack(const SpawnableEntity& attacker, const SpawnableEntity& target);

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
    void displace_units_from(core::TilePos anchor, int w, int h);

    // ─── Comandos y sistemas ─────────────────────────────────────────────
    void apply_command(const Command& cmd);
    void command_move(const std::vector<core::EntityHandle>& actors, core::Vec2F target);
    void command_place_building(const Command& cmd, const std::vector<core::EntityHandle>& actors);
    void production_system();
    void action_system();
    void projectile_system();
    void resolve_damage();
    void auto_attack_system();

    // ─── Tecnologías y civilizaciones ────────────────────────────────────
    void complete_research(data::PlayerId p, data::TechId t);
    void on_modifiers_changed(data::PlayerId p);
    void retype(SpawnableEntity& e, data::TypeId to);

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
    std::vector<DamageEvent> damage_;
    std::vector<core::EntityHandle> query_;  // buffer reutilizable
};

}  // namespace sim
