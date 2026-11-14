#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/entity_handle.hpp"
#include "data/ids.hpp"
#include "data/obj_sheet.hpp"

namespace sim {

struct PlayerState {
    data::PlayerId id = data::kGaia;
    std::string name;
    uint32_t color = 0xffffffffu;  // RGBA, solo para el render
    bool active = false;
    bool is_defeated = false;
    // PlayerStates viven toda la partida aunque el jugador pierda (architecture.md §3.3)

    // ─── Civilización y progreso ─────────────────────────────────────────
    const data::CivTemplate* civ = nullptr;  // read-only, estable toda la partida
    uint8_t age = 0;
    std::vector<bool> researched;            // por TechId
    std::vector<data::Modifier> modifiers;   // bonus de civ + tecnologías, en orden de aplicación
    uint32_t modifier_epoch = 0;
    std::map<data::TypeId, data::TypeId> upgraded;  // líneas de mejora: from → to (ya resuelto al final de la cadena)
    // Caché de stats efectivos por TypeId (se vacía al cambiar los modificadores)
    mutable std::vector<std::optional<data::EntityStats>> stats_cache;

    // ─── Economía ────────────────────────────────────────────────────────
    data::ResourceArray resources{};
    int32_t population_current = 0;
    int32_t population_cap = 0;

    // ─── Entidades vivas ─────────────────────────────────────────────────
    std::vector<core::EntityHandle> units;
    std::vector<core::EntityHandle> buildings;

    // ─── Estadísticas (architecture.md §3.4) ─────────────────────────────
    std::map<data::TypeId, uint32_t> kills_by_type;
    std::map<data::TypeId, uint32_t> losses_by_type;

    data::TypeId resolve(data::TypeId t) const {
        auto it = upgraded.find(t);
        return it == upgraded.end() ? t : it->second;
    }
    bool has_researched(data::TechId t) const { return t < researched.size() && researched[t]; }
};

}  // namespace sim
