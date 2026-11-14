#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/fixed.hpp"
#include "data/ids.hpp"

namespace data {

enum class EntityKind : uint8_t { Unit, Building, Resource };

// Cómo se dibuja mientras no haya sprites: una figura con etiqueta.
enum class ShapeKind : uint8_t { Unit, Cavalry, Building, Tree, Mine, Bush };

// Solo lo usa la capa de render (float permitido: no afecta a la simulación).
struct VisualDef {
    ShapeKind shape = ShapeKind::Unit;
    float size = 0.3f;       // radio aproximado en tiles
    float height = 1.0f;     // altura en tiles (edificios, árboles)
    uint32_t color = 0xffffffffu;  // RGBA; si use_player_color, se usa el color del dueño
    bool use_player_color = true;
    std::string label;       // texto de la etiqueta
};

// Lo que una entidad sabe hacer (architecture.md §11.9). Fijo por ficha hasta la Fase 6.
enum class Capability : uint8_t { Move, Gather, Build };
inline constexpr std::size_t kCapabilityCount = 3;
using CapabilitySet = std::bitset<kCapabilityCount>;

struct EntityStats {
    ClassSet classes;
    CapabilitySet capabilities;
    int32_t max_hp = 1;
    core::Fixed move_speed;  // tiles por segundo; 0 = inmóvil
    core::Fixed radius = core::Fixed::ratio(1, 4);
    uint8_t footprint_w = 1;  // tiles que ocupa (edificios y recursos)
    uint8_t footprint_h = 1;
    uint8_t sight = 4;
    int32_t population_cost = 0;
    int32_t population_provided = 0;

    // ─── Producción ──────────────────────────────────────────────────────
    ResourceArray cost{};
    uint32_t train_ticks = 0;  // unidades: tiempo de entrenamiento
    uint32_t build_ticks = 0;  // edificios: tiempo de construcción con un constructor

    // ─── Economía ────────────────────────────────────────────────────────
    std::array<core::Fixed, kResourceCount> gather_per_tick{};  // por recurso
    int32_t carry_capacity = 0;
    std::bitset<kResourceCount> dropoff;  // recursos que se pueden depositar aquí

    bool has(Capability c) const { return capabilities.test(static_cast<std::size_t>(c)); }
};

struct ResourceNodeDef {
    ResourceTag resource = ResourceTag::Wood;
    int32_t amount = 0;
};

struct ObjSheet {
    TypeId type;
    std::string key;           // "villager", "town_center"... clave estable en JSON
    std::string display_name;  // UTF-8
    std::string description;   // texto de ayuda del botón
    EntityKind kind = EntityKind::Unit;
    EntityStats stats;
    std::optional<ResourceNodeDef> resource;
    VisualDef visual;
    std::vector<TypeId> trains;  // edificios: unidades que entrena
    std::vector<TypeId> builds;  // unidades: edificios que construye

    bool is_unit() const { return kind == EntityKind::Unit; }
    bool is_building() const { return kind == EntityKind::Building; }
    bool is_resource() const { return kind == EntityKind::Resource; }
    bool can_move() const { return stats.has(Capability::Move) && stats.move_speed > core::Fixed{}; }
    bool can_build(TypeId t) const {
        return stats.has(Capability::Build) && std::find(builds.begin(), builds.end(), t) != builds.end();
    }
    bool can_train(TypeId t) const { return std::find(trains.begin(), trains.end(), t) != trains.end(); }
};

// ─── Configuración de partida (data/start.json) ─────────────────────────────

struct PlayerDef {
    std::string name;
    uint32_t color = 0xffffffffu;
};

struct StartBuilding {
    TypeId type;
    core::TilePos offset;  // respecto al centro de la base
};

struct StartUnits {
    TypeId type;
    int count = 1;
};

// Grupo de recursos cerca de cada base (bayas, oro, piedra, bosque cercano)
struct NearBaseCluster {
    TypeId type;
    int count_min = 1, count_max = 1;
    int dist_min = 6, dist_max = 10;
};

// Grupos repartidos por el mapa
struct MapCluster {
    TypeId type;
    int clusters = 1;
    int size_min = 1, size_max = 1;
    int min_dist_from_start = 12;
};

struct StartConfig {
    ResourceArray resources{};
    std::vector<PlayerDef> players;
    std::vector<StartBuilding> buildings;
    std::vector<StartUnits> units;
    std::vector<NearBaseCluster> near_base;
    std::vector<MapCluster> map_wide;
};

}  // namespace data
