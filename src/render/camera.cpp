#include "render/camera.hpp"

#include <algorithm>

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include "render/iso.hpp"

namespace render {

namespace {
constexpr float kMinZoom = 0.4f;
constexpr float kMaxZoom = 3.0f;
constexpr float kPanSpeed = 900.f;  // píxeles de pantalla por segundo
constexpr int kEdge = 12;           // píxeles del borde que activan el desplazamiento
}  // namespace

Camera::Camera(sf::Vector2u window_size, int map_width, int map_height)
    : window_size_(static_cast<float>(window_size.x), static_cast<float>(window_size.y)),
      map_w_(map_width),
      map_h_(map_height) {
    apply_size();
    center_on_world({map_width * 0.5f, map_height * 0.5f});
}

void Camera::apply_size() { view_.setSize(window_size_ * zoom_); }

void Camera::on_resize(sf::Vector2u window_size) {
    window_size_ = {static_cast<float>(window_size.x), static_cast<float>(window_size.y)};
    apply_size();
    clamp();
}

void Camera::clamp() {
    // Caja que contiene el rombo del mapa en coordenadas isométricas
    const float min_x = -map_h_ * kTileW * 0.5f;
    const float max_x = map_w_ * kTileW * 0.5f;
    const float min_y = 0.f;
    const float max_y = (map_w_ + map_h_) * kTileH * 0.5f;
    const sf::Vector2f c = view_.getCenter();
    view_.setCenter({std::clamp(c.x, min_x, max_x), std::clamp(c.y, min_y, max_y)});
}

void Camera::center_on_world(sf::Vector2f world_tiles) {
    view_.setCenter(world_to_iso(world_tiles.x, world_tiles.y));
    clamp();
}

void Camera::pan_pixels(sf::Vector2f delta, sf::Vector2u window_size) {
    const float scale = view_.getSize().x / static_cast<float>(std::max(1u, window_size.x));
    view_.move(delta * scale);
    clamp();
}

void Camera::zoom_at(float factor, sf::Vector2i pixel, const sf::RenderWindow& window) {
    const sf::Vector2f before = window.mapPixelToCoords(pixel, view_);
    zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    apply_size();
    const sf::Vector2f after = window.mapPixelToCoords(pixel, view_);
    view_.move(before - after);
    clamp();
}

void Camera::update(float dt, const sf::RenderWindow& window) {
    if (!window.hasFocus()) return;
    sf::Vector2f dir{0.f, 0.f};
    using K = sf::Keyboard::Key;
    if (sf::Keyboard::isKeyPressed(K::Left)) dir.x -= 1.f;
    if (sf::Keyboard::isKeyPressed(K::Right)) dir.x += 1.f;
    if (sf::Keyboard::isKeyPressed(K::Up)) dir.y -= 1.f;
    if (sf::Keyboard::isKeyPressed(K::Down)) dir.y += 1.f;

    const sf::Vector2i m = sf::Mouse::getPosition(window);
    const sf::Vector2u size = window.getSize();
    const bool inside = m.x >= 0 && m.y >= 0 && m.x < static_cast<int>(size.x) && m.y < static_cast<int>(size.y);
    if (inside) {
        if (m.x < kEdge) dir.x -= 1.f;
        if (m.x >= static_cast<int>(size.x) - kEdge) dir.x += 1.f;
        if (m.y < kEdge) dir.y -= 1.f;
        if (m.y >= static_cast<int>(size.y) - kEdge) dir.y += 1.f;
    }
    if (dir.x != 0.f || dir.y != 0.f) pan_pixels(dir * (kPanSpeed * dt), size);
}

sf::FloatRect Camera::visible_rect() const {
    const sf::Vector2f size = view_.getSize();
    return sf::FloatRect(view_.getCenter() - size * 0.5f, size);
}

}  // namespace render
