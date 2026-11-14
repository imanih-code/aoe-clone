#pragma once

#include <optional>
#include <span>

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
