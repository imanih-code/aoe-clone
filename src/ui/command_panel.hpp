#pragma once

#include <span>
#include <string>
#include <vector>

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Keyboard.hpp>

#include "core/entity_handle.hpp"
#include "data/ids.hpp"
#include "sim/game/game_state.hpp"

namespace ui {

enum class ButtonKind { Build, Train, Research, Stop, CancelQueue, UseSkill, Ungarrison };

struct Button {
    ButtonKind kind = ButtonKind::Stop;
    data::TypeId type;
    int index = 0;  // posición en la cuadrícula; CancelQueue: posición en la cola
    data::TechId tech = 0;  // Research
    bool is_tech = false;   // color distinto para tecnologías
    int skill = 0;          // UseSkill: posición de la habilidad en la unidad
    sf::FloatRect rect;
    sf::Keyboard::Key hotkey = sf::Keyboard::Key::Unknown;
    char hotkey_char = 0;
    std::string label;    // UTF-8
    std::string tooltip;  // UTF-8, varias líneas
    bool enabled = true;
    float progress = -1.f;  // cola: progreso del primero (0..1)
};

// Panel de órdenes: se genera cada frame a partir de la selección y de los datos
// (lo que construye un aldeano, lo que entrena un edificio). No hay botones fijos en el código.
class CommandPanel {
public:
    void rebuild(const sim::GameState& game, data::PlayerId local, std::span<const core::EntityHandle> selection,
                 sf::Vector2u window_size);

    const std::vector<Button>& buttons() const { return buttons_; }
    const Button* hit(sf::Vector2f pixel) const;
    const Button* by_key(sf::Keyboard::Key key) const;
    // Zona que no debe recibir clics del mundo
    bool contains(sf::Vector2f pixel) const;

    void draw(sf::RenderWindow& window, const sf::Font& font, sf::Vector2f mouse) const;

private:
    std::vector<Button> buttons_;
    sf::FloatRect grid_area_;
    sf::FloatRect queue_area_;
    bool visible_ = false;
    bool has_queue_ = false;
};

// "50 comida · 25 madera"
std::string cost_text(const data::ResourceArray& cost);

}  // namespace ui
