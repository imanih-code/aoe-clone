#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include "render/camera.hpp"
#include "sim/game/game_state.hpp"

namespace render {

// Minimapa isométrico abajo a la derecha: terreno, niebla, entidades y el encuadre de la cámara.
// Clic o arrastre sobre él → mueve la cámara.
class Minimap {
public:
    void build(const sim::TileMap& map);
    // viewer vacío = sin niebla (espectador)
    void draw(sf::RenderWindow& window, const sim::GameState& game, std::optional<data::PlayerId> viewer, const Camera& camera);

    bool contains(sf::Vector2f pixel) const { return rect_.contains(pixel); }
    sf::Vector2f pixel_to_world(sf::Vector2f pixel) const;
    const sf::FloatRect& rect() const { return rect_; }

private:
    sf::Vector2f to_pixel(float wx, float wy) const;
    void layout(sf::Vector2u window_size);

    int map_w_ = 0;
    int map_h_ = 0;
    float k_ = 1.f;
    sf::FloatRect rect_;
    sf::VertexArray terrain_{sf::PrimitiveType::Triangles};  // en coordenadas locales (0,0 = esquina del minimapa)
    sf::VertexArray fog_{sf::PrimitiveType::Triangles};
    uint32_t fog_version_ = std::numeric_limits<uint32_t>::max();
    const sim::VisibilityMap* fog_source_ = nullptr;
};

}  // namespace render
