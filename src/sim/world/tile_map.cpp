#include "sim/world/tile_map.hpp"

namespace sim {

TileMap::TileMap(int width, int height)
    : width_(width),
      height_(height),
      terrain_(static_cast<std::size_t>(width * height), Terrain::Grass),
      blockers_(static_cast<std::size_t>(width * height), 0),
      reserved_(static_cast<std::size_t>(width * height), 0) {}

bool TileMap::is_area_free(int x, int y, int w, int h, Placement rule) const {
    bool touches_land = false;
    for (int ty = y; ty < y + h; ++ty) {
        for (int tx = x; tx < x + w; ++tx) {
            if (!in_bounds(tx, ty)) return false;
            const int i = index(tx, ty);
            if (blockers_[i] != 0 || reserved_[i] != 0) return false;
            const bool water = terrain_[i] == Terrain::Water;
            if (rule == Placement::Land ? water : !water) return false;
        }
    }
    if (rule != Placement::Shore) return true;
    // Muelle: al menos un tile de tierra pegado al rectángulo (por donde llegan los aldeanos)
    for (int ty = y - 1; ty <= y + h && !touches_land; ++ty) {
        for (int tx = x - 1; tx <= x + w && !touches_land; ++tx) {
            const bool border = tx == x - 1 || tx == x + w || ty == y - 1 || ty == y + h;
            touches_land = border && in_bounds(tx, ty) && terrain_[index(tx, ty)] != Terrain::Water;
        }
    }
    return touches_land;
}

void TileMap::add_blocker(int x, int y, int w, int h, bool blocks_movement) {
    auto& layer = blocks_movement ? blockers_ : reserved_;
    for (int ty = y; ty < y + h; ++ty) {
        for (int tx = x; tx < x + w; ++tx) {
            if (in_bounds(tx, ty)) ++layer[index(tx, ty)];
        }
    }
}

void TileMap::remove_blocker(int x, int y, int w, int h, bool blocks_movement) {
    auto& layer = blocks_movement ? blockers_ : reserved_;
    for (int ty = y; ty < y + h; ++ty) {
        for (int tx = x; tx < x + w; ++tx) {
            if (in_bounds(tx, ty) && layer[index(tx, ty)] > 0) --layer[index(tx, ty)];
        }
    }
}

}  // namespace sim
