#include "sim/world/visibility_map.hpp"

#include <algorithm>

namespace sim {

void VisibilityMap::reset(int width, int height) {
    width_ = width;
    height_ = height;
    visible_.assign(static_cast<std::size_t>(width * height), 0);
    explored_.assign(static_cast<std::size_t>(width * height), 0);
    ++version_;
}

void VisibilityMap::begin_update() { std::fill(visible_.begin(), visible_.end(), 0); }

void VisibilityMap::reveal_circle(core::TilePos c, int radius) {
    const int r2 = radius * radius + radius;  // + radius: borde más redondo
    const int y0 = std::max(0, c.y - radius), y1 = std::min(height_ - 1, c.y + radius);
    const int x0 = std::max(0, c.x - radius), x1 = std::min(width_ - 1, c.x + radius);
    for (int y = y0; y <= y1; ++y) {
        const int dy = y - c.y;
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - c.x;
            if (dx * dx + dy * dy > r2) continue;
            const int i = index(x, y);
            visible_[i] = 1;
            explored_[i] = 1;
        }
    }
}

}  // namespace sim
