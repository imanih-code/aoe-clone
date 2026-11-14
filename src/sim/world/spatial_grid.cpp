#include "sim/world/spatial_grid.hpp"

#include <algorithm>

namespace sim {

SpatialGrid::SpatialGrid(int map_width, int map_height, int cell_tiles)
    : cell_tiles_(cell_tiles),
      cols_((map_width + cell_tiles - 1) / cell_tiles),
      rows_((map_height + cell_tiles - 1) / cell_tiles),
      cells_(static_cast<std::size_t>(cols_ * rows_)) {}

int SpatialGrid::clamp_cx(int tile_x) const { return std::clamp(tile_x / cell_tiles_, 0, cols_ - 1); }
int SpatialGrid::clamp_cy(int tile_y) const { return std::clamp(tile_y / cell_tiles_, 0, rows_ - 1); }

int SpatialGrid::cell_of(core::Vec2F p) const {
    const int tx = std::max(0, p.x.floor_int());
    const int ty = std::max(0, p.y.floor_int());
    return clamp_cy(ty) * cols_ + clamp_cx(tx);
}

void SpatialGrid::insert(core::EntityHandle e, core::Vec2F pos) { cells_[cell_of(pos)].push_back(e); }

void SpatialGrid::remove(core::EntityHandle e, core::Vec2F pos) {
    auto& cell = cells_[cell_of(pos)];
    auto it = std::find(cell.begin(), cell.end(), e);
    if (it != cell.end()) cell.erase(it);
}

void SpatialGrid::move(core::EntityHandle e, core::Vec2F from, core::Vec2F to) {
    const int a = cell_of(from);
    const int b = cell_of(to);
    if (a == b) return;
    auto& cell = cells_[a];
    auto it = std::find(cell.begin(), cell.end(), e);
    if (it != cell.end()) cell.erase(it);
    cells_[b].push_back(e);
}

void SpatialGrid::query_rect(core::Vec2F min, core::Vec2F max, std::vector<core::EntityHandle>& out) const {
    const int cx0 = clamp_cx(std::max(0, min.x.floor_int()));
    const int cy0 = clamp_cy(std::max(0, min.y.floor_int()));
    const int cx1 = clamp_cx(std::max(0, max.x.floor_int()));
    const int cy1 = clamp_cy(std::max(0, max.y.floor_int()));
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const auto& cell = cells_[cy * cols_ + cx];
            out.insert(out.end(), cell.begin(), cell.end());
        }
    }
}

}  // namespace sim
