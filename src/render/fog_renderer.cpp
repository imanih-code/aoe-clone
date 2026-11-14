#include "render/fog_renderer.hpp"

#include <vector>

#include "render/iso.hpp"

namespace render {

namespace {
constexpr int kUnexplored = 255;
constexpr int kRemembered = 130;
}  // namespace

void FogRenderer::update(const sim::VisibilityMap& fog) {
    if (&fog == source_ && fog.version() == version_) return;
    source_ = &fog;
    version_ = fog.version();

    const int w = fog.width();
    const int h = fog.height();
    auto tile_alpha = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return kUnexplored;
        if (fog.visible(x, y)) return 0;
        return fog.explored(x, y) ? kRemembered : kUnexplored;
    };
    // Opacidad por esquina = media de los 4 tiles que la tocan → bordes suaves
    std::vector<uint8_t> corner(static_cast<std::size_t>((w + 1) * (h + 1)));
    for (int cy = 0; cy <= h; ++cy) {
        for (int cx = 0; cx <= w; ++cx) {
            const int sum = tile_alpha(cx - 1, cy - 1) + tile_alpha(cx, cy - 1) + tile_alpha(cx - 1, cy) + tile_alpha(cx, cy);
            corner[static_cast<std::size_t>(cy * (w + 1) + cx)] = static_cast<uint8_t>(sum / 4);
        }
    }
    auto at = [&](int cx, int cy) { return corner[static_cast<std::size_t>(cy * (w + 1) + cx)]; };

    vertices_.clear();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t a0 = at(x, y), a1 = at(x + 1, y), a2 = at(x + 1, y + 1), a3 = at(x, y + 1);
            if ((a0 | a1 | a2 | a3) == 0) continue;  // totalmente visible: nada que pintar
            const sf::Vertex top{world_to_iso(static_cast<float>(x), static_cast<float>(y)), sf::Color(0, 0, 0, a0)};
            const sf::Vertex right{world_to_iso(static_cast<float>(x + 1), static_cast<float>(y)), sf::Color(0, 0, 0, a1)};
            const sf::Vertex bottom{world_to_iso(static_cast<float>(x + 1), static_cast<float>(y + 1)), sf::Color(0, 0, 0, a2)};
            const sf::Vertex left{world_to_iso(static_cast<float>(x), static_cast<float>(y + 1)), sf::Color(0, 0, 0, a3)};
            vertices_.append(top);
            vertices_.append(right);
            vertices_.append(bottom);
            vertices_.append(top);
            vertices_.append(bottom);
            vertices_.append(left);
        }
    }
}

}  // namespace render
