#pragma once

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

struct EntityStats {
    ClassSet classes;
    int32_t max_hp = 1;
    core::Fixed move_speed;  // tiles por segundo; 0 = inmóvil
    core::Fixed radius = core::Fixed::ratio(1, 4);
    uint8_t footprint_w = 1;  // tiles que ocupa (edificios y recursos)
    uint8_t footprint_h = 1;
    uint8_t sight = 4;
    int32_t population_cost = 0;
    int32_t population_provided = 0;
};

struct ResourceNodeDef {
    ResourceTag resource = ResourceTag::Wood;
    int32_t amount = 0;
};

struct ObjSheet {
    TypeId type;
    std::string key;           // "villager", "town_center"... clave estable en JSON
    std::string display_name;  // UTF-8
    EntityKind kind = EntityKind::Unit;
    EntityStats stats;
    std::optional<ResourceNodeDef> resource;
    VisualDef visual;

    bool is_unit() const { return kind == EntityKind::Unit; }
    bool is_building() const { return kind == EntityKind::Building; }
    bool is_resource() const { return kind == EntityKind::Resource; }
    bool can_move() const { return stats.move_speed > core::Fixed{}; }
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
