#pragma once

#include <cstdint>
#include <vector>

#include "core/entity_handle.hpp"
#include "core/fixed.hpp"
#include "data/ids.hpp"

namespace sim {

enum class CommandType : uint8_t { Move, Stop };

// Única forma de cambiar la simulación desde fuera (jugador o IA).
// Grabar seed + Commands = replay completo (architecture.md, Diagrama 7).
struct Command {
    uint32_t tick = 0;
    data::PlayerId issuer = data::kGaia;
    CommandType type = CommandType::Move;
    std::vector<core::EntityHandle> actors;
    core::Vec2F target_pos;
    core::EntityHandle target;
};

}  // namespace sim
