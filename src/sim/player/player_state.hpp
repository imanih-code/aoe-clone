#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/entity_handle.hpp"
#include "data/ids.hpp"

namespace sim {

struct PlayerState {
    data::PlayerId id = data::kGaia;
    std::string name;
    uint32_t color = 0xffffffffu;  // RGBA, solo para el render
    bool active = false;
    bool is_defeated = false;

    data::ResourceArray resources{};
    int32_t population_current = 0;
    int32_t population_cap = 0;

    std::vector<core::EntityHandle> units;
    std::vector<core::EntityHandle> buildings;
};

}  // namespace sim
