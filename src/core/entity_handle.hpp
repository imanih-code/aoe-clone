#pragma once

#include <cstdint>
#include <limits>

namespace core {

// Referencia segura a una entidad: índice en el SlotMap + generación del hueco.
// Si la entidad muere y el hueco se reutiliza, la generación ya no coincide
// y SlotMap::get() devuelve nullptr en vez de otra entidad (ver architecture.md §2).
struct EntityHandle {
    uint32_t index = std::numeric_limits<uint32_t>::max();
    uint32_t generation = 0;

    bool operator==(const EntityHandle&) const = default;
    explicit operator bool() const { return index != std::numeric_limits<uint32_t>::max(); }
};

}  // namespace core
