#pragma once

#include <cstdint>
#include <vector>

#include "core/fixed.hpp"

namespace sim {

enum class Terrain : uint8_t { Grass, Dirt, Sand, Water };

class TileMap {
public:
    TileMap(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    bool in_bounds(core::TilePos t) const { return in_bounds(t.x, t.y); }

    Terrain terrain(int x, int y) const { return terrain_[index(x, y)]; }
    void set_terrain(int x, int y, Terrain t) { terrain_[index(x, y)] = t; }

    // Pisable por unidades terrestres: dentro del mapa, no agua y sin edificio/recurso encima.
    bool is_passable(int x, int y) const {
        return in_bounds(x, y) && terrain_[index(x, y)] != Terrain::Water && blockers_[index(x, y)] == 0;
    }
    bool is_passable(core::TilePos t) const { return is_passable(t.x, t.y); }

    // Rectángulo libre para colocar un edificio o recurso.
    bool is_area_free(int x, int y, int w, int h) const;

    void add_blocker(int x, int y, int w, int h);
    void remove_blocker(int x, int y, int w, int h);

private:
    int index(int x, int y) const { return y * width_ + x; }

    int width_;
    int height_;
    std::vector<Terrain> terrain_;
    std::vector<uint8_t> blockers_;
};

}  // namespace sim
