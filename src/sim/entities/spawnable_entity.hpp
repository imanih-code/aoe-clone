#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <string>
#include <memory>
#include <optional>
#include <vector>

#include "core/entity_handle.hpp"
#include "core/fixed.hpp"
#include "data/ids.hpp"
#include "data/obj_sheet.hpp"
#include "sim/actions/action.hpp"
#include "sim/commands/command.hpp"

namespace sim {

struct ResourceNodeData {
    data::ResourceTag resource = data::ResourceTag::Wood;
    int32_t amount_remaining = 0;
    int32_t amount_max = 0;
};

// Lo que lleva encima un recolector
struct CarryState {
    data::ResourceTag resource = data::ResourceTag::Food;
    core::Fixed amount;
};

// Edificio en obra
struct ConstructionData {
    core::Fixed progress;  // en ticks de trabajo de un constructor
    uint32_t total_ticks = 1;
    uint32_t last_tick = std::numeric_limits<uint32_t>::max();  // para el rendimiento decreciente
};

struct ProductionJob {
    data::TypeId type;                // unidad a entrenar (si tech está vacío)
    std::optional<data::TechId> tech; // o tecnología a investigar
    uint32_t ticks_remaining = 0;
    uint32_t ticks_total = 0;
    data::ResourceArray paid{};  // se devuelve si se cancela
};

// Proyectil en vuelo: una entidad ligera (architecture.md §8, decisión 2)
struct ProjectileData {
    core::EntityHandle source;
    core::EntityHandle target;
    core::Vec2F aim;  // dónde estaba el objetivo al disparar: si se mueve, puede fallar
    int32_t damage = 0;
    data::PlayerId owner = data::kGaia;
    data::TypeId source_type;
};

// Orden pendiente en la cola de una unidad (Shift + orden). Se convierte en acción al acabar la anterior.
struct Order {
    CommandType type = CommandType::Move;
    core::EntityHandle target;
    core::Vec2F pos;
    data::TypeId type_arg;
};

// Lo que la entidad puede hacer AHORA: ficha + modificadores del jugador original + los suyos propios.
// Se recalcula cuando cambia algo de eso (mutability.md, rebuild_capabilities).
struct CapabilityCache {
    bool valid = false;
    uint32_t epoch = 0;             // modifier_epoch del jugador original al calcularla
    uint32_t instance_version = 0;  // versión de los modificadores de la propia entidad
    data::TypeId type;
    data::CapabilitySet caps;
    std::vector<std::pair<data::Capability, data::ActionConfig>> configs;
    std::vector<std::string> triggers;
    std::vector<std::string> skills;
};

struct SkillState {
    std::string id;
    uint32_t cooldown = 0;  // ticks hasta poder volver a usarla
    uint32_t active = 0;    // ticks que le quedan de efecto (auras)
};

struct SpawnableEntity {
    core::EntityHandle self;
    data::TypeId type;
    data::ClassSet classes;

    data::PlayerId original_player = data::kGaia;  // de quién salen los stats; nunca cambia
    data::PlayerId current_player = data::kGaia;   // quién la controla

    core::Vec2F position;       // centro, en tiles
    core::Vec2F prev_position;  // posición al inicio del tick (interpolación del render)
    core::TilePos anchor;       // esquina del footprint (edificios y recursos)
    int32_t hp = 1;
    uint32_t last_damaged_tick = std::numeric_limits<uint32_t>::max();  // animales: huir un rato tras un golpe

    // ─── Componentes opcionales (architecture.md, Diagrama 5) ────────────
    std::optional<ResourceNodeData> resource_node;
    std::optional<ConstructionData> construction;  // con valor = en obra
    CarryState carry;
    std::deque<ProductionJob> production;
    std::optional<core::Vec2F> rally_point;
    std::optional<ProjectileData> projectile;

    // ─── Acción en curso ─────────────────────────────────────────────────
    std::unique_ptr<IAction> action;
    std::deque<Order> orders;  // cola de órdenes (Shift), en orden de llegada

    // ─── Mutabilidad ─────────────────────────────────────────────────────
    mutable CapabilityCache caps;
    std::vector<data::Modifier> instance_mods;  // efectos sobre esta entidad (habilidades, triggers)
    std::vector<uint32_t> instance_mod_until;   // tick en que caduca cada uno (0 = permanente)
    uint32_t instance_version = 0;
    mutable std::optional<data::EntityStats> instance_stats;  // stats con instance_mods (caché)
    mutable uint32_t instance_stats_epoch = 0;
    mutable uint32_t instance_stats_version = 0;
    std::map<std::string, uint32_t> trigger_uses;
    std::vector<SkillState> skill_states;

    // ─── Guarnición ──────────────────────────────────────────────────────
    core::EntityHandle garrisoned_in;           // dentro de qué edificio está (oculta)
    std::vector<core::EntityHandle> garrison;   // edificio: quién hay dentro

    bool is_converted() const { return original_player != current_player; }
    bool is_complete() const { return !construction.has_value(); }
};

}  // namespace sim
