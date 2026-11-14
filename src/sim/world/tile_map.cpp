#include "sim/world/tile_map.hpp"

namespace sim {

TileMap::TileMap(int width, int height)
    : width_(width),
      height_(height),
      terrain_(static_cast<std::size_t>(width * height), Terrain::Grass),
      blockers_(static_cast<std::size_t>(width * height), 0) {}

bool TileMap::is_area_free(int x, int y, int w, int h) const {
    for (int ty = y; ty < y + h; ++ty) {
        for (int tx = x; tx < x + w; ++tx) {
            if (!is_passable(tx, ty)) return false;
        }
    }
    return true;
}

void TileMap::add_blocker(int x, int y, int w, int h) {
    for (int ty = y; ty < y + h; ++ty) {
        for (int tx = x; tx < x + w; ++tx) {
            if (in_bounds(tx, ty)) ++blockers_[index(tx, ty)];
        }
    }
}

void TileMap::remove_blocker(int x, int y, int w, int h) {
    for (int ty = y; ty < y + h; ++ty) {
        for (int tx = x; tx < x + w; ++tx) {
            if (in_bounds(tx, ty) && blockers_[index(tx, ty)] > 0) --blockers_[index(tx, ty)];
        }
    }
}

}  // namespace sim
