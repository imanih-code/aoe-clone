#include "render/minimap.hpp"

#include <algorithm>

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/Transform.hpp>

#include "render/iso.hpp"
#include "render/text.hpp"

namespace render {

namespace {

constexpr float kWidth = 230.f;  // ancho del rombo; alto = la mitad (proyección 2:1)
constexpr float kMargin = 10.f;

sf::Color terrain_color(sim::Terrain t) {
    switch (t) {
    case sim::Terrain::Grass: return {86, 138, 62};
    case sim::Terrain::Dirt: return {138, 116, 74};
    case sim::Terrain::Sand: return {196, 178, 120};
    case sim::Terrain::Water: return {48, 96, 160};
    }
    return sf::Color::Magenta;
}

void append_tile(sf::VertexArray& va, sf::Vector2f top, sf::Vector2f right, sf::Vector2f bottom, sf::Vector2f left, sf::Color c) {
    va.append({top, c});
    va.append({right, c});
    va.append({bottom, c});
    va.append({top, c});
    va.append({bottom, c});
    va.append({left, c});
}

}  // namespace

sf::Vector2f Minimap::to_pixel(float wx, float wy) const {
    // Igual que world_to_iso pero escalado: x ∈ [-h, w] → [0, ancho], y ∈ [0, w + h] → [0, alto]
    return {(wx - wy + static_cast<float>(map_h_)) * k_, (wx + wy) * k_ * 0.5f};
}

sf::Vector2f Minimap::pixel_to_world(sf::Vector2f pixel) const {
    const float a = (pixel.x - rect_.position.x) / k_ - static_cast<float>(map_h_);  // wx - wy
    const float b = (pixel.y - rect_.position.y) / (k_ * 0.5f);                      // wx + wy
    return {(a + b) * 0.5f, (b - a) * 0.5f};
}

void Minimap::build(const sim::TileMap& map) {
    map_w_ = map.width();
    map_h_ = map.height();
    k_ = kWidth / static_cast<float>(map_w_ + map_h_);
    terrain_.clear();
    for (int y = 0; y < map_h_; ++y) {
        for (int x = 0; x < map_w_; ++x) {
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            append_tile(terrain_, to_pixel(fx, fy), to_pixel(fx + 1, fy), to_pixel(fx + 1, fy + 1), to_pixel(fx, fy + 1),
                        terrain_color(map.terrain(x, y)));
        }
    }
}

void Minimap::layout(sf::Vector2u window_size) {
    const sf::Vector2f size{kWidth, kWidth * 0.5f};
    rect_ = sf::FloatRect({static_cast<float>(window_size.x) - size.x - kMargin, static_cast<float>(window_size.y) - size.y - kMargin}, size);
}

void Minimap::draw(sf::RenderWindow& window, const sim::GameState& game, std::optional<data::PlayerId> viewer, const Camera& camera) {
    layout(window.getSize());
    sf::RectangleShape bg(rect_.size + sf::Vector2f{8.f, 8.f});
    bg.setPosition(rect_.position - sf::Vector2f{4.f, 4.f});
    bg.setFillColor(sf::Color(20, 16, 12, 230));
    bg.setOutlineColor(sf::Color(120, 100, 60));
    bg.setOutlineThickness(1.f);
    window.draw(bg);

    sf::RenderStates states;
    states.transform.translate(rect_.position);
    window.draw(terrain_, states);

    // ─── Entidades (las que el espectador puede ver) ─────────────────────
    sf::VertexArray dots(sf::PrimitiveType::Triangles);
    auto dot = [&](sf::Vector2f p, float r, sf::Color c) {
        append_tile(dots, p + sf::Vector2f{0.f, -r}, p + sf::Vector2f{r, 0.f}, p + sf::Vector2f{0.f, r}, p + sf::Vector2f{-r, 0.f}, c);
    };
    game.entities().for_each([&](core::EntityHandle, const sim::SpawnableEntity& e) {
        const auto& s = game.sheet_of(e);
        if (s.is_projectile() || s.visual.shape == data::ShapeKind::Tree || e.garrisoned_in) return;
        if (viewer && !(s.is_unit() ? game.is_visible_to(*viewer, e) : game.is_known_to(*viewer, e))) return;
        sf::Color c = e.current_player == data::kGaia ? rgba(s.visual.color) : rgba(game.player(e.current_player).color);
        if (e.current_player != data::kGaia) c = mix(c, sf::Color::White, 0.15f);
        dot(to_pixel(e.position.x.to_float(), e.position.y.to_float()), s.is_building() ? 2.6f : 1.6f, c);
    });
    window.draw(dots, states);

    // ─── Niebla ──────────────────────────────────────────────────────────
    if (viewer) {
        const sim::VisibilityMap& fog = game.visibility(*viewer);
        if (&fog != fog_source_ || fog.version() != fog_version_) {
            fog_source_ = &fog;
            fog_version_ = fog.version();
            fog_.clear();
            for (int y = 0; y < map_h_; ++y) {
                for (int x = 0; x < map_w_; ++x) {
                    if (fog.visible(x, y)) continue;
                    const sf::Color c(0, 0, 0, fog.explored(x, y) ? 120 : 255);
                    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
                    append_tile(fog_, to_pixel(fx, fy), to_pixel(fx + 1, fy), to_pixel(fx + 1, fy + 1), to_pixel(fx, fy + 1), c);
                }
            }
        }
        window.draw(fog_, states);
    }

    // ─── Encuadre de la cámara ───────────────────────────────────────────
    const sf::FloatRect view = camera.visible_rect();
    const sf::Vector2f corners_iso[4] = {view.position, view.position + sf::Vector2f{view.size.x, 0.f}, view.position + view.size,
                                         view.position + sf::Vector2f{0.f, view.size.y}};
    sf::VertexArray frame(sf::PrimitiveType::LineStrip);
    for (int i = 0; i <= 4; ++i) {
        const sf::Vector2f w = iso_to_world(corners_iso[i % 4]);
        sf::Vector2f p = to_pixel(w.x, w.y) + rect_.position;
        p.x = std::clamp(p.x, rect_.position.x, rect_.position.x + rect_.size.x);
        p.y = std::clamp(p.y, rect_.position.y, rect_.position.y + rect_.size.y);
        frame.append({p, sf::Color(255, 255, 255, 220)});
    }
    window.draw(frame);
}

}  // namespace render
