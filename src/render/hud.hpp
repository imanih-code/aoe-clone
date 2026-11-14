#pragma once

#include <optional>
#include <span>
#include <string>

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderWindow.hpp>

#include "core/entity_handle.hpp"
#include "render/entity_renderer.hpp"
#include "sim/game/game_state.hpp"

namespace render {

struct HudInfo {
    data::PlayerId local_player = 1;
    std::span<const core::EntityHandle> selection;
    std::optional<core::TilePos> hovered_tile;
    float fps = 0.f;
    std::optional<sf::FloatRect> drag_rect;  // en píxeles
    std::string mode;                        // "REPLAY", "ESPECTADOR", "PAUSA"...
    int speed = 1;
    float minimap_top = 0.f;                 // el texto de la casilla va encima del minimapa
};

class Hud {
public:
    explicit Hud(const sf::Font& font) : font_(font) {}
    // Dibuja en la vista por defecto de la ventana (píxeles).
    void draw(sf::RenderWindow& window, const sim::GameState& game, const HudInfo& info, const ViewOptions& options) const;

private:
    const sf::Font& font_;
};

}  // namespace render
