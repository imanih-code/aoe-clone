#pragma once

#include <vector>

#include "core/entity_handle.hpp"
#include "core/fixed.hpp"

namespace sim {

// Índice espacial uniforme alineado a los tiles (architecture.md, Diagrama 6).
// Mover una entidad solo toca celdas si cambia de celda.
class SpatialGrid {
public:
    SpatialGrid(int map_width, int map_height, int cell_tiles);

    void insert(core::EntityHandle e, core::Vec2F pos);
    void remove(core::EntityHandle e, core::Vec2F pos);
    void move(core::EntityHandle e, core::Vec2F from, core::Vec2F to);

    // Añade a `out` las entidades cuyas posiciones caen en celdas que tocan el rectángulo.
    // El llamador filtra con precisión. Orden: por celda y orden de inserción (determinista).
    void query_rect(core::Vec2F min, core::Vec2F max, std::vector<core::EntityHandle>& out) const;

private:
    int cell_of(core::Vec2F p) const;
    int clamp_cx(int tile_x) const;
    int clamp_cy(int tile_y) const;

    int cell_tiles_;
    int cols_;
    int rows_;
    std::vector<std::vector<core::EntityHandle>> cells_;
};

}  // namespace sim
