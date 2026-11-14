#pragma once

#include <cstdint>
#include <limits>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include "sim/world/visibility_map.hpp"

namespace render {

// Pinta la niebla de guerra encima del mundo: negro = sin explorar, oscurecido = explorado
// pero sin visión. La opacidad se interpola por esquina de tile para que el borde sea suave.
class FogRenderer {
public:
    void update(const sim::VisibilityMap& fog);  // solo reconstruye si cambió la versión
    void draw(sf::RenderTarget& target) const { target.draw(vertices_); }

private:
    sf::VertexArray vertices_{sf::PrimitiveType::Triangles};
    uint32_t version_ = std::numeric_limits<uint32_t>::max();
    const sim::VisibilityMap* source_ = nullptr;
};

}  // namespace render
