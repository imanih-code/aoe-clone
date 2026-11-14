#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/entity_handle.hpp"
#include "core/fixed.hpp"
#include "data/ids.hpp"

namespace sim {

struct ResourceNodeData {
    data::ResourceTag resource = data::ResourceTag::Wood;
    int32_t amount_remaining = 0;
    int32_t amount_max = 0;
};

// Orden de movimiento en curso. En la Fase 3 se generaliza a IActionBehavior
// (mutability.md); de momento mover es la única acción.
struct MoveOrder {
    std::vector<core::Vec2F> waypoints;  // el último es el destino exacto
    std::size_t next = 0;

    bool done() const { return next >= waypoints.size(); }
    core::Vec2F current() const { return waypoints[next]; }
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

    std::optional<ResourceNodeData> resource_node;
    std::optional<MoveOrder> move;

    bool is_converted() const { return original_player != current_player; }
};

}  // namespace sim
