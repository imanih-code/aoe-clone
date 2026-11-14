#include "render/map_renderer.hpp"

#include <algorithm>
#include <cstdint>

#include "render/iso.hpp"

namespace render {

namespace {

constexpr int kChunk = 16;

sf::Color terrain_color(sim::Terrain t) {
    switch (t) {
    case sim::Terrain::Grass: return {86, 138, 62};
    case sim::Terrain::Dirt: return {138, 116, 74};
    case sim::Terrain::Sand: return {196, 178, 120};
    case sim::Terrain::Water: return {48, 96, 160};
    }
    return sf::Color::Magenta;
}

// Variación de color estable por tile para que se lea la cuadrícula sin líneas
int tile_noise(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return static_cast<int>((h >> 16u) % 17u) - 8;
}

sf::Color vary(sf::Color c, int d) {
    auto ch = [d](std::uint8_t v) { return static_cast<std::uint8_t>(std::clamp(static_cast<int>(v) + d, 0, 255)); };
    return {ch(c.r), ch(c.g), ch(c.b)};
}

}  // namespace

void MapRenderer::build(const sim::TileMap& map) {
    chunks_.clear();
    const sf::Color grid_color(0, 0, 0, 30);
    for (int cy = 0; cy < map.height(); cy += kChunk) {
        for (int cx = 0; cx < map.width(); cx += kChunk) {
            Chunk chunk;
            float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
            for (int y = cy; y < std::min(cy + kChunk, map.height()); ++y) {
                for (int x = cx; x < std::min(cx + kChunk, map.width()); ++x) {
                    const sf::Vector2f top = world_to_iso(static_cast<float>(x), static_cast<float>(y));
                    const sf::Vector2f right = world_to_iso(static_cast<float>(x + 1), static_cast<float>(y));
                    const sf::Vector2f bottom = world_to_iso(static_cast<float>(x + 1), static_cast<float>(y + 1));
                    const sf::Vector2f left = world_to_iso(static_cast<float>(x), static_cast<float>(y + 1));
                    const sf::Color c = vary(terrain_color(map.terrain(x, y)), tile_noise(x, y));

                    chunk.tiles.append({top, c});
                    chunk.tiles.append({right, c});
                    chunk.tiles.append({bottom, c});
                    chunk.tiles.append({top, c});
                    chunk.tiles.append({bottom, c});
                    chunk.tiles.append({left, c});

                    chunk.grid.append({top, grid_color});
                    chunk.grid.append({right, grid_color});
                    chunk.grid.append({top, grid_color});
                    chunk.grid.append({left, grid_color});

                    min_x = std::min(min_x, left.x);
                    max_x = std::max(max_x, right.x);
                    min_y = std::min(min_y, top.y);
                    max_y = std::max(max_y, bottom.y);
                }
            }
            chunk.bounds = sf::FloatRect({min_x, min_y}, {max_x - min_x, max_y - min_y});
            chunks_.push_back(std::move(chunk));
        }
    }
}

void MapRenderer::draw(sf::RenderTarget& target, const sf::FloatRect& visible, bool grid) const {
    for (const Chunk& chunk : chunks_) {
        if (!chunk.bounds.findIntersection(visible)) continue;
        target.draw(chunk.tiles);
        if (grid) target.draw(chunk.grid);
    }
}

}  // namespace render
