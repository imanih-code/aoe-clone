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

bool shift_down() {
    return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
}
}  // namespace

sim::Command InputController::make(sim::CommandType type, std::vector<core::EntityHandle> actors) const {
    sim::Command c;
    c.issuer = local_;
    c.type = type;
    c.actors = std::move(actors);
    return c;
}

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

void InputController::update(float dt, const sim::GameState& game, const sf::RenderWindow& window, const render::Camera& camera) {
    // Quitar de la selección lo que ya no existe
    std::erase_if(selection_, [&](core::EntityHandle h) { return game.entity(h) == nullptr; });
    for (auto& m : markers_) m.age += dt;
    std::erase_if(markers_, [](const render::MoveMarker& m) { return m.age > 0.6f; });

    // El edificio a colocar sigue al ratón, con su centro bajo el cursor
    if (placement_) {
        if (own_units(game, true).empty()) {
            placement_.reset();
            return;
        }
        const auto& s = game.data().sheet(placement_->type);
        const sf::Vector2f w = render::iso_to_world(window.mapPixelToCoords(sf::Mouse::getPosition(window), camera.view()));
        placement_->tile = {static_cast<int32_t>(std::floor(w.x - s.stats.footprint_w * 0.5f + 0.5f)),
                            static_cast<int32_t>(std::floor(w.y - s.stats.footprint_h * 0.5f + 0.5f))};
        const auto& fog = game.visibility(local_);
        placement_->valid = game.can_place(placement_->type, placement_->tile) && game.can_afford(local_, s.stats.cost) &&
                            fog.explored(placement_->tile) &&
                            fog.explored({placement_->tile.x + s.stats.footprint_w - 1, placement_->tile.y + s.stats.footprint_h - 1});
    }
}

std::vector<core::EntityHandle> InputController::own_units(const sim::GameState& game, bool builders_only) const {
    std::vector<core::EntityHandle> out;
    for (core::EntityHandle h : selection_) {
        const sim::SpawnableEntity* e = game.entity(h);
        if (!e || e->current_player != local_) continue;
        const auto& s = game.sheet_of(*e);
        if (!s.can_move()) continue;
        if (builders_only && !s.stats.has(data::Capability::Build)) continue;
        out.push_back(h);
    }
    return out;
}

void InputController::handle_event(const sf::Event& event, sf::RenderWindow& window, render::Camera& camera,
                                   const sim::GameState& game, const render::EntityRenderer& entities,
                                   const ui::CommandPanel& panel, render::ViewOptions& options) {
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
        const sf::Vector2f px(press->position);
        if (press->button == sf::Mouse::Button::Middle) {
            pan_last_ = press->position;
            return;
        }
        // La interfaz tiene prioridad sobre el mundo
        if (panel.contains(px)) {
            if (press->button == sf::Mouse::Button::Left) {
                if (const ui::Button* b = panel.hit(px); b && b->enabled) activate(*b, game);
            }
            return;
        }
        if (press->button == sf::Mouse::Button::Left) {
            if (placement_) place_building(game, shift_down());
            else drag_start_ = press->position;
        }
        if (press->button == sf::Mouse::Button::Right) right_click(press->position, window, camera, game, entities);
        return;
    }

    if (const auto* release = event.getIf<sf::Event::MouseButtonReleased>()) {
        mouse_ = release->position;
        if (release->button == sf::Mouse::Button::Middle) pan_last_.reset();
        if (release->button == sf::Mouse::Button::Left && drag_start_) {
            const bool add = shift_down();
            if (drag_rect()) {
                box_select(*drag_start_, release->position, add, window, camera, entities);
            } else {
                const bool double_click = last_click_clock_.getElapsedTime().asSeconds() < kDoubleClickSeconds &&
                                          std::abs(release->position.x - last_click_pos_.x) < kDragThreshold &&
                                          std::abs(release->position.y - last_click_pos_.y) < kDragThreshold;
                click_select(release->position, add, window, camera, entities);
                if (double_click && selection_.size() == 1) select_same_type_on_screen(selection_.front(), game, entities);
                last_click_clock_.restart();
                last_click_pos_ = release->position;
            }
            drag_start_.reset();
        }
        return;
    }

    if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        // Atajos del panel (cuadrícula Q..B) primero
        if (const ui::Button* b = panel.by_key(key->code)) {
            if (b->enabled) activate(*b, game);
            return;
        }
        using K = sf::Keyboard::Key;
        switch (key->code) {
        case K::Escape:
            if (placement_) placement_.reset();
            else selection_.clear();
            break;
        case K::L:
            options.labels = options.labels == render::LabelMode::Smart ? render::LabelMode::All
                             : options.labels == render::LabelMode::All ? render::LabelMode::None
                                                                        : render::LabelMode::Smart;
            break;
        case K::F2: options.grid = !options.grid; break;
        case K::H:
        case K::F1: options.help = !options.help; break;
        case K::Period: select_next_idle(game, camera); break;
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

void InputController::activate(const ui::Button& button, const sim::GameState& game) {
    switch (button.kind) {
    case ui::ButtonKind::Build:
        placement_ = Placement{button.type, {}, false};
        break;
    case ui::ButtonKind::Stop:
        pending_.push_back(make(sim::CommandType::Stop, own_units(game)));
        break;
    case ui::ButtonKind::Train: {
        // Con varios edificios seleccionados, cada clic va al que tenga la cola más corta.
        std::vector<core::EntityHandle> buildings;
        for (core::EntityHandle h : selection_) {
            const sim::SpawnableEntity* e = game.entity(h);
            if (e && e->current_player == local_ && game.sheet_of(*e).can_train(button.type)) buildings.push_back(h);
        }
        if (buildings.empty()) break;
        std::stable_sort(buildings.begin(), buildings.end(), [&](auto a, auto b) {
            return game.entity(a)->production.size() < game.entity(b)->production.size();
        });
        auto cmd = make(sim::CommandType::Train, {buildings.front()});
        cmd.type_arg = button.type;
        pending_.push_back(std::move(cmd));
        break;
    }
    case ui::ButtonKind::Research: {
        const data::TypeId at = game.data().tech(button.tech).researched_at;
        for (core::EntityHandle h : selection_) {
            const sim::SpawnableEntity* e = game.entity(h);
            if (e && e->current_player == local_ && e->type == at) {
                auto cmd = make(sim::CommandType::Research, {h});
                cmd.index = button.tech;
                pending_.push_back(std::move(cmd));
                break;
            }
        }
        break;
    }
    case ui::ButtonKind::CancelQueue: {
        for (core::EntityHandle h : selection_) {
            const sim::SpawnableEntity* e = game.entity(h);
            if (e && e->current_player == local_ && !e->production.empty()) {
                auto cmd = make(sim::CommandType::CancelTrain, {h});
                cmd.index = button.index;
                pending_.push_back(std::move(cmd));
                break;
            }
        }
        break;
    }
    }
}

void InputController::place_building(const sim::GameState& game, bool keep_placing) {
    if (!placement_ || !placement_->valid) return;
    auto cmd = make(sim::CommandType::PlaceBuilding, own_units(game, true));
    cmd.type_arg = placement_->type;
    cmd.tile = placement_->tile;
    cmd.queued = keep_placing;  // con Shift: varios cimientos, construidos uno tras otro
    pending_.push_back(std::move(cmd));
    if (!keep_placing) placement_.reset();
}

void InputController::right_click(sf::Vector2i pixel, const sf::RenderWindow& window, const render::Camera& camera,
                                  const sim::GameState& game, const render::EntityRenderer& entities) {
    if (placement_) {
        placement_.reset();
        return;
    }
    const sf::Vector2f iso = window.mapPixelToCoords(pixel, camera.view());
    const sf::Vector2f w = render::iso_to_world(iso);
    const float max_x = static_cast<float>(game.map().width()) - 0.5f;
    const float max_y = static_cast<float>(game.map().height()) - 0.5f;
    const core::Vec2F ground{core::Fixed::from_double(std::clamp(w.x, 0.5f, max_x)), core::Fixed::from_double(std::clamp(w.y, 0.5f, max_y))};

    const auto units = own_units(game);
    // Shift: la orden se añade a la cola de cada unidad (se cumple después de las anteriores)
    const bool queued = shift_down();
    auto push = [&](sim::Command cmd, core::Vec2F where) {
        cmd.queued = queued;
        pending_.push_back(std::move(cmd));
        markers_.push_back({render::world_to_iso(where), 0.f});
    };
    // Ctrl + clic derecho: avanzar atacando lo que haya por el camino
    if (!units.empty() && (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))) {
        auto cmd = make(sim::CommandType::AttackMove, units);
        cmd.target_pos = ground;
        push(std::move(cmd), ground);
        return;
    }
    if (units.empty()) {
        // Solo edificios: el clic derecho fija el punto de reunión
        std::vector<core::EntityHandle> buildings;
        for (core::EntityHandle h : selection_) {
            const sim::SpawnableEntity* e = game.entity(h);
            if (e && e->current_player == local_ && !game.trainable_at(local_, e->type).empty()) buildings.push_back(h);
        }
        if (buildings.empty()) return;
        auto cmd = make(sim::CommandType::SetRally, std::move(buildings));
        cmd.target_pos = ground;
        pending_.push_back(std::move(cmd));
        markers_.push_back({render::world_to_iso(ground), 0.f});
        return;
    }

    // Orden contextual según lo que haya bajo el cursor
    if (const auto hit = entities.pick(iso)) {
        const sim::SpawnableEntity* target = game.entity(*hit);
        if (target && target->resource_node && target->is_complete()) {
            auto cmd = make(sim::CommandType::Gather, units);
            cmd.target = *hit;
            push(std::move(cmd), target->position);
            return;
        }
        // Animales: los aldeanos cazan (o sacrifican el ganado propio); las tropas atacan
        const bool animal = target && game.sheet_of(*target).stats.animal != data::AnimalBehavior::None;
        if (animal && (target->current_player == data::kGaia || target->current_player == local_)) {
            std::vector<core::EntityHandle> hunters, fighters;
            for (auto h : units) {
                (game.sheet_of(*game.entity(h)).stats.has(data::Capability::Gather) ? hunters : fighters).push_back(h);
            }
            const bool huntable = game.is_huntable(*target);
            if (huntable && !hunters.empty()) {
                auto cmd = make(sim::CommandType::Gather, hunters);
                cmd.target = *hit;
                push(std::move(cmd), target->position);
            } else {
                fighters.insert(fighters.end(), hunters.begin(), hunters.end());
            }
            // El ganado propio no se ataca con tropas: solo se mueven hasta él
            if (!fighters.empty()) {
                auto cmd = make(target->current_player == local_ ? sim::CommandType::Move : sim::CommandType::Attack, fighters);
                cmd.target = *hit;
                cmd.target_pos = target->position;
                push(std::move(cmd), target->position);
            }
            return;
        }
        if (target && target->current_player != local_ && target->current_player != data::kGaia && !target->projectile) {
            auto cmd = make(sim::CommandType::Attack, units);  // los que no pueden atacar se acercan
            cmd.target = *hit;
            push(std::move(cmd), target->position);
            return;
        }
        if (target && target->current_player == local_ && !target->is_complete()) {
            auto builders = own_units(game, true);
            if (!builders.empty()) {
                auto cmd = make(sim::CommandType::Build, std::move(builders));
                cmd.target = *hit;
                push(std::move(cmd), target->position);
                return;
            }
        }
    }
    order_move_world(ground, game, queued);
}

void InputController::order_move_world(core::Vec2F target, const sim::GameState& game, bool queued) {
    auto actors = own_units(game);
    if (actors.empty()) return;
    auto cmd = make(sim::CommandType::Move, std::move(actors));
    cmd.target_pos = target;
    cmd.queued = queued;
    pending_.push_back(std::move(cmd));
    markers_.push_back({render::world_to_iso(target), 0.f});
}

void InputController::select_next_idle(const sim::GameState& game, render::Camera& camera) {
    const auto& units = game.player(local_).units;
    if (units.empty()) return;
    for (std::size_t n = 0; n < units.size(); ++n) {
        const std::size_t i = (idle_cursor_ + n) % units.size();
        const sim::SpawnableEntity* e = game.entity(units[i]);
        if (!e || e->action || !game.sheet_of(*e).stats.has(data::Capability::Gather)) continue;
        selection_ = {units[i]};
        camera.center_on_world({e->position.x.to_float(), e->position.y.to_float()});
        idle_cursor_ = static_cast<uint32_t>(i + 1);
        return;
    }
}

void InputController::click_select(sf::Vector2i pixel, bool add, const sf::RenderWindow& window, const render::Camera& camera,
                                   const render::EntityRenderer& entities) {
    const auto hit = entities.pick(window.mapPixelToCoords(pixel, camera.view()));
    if (!add) selection_.clear();
    if (!hit) return;
    auto it = std::find(selection_.begin(), selection_.end(), *hit);
    if (it != selection_.end()) selection_.erase(it);  // shift+clic sobre algo ya seleccionado lo quita
    else selection_.push_back(*hit);
}

void InputController::box_select(sf::Vector2i a, sf::Vector2i b, bool add, const sf::RenderWindow& window,
                                 const render::Camera& camera, const render::EntityRenderer& entities) {
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

}  // namespace input
