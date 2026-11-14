#pragma once

#include <span>

#include "core/fixed.hpp"
#include "data/obj_sheet.hpp"

namespace sim {

// Función pura (architecture.md §4): stat efectivo = f(base, modificadores).
// Orden fijo, independiente del orden en que se investigaron las tecnologías:
//   1. base + Σ ADD_FLAT
//   2. × (1 + Σ ADD_PERCENT / 100)
//   3. × Π MULTIPLY_FLAT
//   4. SET_VALUE de mayor prioridad (desempate: mayor id) sustituye todo
core::Fixed resolve_stat(core::Fixed base, data::StatTag stat, uint8_t sub, std::span<const data::Modifier> mods,
                         data::TypeId type, const data::ClassSet& classes);

// Ficha efectiva de un tipo para un jugador con esos modificadores activos.
data::EntityStats compute_effective(const data::ObjSheet& sheet, std::span<const data::Modifier> mods);

}  // namespace sim
