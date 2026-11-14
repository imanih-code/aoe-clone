#pragma once

#include <optional>
#include <vector>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

#include "core/entity_handle.hpp"
#include "render/camera.hpp"
#include "render/entity_renderer.hpp"
#include "sim/commands/command.hpp"
#include "sim/game/game_state.hpp"

namespace input {

// Traduce ratón y teclado en selección (presentación) y Commands (simulación).
// Nunca toca la simulación directamente (architecture.md §10.3).
class InputController {
public:
    explicit InputController(data::PlayerId local_player) : local_(local_player) {}

    void handle_event(const sf::Event& event, sf::RenderWindow& window, render::Camera& camera,
                      const sim::GameState& game, const render::EntityRenderer& entities, render::ViewOptions& options);
    void update(float dt, const sim::GameState& game);

    std::vector<sim::Command> take_commands(uint32_t tick);

    // Usados también por el modo --demo
    void select(std::vector<core::EntityHandle> handles) { selection_ = std::move(handles); }
    void order_move_world(core::Vec2F target, const sim::GameState& game);

    const std::vector<core::EntityHandle>& selection() const { return selection_; }
    const std::vector<render::MoveMarker>& markers() const { return markers_; }
    std::optional<sf::FloatRect> drag_rect() const;
    data::PlayerId local_player() const { return local_; }

private:
    void click_select(sf::Vector2i pixel, bool add, const sf::RenderWindow& window, const render::Camera& camera,
                      const sim::GameState& game, const render::EntityRenderer& entities);
    void box_select(sf::Vector2i a, sf::Vector2i b, bool add, const sf::RenderWindow& window, const render::Camera& camera,
                    const sim::GameState& game, const render::EntityRenderer& entities);
    void select_same_type_on_screen(core::EntityHandle like, const sim::GameState& game, const render::EntityRenderer& entities);
    void order_move(sf::Vector2i pixel, const sf::RenderWindow& window, const render::Camera& camera, const sim::GameState& game);
    std::vector<core::EntityHandle> own_movable(const sim::GameState& game) const;

    data::PlayerId local_;
    std::vector<core::EntityHandle> selection_;
    std::vector<sim::Command> pending_;
    std::vector<render::MoveMarker> markers_;

    std::optional<sf::Vector2i> drag_start_;
    sf::Vector2i mouse_{0, 0};
    std::optional<sf::Vector2i> pan_last_;
    sf::Clock last_click_clock_;
    sf::Vector2i last_click_pos_{-1000, -1000};
};

}  // namespace input
