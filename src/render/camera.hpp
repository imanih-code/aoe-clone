#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/View.hpp>

namespace render {

class Camera {
public:
    Camera(sf::Vector2u window_size, int map_width, int map_height);

    void on_resize(sf::Vector2u window_size);
    void update(float dt, const sf::RenderWindow& window);  // teclado y bordes de pantalla
    void pan_pixels(sf::Vector2f delta, sf::Vector2u window_size);
    void zoom_at(float factor, sf::Vector2i pixel, const sf::RenderWindow& window);
    void center_on_world(sf::Vector2f world_tiles);

    const sf::View& view() const { return view_; }
    float zoom() const { return zoom_; }

    // Rectángulo visible en coordenadas isométricas
    sf::FloatRect visible_rect() const;

private:
    void apply_size();
    void clamp();

    sf::View view_;
    sf::Vector2f window_size_;
    float zoom_ = 1.f;
    int map_w_;
    int map_h_;
};

}  // namespace render
