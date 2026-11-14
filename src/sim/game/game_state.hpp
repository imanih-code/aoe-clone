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
    std::optional<int32_t> resource_amount;      // carne de un animal: la cantidad la fija el animal
    bool nearby_ok = false;                      // si el tile está ocupado, vale uno libre cercano
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
    // Hostil: jugadores distintos, o animales que atacan (lobos) o se defienden (jabalíes)
    bool is_enemy(const SpawnableEntity& a, const SpawnableEntity& b) const;
    // Se le puede atacar a propósito: enemigos y además cualquier animal (cazar)
    bool can_target(const SpawnableEntity& a, const SpawnableEntity& b) const;
    bool is_huntable(const SpawnableEntity& e) const;
    core::Fixed distance_to(const SpawnableEntity& from, const SpawnableEntity& to) const;  // al borde del objetivo
    bool in_attack_range(const SpawnableEntity& attacker, const SpawnableEntity& target) const;
    core::EntityHandle find_enemy_near(const SpawnableEntity& e, int radius_tiles) const;

    // ─── Niebla de guerra ────────────────────────────────────────────────
    const VisibilityMap& visibility(data::PlayerId p) const { return players_.at(p).visibility; }
    // Unidades: el tile donde están es visible. Edificios y recursos: basta con haberlo explorado.
    bool is_visible_to(data::PlayerId p, const SpawnableEntity& e) const;
    bool is_known_to(data::PlayerId p, const SpawnableEntity& e) const;

    // ─── Fin de partida ──────────────────────────────────────────────────
    bool is_game_over() const { return game_over_; }
    std::optional<data::PlayerId> winner() const { return winner_; }

    // ─── Para tests y futuras herramientas (editor, escenarios) ──────────
    void debug_destroy_player(data::PlayerId p);
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
    core::EntityHandle nearest_dropoff(data::PlayerId p, data::ResourceTag r, core::Vec2F from, MoveLayer layer = MoveLayer::Land) const;
    // Recursos de Gaia y, si for_player no es Gaia, también sus propias granjas terminadas
    // skip_full: ignora los que ya tienen todos sus sitios ocupados
    // reach: tierra → no los de aguas profundas; agua → solo los del agua
    core::EntityHandle nearest_resource(data::ResourceTag r, core::Vec2F from, int max_tiles, data::PlayerId for_player = data::kGaia,
                                        bool skip_full = false, std::optional<MoveLayer> reach = std::nullopt,
                                        core::EntityHandle exclude = {}) const;
    int gatherers_on(core::EntityHandle node) const;
    void claim_gather(core::EntityHandle node);
    // Al terminar un depósito o una granja, el constructor se pone a trabajar ahí si hay recurso cerca
    void continue_after_build(SpawnableEntity& builder, core::EntityHandle site);
    MoveLayer move_layer(const SpawnableEntity& e) const { return sheet_of(e).stats.naval ? MoveLayer::Water : MoveLayer::Land; }
    // Lo que sabe el dueño de la unidad: capa por la que se mueve y su niebla
    PathQuery path_query(const SpawnableEntity& e) const;
    uint32_t random(uint32_t n) { return rng_.below(n); }  // aleatoriedad determinista de la simulación
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
    void command_move(const std::vector<core::EntityHandle>& actors, core::Vec2F target, bool attack_move = false, bool queued = false);
    // Orden a una unidad: sustituye la actual o, con queued, se añade a su cola
    void give_order(SpawnableEntity& e, const Order& order, bool queued);
    std::unique_ptr<IAction> make_action(SpawnableEntity& e, const Order& order);
    void convert(core::EntityHandle h, data::PlayerId new_owner);
    void animal_system();
    void command_place_building(const Command& cmd, const std::vector<core::EntityHandle>& actors);
    void production_system();
    void action_system();
    void projectile_system();
    void resolve_damage();
    void auto_attack_system();
    void visibility_system(bool force);
    void victory_system();

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
    bool game_over_ = false;
    std::optional<data::PlayerId> winner_;
    std::vector<core::EntityHandle> query_;  // buffer reutilizable
    std::vector<uint16_t> gather_claims_;    // recolectores por recurso (índice del handle), rehecho cada tick
    std::optional<data::TypeId> carcass_type_;
};

}  // namespace sim
