#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/fixed.hpp"
#include "sim/world/tile_map.hpp"

namespace sim {

// A* octil sobre tiles, 8 direcciones, sin cortar esquinas.
// Reutiliza sus buffers entre búsquedas (sin asignar memoria cada vez).
class Pathfinder {
public:
    struct Result {
        std::vector<core::TilePos> tiles;  // sin incluir el tile de inicio
        core::TilePos goal;                // destino real (puede diferir si el pedido estaba bloqueado)
        bool reached_goal = false;         // false: camino al punto alcanzable más cercano
    };

    // nullopt si no hay ningún tile pisable cerca del destino.
    std::optional<Result> find(const TileMap& map, core::TilePos start, core::TilePos goal, int max_expansions = 40000);

    // Tile pisable más cercano a `t` (búsqueda en anchura, radio limitado).
    static std::optional<core::TilePos> nearest_passable(const TileMap& map, core::TilePos t, int max_radius = 16);

private:
    void prepare(const TileMap& map);

    int width_ = 0;
    int height_ = 0;
    uint32_t stamp_ = 0;
    std::vector<uint32_t> seen_;    // == stamp_ → visitado en esta búsqueda
    std::vector<uint32_t> closed_;  // == stamp_ → cerrado en esta búsqueda
    std::vector<int32_t> g_;
    std::vector<int32_t> parent_;
};

// Comprueba en pasos de 1/4 de tile que la línea a→b solo cruza tiles pisables.
bool line_is_clear(const TileMap& map, core::Vec2F a, core::Vec2F b);

}  // namespace sim
