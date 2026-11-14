#include "render/text.hpp"

#include <array>
#include <filesystem>
#include <stdexcept>

namespace render {

sf::String utf8(std::string_view s) { return sf::String::fromUtf8(s.begin(), s.end()); }

sf::Font load_ui_font() {
    const std::array<const char*, 7> candidates = {
        "assets/fonts/ui.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    sf::Font font;
    for (const char* path : candidates) {
        if (std::filesystem::exists(path) && font.openFromFile(path)) return font;
    }
    throw std::runtime_error("No se encontró ninguna fuente. Copia un .ttf en assets/fonts/ui.ttf");
}

}  // namespace render
