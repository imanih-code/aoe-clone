#pragma once

#include <cstdint>
#include <vector>

#include "core/entity_handle.hpp"
#include "core/fixed.hpp"
#include "data/ids.hpp"

namespace sim {

enum class CommandType : uint8_t {
    Move,           // actors → target_pos
    Stop,           // actors
    Gather,         // actors → target (recurso)
    Build,          // actors → target (obra propia)
    PlaceBuilding,  // actors (constructores), type_arg, tile
    Train,          // actors (edificios), type_arg
    CancelTrain,    // actors[0] (edificio), index
    SetRally,       // actors (edificios) → target_pos
    Attack,         // actors → target (enemigo)
    Research,       // actors[0] (edificio), index = TechId
};

// Única forma de cambiar la simulación desde fuera (jugador o IA).
// Grabar seed + Commands = replay completo (architecture.md, Diagrama 7).
struct Command {
    uint32_t tick = 0;
    data::PlayerId issuer = data::kGaia;
    CommandType type = CommandType::Move;
    std::vector<core::EntityHandle> actors;
    core::Vec2F target_pos;
    core::EntityHandle target;
    data::TypeId type_arg;
    core::TilePos tile;
    int32_t index = 0;
};

}  // namespace sim
