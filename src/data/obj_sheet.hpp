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

enum class EntityKind : uint8_t { Unit, Building, Resource, Projectile };

// Cómo se dibuja mientras no haya sprites: una figura con etiqueta.
enum class ShapeKind : uint8_t { Unit, Cavalry, Building, Tree, Mine, Bush, Arrow };

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
enum class Capability : uint8_t { Move, Gather, Build, Attack };
inline constexpr std::size_t kCapabilityCount = 4;
using CapabilitySet = std::bitset<kCapabilityCount>;

// Ataque o armadura contra una clase de armadura (architecture.md §4.5)
struct DamageEntry {
    ArmorClassId armor_class = 0;
    int32_t value = 0;
    bool operator==(const DamageEntry&) const = default;
};

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
    uint8_t min_age = 0;       // edad mínima para entrenar/construir

    // ─── Economía ────────────────────────────────────────────────────────
    std::array<core::Fixed, kResourceCount> gather_per_tick{};  // por recurso
    int32_t carry_capacity = 0;
    std::bitset<kResourceCount> dropoff;  // recursos que se pueden depositar aquí

    // ─── Combate ─────────────────────────────────────────────────────────
    std::vector<DamageEntry> attacks;
    std::vector<DamageEntry> armors;
    uint32_t reload_ticks = 40;
    core::Fixed range;      // 0 = cuerpo a cuerpo
    core::Fixed min_range;
    std::optional<TypeId> projectile;  // a distancia: ficha del proyectil
    bool aggressive = false;           // ataca sola a los enemigos que ve

    bool has(Capability c) const { return capabilities.test(static_cast<std::size_t>(c)); }

    int32_t attack_vs(ArmorClassId c) const {
        for (const auto& a : attacks) if (a.armor_class == c) return a.value;
        return 0;
    }
    int32_t armor_vs(ArmorClassId c) const {
        for (const auto& a : armors) if (a.armor_class == c) return a.value;
        return 0;
    }
};

// Daño de un ataque (architecture.md §4.5): por cada clase que esté en los ataques
// del atacante Y en las armaduras del defensor, max(0, ataque - armadura); mínimo 1.
inline int32_t compute_damage(const EntityStats& attacker, const EntityStats& defender) {
    int32_t total = 0;
    for (const auto& a : attacker.attacks) {
        for (const auto& d : defender.armors) {
            if (a.armor_class == d.armor_class) total += std::max(0, a.value - d.value);
        }
    }
    return std::max(1, total);
}

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
    bool is_projectile() const { return kind == EntityKind::Projectile; }
    bool can_move() const { return stats.has(Capability::Move) && stats.move_speed > core::Fixed{}; }
    bool can_attack() const { return stats.has(Capability::Attack) && !stats.attacks.empty(); }
    bool can_build(TypeId t) const {
        return stats.has(Capability::Build) && std::find(builds.begin(), builds.end(), t) != builds.end();
    }
    bool can_train(TypeId t) const { return std::find(trains.begin(), trains.end(), t) != trains.end(); }
};

// ─── Modificadores, tecnologías y civilizaciones ─────────────────────────────

enum class StatTag : uint8_t { MaxHp, Attack, Armor, MoveSpeed, Range, Reload, GatherRate, CarryCapacity, Sight };
enum class ModifierOp : uint8_t { AddFlat, AddPercent, MultiplyFlat, SetValue };

struct StatModifier {
    StatTag stat = StatTag::MaxHp;
    uint8_t sub = 0;  // Attack/Armor: ArmorClassId · GatherRate: ResourceTag
    ModifierOp op = ModifierOp::AddFlat;
    core::Fixed value;
    int16_t priority = 0;  // solo desempata SetValue (architecture.md §4.3)
};

// Un paquete de cambios de stats: lo que da una tecnología o un bonus de civilización.
struct Modifier {
    std::string id;
    std::optional<TypeId> target_type;  // vacío = cualquier tipo
    ClassSet target_classes;            // vacío = cualquier clase; si no, basta con tener una
    std::vector<StatModifier> stats;

    bool applies_to(TypeId t, const ClassSet& classes) const {
        if (target_type && *target_type != t) return false;
        if (target_classes.bits.any() && !target_classes.intersects(classes)) return false;
        return true;
    }
};

using TechId = uint16_t;

struct Upgrade {
    TypeId from;
    TypeId to;
};

struct TechTemplate {
    TechId id = 0;
    std::string key;
    std::string name;
    std::string description;
    TypeId researched_at;
    ResourceArray cost{};
    uint32_t research_ticks = 0;
    uint8_t min_age = 0;
    std::vector<TechId> requires_techs;
    std::vector<Modifier> modifiers;
    std::vector<Upgrade> upgrades;        // líneas de mejora (architecture.md §11.8)
    std::optional<uint8_t> advances_to_age;
};

struct CivTemplate {
    std::string key;
    std::string name;
    std::string description;
    std::vector<Modifier> bonuses;           // se aplican al empezar la partida
    std::vector<TypeId> disabled_types;      // no puede entrenar/construir
    std::vector<TechId> disabled_techs;
    struct ExtraTrain {
        TypeId building;
        TypeId unit;
    };
    std::vector<ExtraTrain> extra_trains;    // unidades únicas y similares
};

// ─── Configuración de partida (data/start.json) ─────────────────────────────

struct PlayerDef {
    std::string name;
    uint32_t color = 0xffffffffu;
    std::string civ;  // key de data/civs; vacío = la primera
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
