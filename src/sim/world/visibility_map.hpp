#pragma once

#include <cstdint>
#include <vector>

#include "core/fixed.hpp"

namespace sim {

// Niebla de guerra de un jugador (architecture.md §11.1).
// - visible: lo ven ahora sus unidades y edificios
// - explored: lo han visto alguna vez (el terreno y los edificios quedan en el recuerdo)
// Vive en la simulación: la IA y la validación de órdenes la respetan; el render solo la pinta.
class VisibilityMap {
public:
    void reset(int width, int height);

    void begin_update();                                   // borra "visible", conserva "explored"
    void reveal_circle(core::TilePos center, int radius);  // marca visible + explorado
    void end_update() { ++version_; }

    bool visible(int x, int y) const { return in_bounds(x, y) && visible_[index(x, y)] != 0; }
    bool explored(int x, int y) const { return in_bounds(x, y) && explored_[index(x, y)] != 0; }
    bool visible(core::TilePos t) const { return visible(t.x, t.y); }
    bool explored(core::TilePos t) const { return explored(t.x, t.y); }

    int width() const { return width_; }
    int height() const { return height_; }
    uint32_t version() const { return version_; }  // cambia en cada actualización (el render reconstruye)

private:
    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    int index(int x, int y) const { return y * width_ + x; }

    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> visible_;
    std::vector<uint8_t> explored_;
    uint32_t version_ = 0;
};

}  // namespace sim
