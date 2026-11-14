#pragma once

#include <SFML/System/Vector2.hpp>

#include "core/fixed.hpp"

namespace render {

// Proyección isométrica 2:1 (architecture.md §10.1). Coordenadas de mundo en tiles.
inline constexpr float kTileW = 64.f;
inline constexpr float kTileH = 32.f;

inline sf::Vector2f world_to_iso(float wx, float wy) {
    return {(wx - wy) * kTileW * 0.5f, (wx + wy) * kTileH * 0.5f};
}

inline sf::Vector2f world_to_iso(core::Vec2F p) { return world_to_iso(p.x.to_float(), p.y.to_float()); }

inline sf::Vector2f iso_to_world(sf::Vector2f p) {
    const float a = p.x / (kTileW * 0.5f);  // wx - wy
    const float b = p.y / (kTileH * 0.5f);  // wx + wy
    return {(a + b) * 0.5f, (b - a) * 0.5f};
}

}  // namespace render
