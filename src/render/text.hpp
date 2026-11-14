#pragma once

#include <cstdint>
#include <string_view>

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/System/String.hpp>

namespace render {

// Los textos del juego y de data/ son UTF-8 (tildes, eñes).
sf::String utf8(std::string_view s);

// Busca una fuente: assets/fonts/ui.ttf y, si no existe, fuentes del sistema.
// Lanza std::runtime_error si no encuentra ninguna.
sf::Font load_ui_font();

inline sf::Color rgba(uint32_t c) { return sf::Color(c); }

inline sf::Color shade(sf::Color c, float f) {
    auto ch = [f](std::uint8_t v) { return static_cast<std::uint8_t>(std::min(255.f, static_cast<float>(v) * f)); };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

inline sf::Color mix(sf::Color a, sf::Color b, float t) {
    auto ch = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t);
    };
    return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
}

}  // namespace render
