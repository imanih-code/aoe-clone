#include "input/input_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include "render/iso.hpp"

namespace input {

namespace {
constexpr int kDragThreshold = 6;  // píxeles antes de considerar que es una caja
constexpr float kDoubleClickSeconds = 0.35f;
}  // namespace

std::optional<sf::FloatRect> InputController::drag_rect() const {
    if (!drag_start_) return std::nullopt;
    const sf::Vector2i a = *drag_start_;
    if (std::abs(mouse_.x - a.x) < kDragThreshold && std::abs(mouse_.y - a.y) < kDragThreshold) return std::nullopt;
    const sf::Vector2f p0(static_cast<float>(std::min(a.x, mouse_.x)), static_cast<float>(std::min(a.y, mouse_.y)));
    const sf::Vector2f p1(static_cast<float>(std::max(a.x, mouse_.x)), static_cast<float>(std::max(a.y, mouse_.y)));
    return sf::FloatRect(p0, p1 - p0);
}

std::vector<sim::Command> InputController::take_commands(uint32_t tick) {
    std::vector<sim::Command> out;
    out.swap(pending_);
    for (auto& c : out) c.tick = tick;
    return out;
}

void InputController::update(float dt, const sim::GameState& game) {
    // Quitar de la selección lo que ya no existe
    std::erase_if(selection_, [&](core::EntityHandle h) { return game.entity(h) == nullptr; });
    for (auto& m : markers_) m.age += dt;
    std::erase_if(markers_, [](const render::MoveMarker& m) { return m.age > 0.6f; });
}

std::vector<core::EntityHandle> InputController::own_movable(const sim::GameState& game) const {
    std::vector<core::EntityHandle> out;
    for (core::EntityHandle h : selection_) {
        const sim::SpawnableEntity* e = game.entity(h);
        if (e && e->current_player == local_ && game.data().sheet(e->type).can_move()) out.push_back(h);
    }
    return out;
}

void InputController::handle_event(const sf::Event& event, sf::RenderWindow& window, render::Camera& camera,
                                   const sim::GameState& game, const render::EntityRenderer& entities,
                                   render::ViewOptions& options) {
    if (const auto* moved = event.getIf<sf::Event::MouseMoved>()) {
        if (pan_last_) {
            camera.pan_pixels(sf::Vector2f(*pan_last_ - moved->position), window.getSize());
            pan_last_ = moved->position;
        }
        mouse_ = moved->position;
        return;
    }

    if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        camera.zoom_at(wheel->delta > 0 ? 0.88f : 1.f / 0.88f, wheel->position, window);
        return;
    }

    if (const auto* press = event.getIf<sf::Event::MouseButtonPressed>()) {
        mouse_ = press->position;
        if (press->button == sf::Mouse::Button::Left) drag_start_ = press->position;
        if (press->button == sf::Mouse::Button::Middle) pan_last_ = press->position;
        if (press->button == sf::Mouse::Button::Right) order_move(press->position, window, camera, game);
        return;
    }

    if (const auto* release = event.getIf<sf::Event::MouseButtonReleased>()) {
        mouse_ = release->position;
        if (release->button == sf::Mouse::Button::Middle) pan_last_.reset();
        if (release->button == sf::Mouse::Button::Left && drag_start_) {
            const bool add = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                             sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
            if (drag_rect()) {
                box_select(*drag_start_, release->position, add, window, camera, game, entities);
            } else {
                const bool double_click = last_click_clock_.getElapsedTime().asSeconds() < kDoubleClickSeconds &&
                                          std::abs(release->position.x - last_click_pos_.x) < kDragThreshold &&
                                          std::abs(release->position.y - last_click_pos_.y) < kDragThreshold;
                click_select(release->position, add, window, camera, game, entities);
                if (double_click && selection_.size() == 1) select_same_type_on_screen(selection_.front(), game, entities);
                last_click_clock_.restart();
                last_click_pos_ = release->position;
            }
            drag_start_.reset();
        }
        return;
    }

    if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        using K = sf::Keyboard::Key;
        switch (key->code) {
        case K::Escape: selection_.clear(); break;
        case K::L:
            options.labels = options.labels == render::LabelMode::Smart ? render::LabelMode::All
                             : options.labels == render::LabelMode::All ? render::LabelMode::None
                                                                        : render::LabelMode::Smart;
            break;
        case K::G: options.grid = !options.grid; break;
        case K::H: options.help = !options.help; break;
        case K::S: {
            auto actors = own_movable(game);
            if (!actors.empty()) pending_.push_back(sim::Command{0, local_, sim::CommandType::Stop, std::move(actors), {}, {}});
            break;
        }
        case K::Space:
            if (!selection_.empty()) {
                if (const sim::SpawnableEntity* e = game.entity(selection_.front())) {
                    camera.center_on_world({e->position.x.to_float(), e->position.y.to_float()});
                }
            }
            break;
        default: break;
        }
    }
}

void InputController::click_select(sf::Vector2i pixel, bool add, const sf::RenderWindow& window, const render::Camera& camera,
                                   const sim::GameState& /*game*/, const render::EntityRenderer& entities) {
    const auto hit = entities.pick(window.mapPixelToCoords(pixel, camera.view()));
    if (!add) selection_.clear();
    if (!hit) return;
    auto it = std::find(selection_.begin(), selection_.end(), *hit);
    if (it != selection_.end()) selection_.erase(it);  // shift+clic sobre algo ya seleccionado lo quita
    else selection_.push_back(*hit);
}

void InputController::box_select(sf::Vector2i a, sf::Vector2i b, bool add, const sf::RenderWindow& window,
                                 const render::Camera& camera, const sim::GameState& /*game*/,
                                 const render::EntityRenderer& entities) {
    const sf::Vector2f p0 = window.mapPixelToCoords({std::min(a.x, b.x), std::min(a.y, b.y)}, camera.view());
    const sf::Vector2f p1 = window.mapPixelToCoords({std::max(a.x, b.x), std::max(a.y, b.y)}, camera.view());
    const sf::FloatRect box(p0, p1 - p0);

    // La caja solo coge unidades propias (como en cualquier RTS)
    std::vector<core::EntityHandle> found;
    for (const render::VisibleEntity& v : entities.visible()) {
        if (v.is_unit && v.owner == local_ && box.contains(v.base)) found.push_back(v.handle);
    }
    if (!add) selection_.clear();
    for (auto h : found) {
        if (std::find(selection_.begin(), selection_.end(), h) == selection_.end()) selection_.push_back(h);
    }
}

void InputController::select_same_type_on_screen(core::EntityHandle like, const sim::GameState& game,
                                                 const render::EntityRenderer& entities) {
    const sim::SpawnableEntity* ref = game.entity(like);
    if (!ref) return;
    selection_.clear();
    for (const render::VisibleEntity& v : entities.visible()) {
        if (v.type == ref->type && v.owner == ref->current_player) selection_.push_back(v.handle);
    }
}

void InputController::order_move(sf::Vector2i pixel, const sf::RenderWindow& window, const render::Camera& camera,
                                 const sim::GameState& game) {
    const sf::Vector2f w = render::iso_to_world(window.mapPixelToCoords(pixel, camera.view()));
    const float max_x = static_cast<float>(game.map().width()) - 0.5f;
    const float max_y = static_cast<float>(game.map().height()) - 0.5f;
    order_move_world({core::Fixed::from_double(std::clamp(w.x, 0.5f, max_x)), core::Fixed::from_double(std::clamp(w.y, 0.5f, max_y))},
                     game);
}

void InputController::order_move_world(core::Vec2F target, const sim::GameState& game) {
    auto actors = own_movable(game);
    if (actors.empty()) return;
    pending_.push_back(sim::Command{0, local_, sim::CommandType::Move, std::move(actors), target, {}});
    markers_.push_back({render::world_to_iso(target), 0.f});
}

}  // namespace input
