#pragma once

#include <vector>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include "sim/world/tile_map.hpp"

namespace render {

// Terreno por chunks de 16x16 tiles: una draw call por chunk visible.
class MapRenderer {
public:
    void build(const sim::TileMap& map);
    void draw(sf::RenderTarget& target, const sf::FloatRect& visible, bool grid) const;

private:
    struct Chunk {
        sf::VertexArray tiles{sf::PrimitiveType::Triangles};
        sf::VertexArray grid{sf::PrimitiveType::Lines};
        sf::FloatRect bounds;
    };
    std::vector<Chunk> chunks_;
};

}  // namespace render
