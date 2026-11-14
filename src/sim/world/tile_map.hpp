#pragma once

#include <cstdint>
#include <vector>

#include "core/fixed.hpp"

namespace sim {

enum class Terrain : uint8_t { Grass, Dirt, Sand, Water };

// Por dónde se mueve una unidad: tierra (casi todas) o agua (barcos)
enum class MoveLayer : uint8_t { Land, Water };

// Dónde se puede colocar algo (edificios, recursos)
enum class Placement : uint8_t {
    Land,   // tierra libre (casi todo)
    Water,  // agua libre (peces)
    Shore,  // agua libre tocando tierra (muelle)
};

class TileMap {
public:
    TileMap(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    bool in_bounds(core::TilePos t) const { return in_bounds(t.x, t.y); }

    Terrain terrain(int x, int y) const { return terrain_[index(x, y)]; }
    bool is_water(int x, int y) const { return in_bounds(x, y) && terrain_[index(x, y)] == Terrain::Water; }
    void set_terrain(int x, int y, Terrain t) { terrain_[index(x, y)] = t; }

    // Transitable para esa capa: dentro del mapa, terreno adecuado y sin nada que bloquee.
    bool is_passable(int x, int y, MoveLayer layer = MoveLayer::Land) const {
        if (!in_bounds(x, y) || blockers_[index(x, y)] != 0) return false;
        const bool water = terrain_[index(x, y)] == Terrain::Water;
        return layer == MoveLayer::Land ? !water : water;
    }
    bool is_passable(core::TilePos t, MoveLayer layer = MoveLayer::Land) const { return is_passable(t.x, t.y, layer); }

    // Rectángulo libre para colocar algo con esa regla de colocación.
    bool is_area_free(int x, int y, int w, int h, Placement rule = Placement::Land) const;

    // blocks_movement = false: ocupa el suelo (no se puede construir encima) pero se puede caminar
    // por él, como una granja.
    void add_blocker(int x, int y, int w, int h, bool blocks_movement = true);
    void remove_blocker(int x, int y, int w, int h, bool blocks_movement = true);

private:
    int index(int x, int y) const { return y * width_ + x; }

    int width_;
    int height_;
    std::vector<Terrain> terrain_;
    std::vector<uint8_t> blockers_;  // bloquean el paso
    std::vector<uint8_t> reserved_;  // solo impiden construir encima
};

}  // namespace sim
