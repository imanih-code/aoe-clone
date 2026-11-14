#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "core/entity_handle.hpp"
#include "core/fixed.hpp"
#include "data/ids.hpp"
#include "sim/actions/action.hpp"

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

    // ─── Componentes opcionales (architecture.md, Diagrama 5) ────────────
    std::optional<ResourceNodeData> resource_node;
    std::optional<ConstructionData> construction;  // con valor = en obra
    CarryState carry;
    std::deque<ProductionJob> production;
    std::optional<core::Vec2F> rally_point;
    std::optional<ProjectileData> projectile;

    // ─── Acción en curso ─────────────────────────────────────────────────
    std::unique_ptr<IAction> action;

    bool is_converted() const { return original_player != current_player; }
    bool is_complete() const { return !construction.has_value(); }
};

}  // namespace sim
