#include "app/game_app.hpp"

#include <algorithm>
#include <iostream>

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>

#include "render/iso.hpp"
#include "render/text.hpp"

namespace app {

GameApp::GameApp(AppOptions options)
    : options_(std::move(options)),
      data_(data::DataRegistry::load(options_.data_dir)),
      window_(sf::VideoMode({1280u, 720u}), render::utf8("AoE Clone — prototipo")),
      font_(render::load_ui_font()),
      game_(std::make_unique<sim::GameState>(data_, options_.game)),
      camera_(window_.getSize(), game_->map().width(), game_->map().height()),
      entity_renderer_(font_, data_),
      hud_(font_),
      input_(1) {
    window_.setVerticalSyncEnabled(true);
    map_renderer_.build(game_->map());
    const core::Vec2F start = game_->start_position(input_.local_player());
    camera_.center_on_world({start.x.to_float(), start.y.to_float() + 2.f});
}

int GameApp::run() {
    constexpr float kStep = 1.f / static_cast<float>(sim::kTicksPerSecond);
    sf::Clock clock;
    float accumulator = 0.f;
    float fps = 0.f;
    float fps_timer = 0.f;
    int fps_frames = 0;
    int frame = 0;

    while (window_.isOpen()) {
        panel_.rebuild(*game_, input_.local_player(), input_.selection(), window_.getSize());
        while (const std::optional event = window_.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window_.close();
                break;
            }
            if (const auto* resized = event->getIf<sf::Event::Resized>()) camera_.on_resize(resized->size);
            input_.handle_event(*event, window_, camera_, *game_, entity_renderer_, panel_, view_options_);
        }
        if (!window_.isOpen()) break;

        const float dt = std::min(clock.restart().asSeconds(), 0.25f);  // evita la espiral de la muerte
        camera_.update(dt, window_);
        input_.update(dt, *game_, window_, camera_);

        // Simulación a tick fijo; el render interpola con alpha (architecture.md §9.1)
        accumulator += dt;
        while (accumulator >= kStep) {
            const auto commands = input_.take_commands(game_->tick());
            game_->step(commands);
            accumulator -= kStep;
        }

        fps_timer += dt;
        ++fps_frames;
        if (fps_timer >= 0.5f) {
            fps = static_cast<float>(fps_frames) / fps_timer;
            fps_timer = 0.f;
            fps_frames = 0;
        }

        if (options_.demo && frame == 5) start_demo();
        if (options_.battle && frame == 5) start_battle();

        draw_frame(accumulator / kStep, fps);
        ++frame;

        if (!options_.screenshot.empty() && frame == options_.screenshot_frame) {
            sf::Texture capture(window_.getSize());
            capture.update(window_);
            if (!capture.copyToImage().saveToFile(options_.screenshot)) {
                std::cerr << "No se pudo guardar la captura en " << options_.screenshot << "\n";
                return 1;
            }
            std::cout << "Captura guardada en " << options_.screenshot.string() << "\n";
            window_.close();
        }
    }
    return 0;
}

// Modo demo: dos aldeanos a las bayas, uno construye una casa, el explorador sale
// a explorar y la plaza mayor entrena dos aldeanos. Sirve para probar la Fase 3 sin ratón.
void GameApp::start_demo() {
    const data::PlayerId me_id = input_.local_player();
    const auto& me = game_->player(me_id);
    const core::Vec2F start = game_->start_position(me_id);
    std::vector<core::EntityHandle> villagers, others;
    for (auto h : me.units) {
        const auto* e = game_->entity(h);
        (game_->sheet_of(*e).stats.has(data::Capability::Gather) ? villagers : others).push_back(h);
    }
    auto command = [&](sim::CommandType type, std::vector<core::EntityHandle> actors) {
        sim::Command c;
        c.issuer = me_id;
        c.type = type;
        c.actors = std::move(actors);
        return c;
    };

    if (villagers.size() >= 2) {
        const auto berry = game_->nearest_resource(data::ResourceTag::Food, start, 16);
        if (berry) {
            auto c = command(sim::CommandType::Gather, {villagers[0], villagers[1]});
            c.target = berry;
            input_.submit(std::move(c));
        }
    }
    if (villagers.size() >= 3) {
        const data::TypeId house = data_.type("house");
        const core::TilePos s = core::tile_of(start);
        for (int r = 4; r < 10; ++r) {
            const core::TilePos t{s.x - r, s.y + 1};
            if (game_->can_place(house, t)) {
                auto c = command(sim::CommandType::PlaceBuilding, {villagers[2]});
                c.type_arg = house;
                c.tile = t;
                input_.submit(std::move(c));
                break;
            }
        }
    }
    if (!others.empty()) {
        auto c = command(sim::CommandType::Move, others);
        c.target_pos = {start.x + core::Fixed::from_int(12), start.y - core::Fixed::from_int(8)};
        input_.submit(std::move(c));
    }
    if (!me.buildings.empty()) {
        for (int i = 0; i < 2; ++i) {
            auto c = command(sim::CommandType::Train, {me.buildings.front()});
            c.type_arg = data_.type("villager");
            input_.submit(std::move(c));
        }
    }
    // Seleccionar los aldeanos y dejar un aserradero "en la mano" para ver el panel y la vista previa
    input_.select(villagers);
    input_.begin_placement(data_.type("lumber_camp"));
}

// Modo batalla: dos ejércitos frente a frente junto a la base del jugador 1,
// con una torre propia. Sirve para ver el combate de la Fase 4 sin jugar una partida.
void GameApp::start_battle() {
    const core::Vec2F s = game_->start_position(1);
    const core::TilePos c{s.x.floor_int() + 9, s.y.floor_int() + 2};
    std::vector<core::EntityHandle> mine;
    auto spawn = [&](const char* key, data::PlayerId p, int dx, int dy) {
        const auto h = game_->debug_spawn(data_.type(key), p, {c.x + dx, c.y + dy});
        if (h && p == 1) mine.push_back(h);
    };
    for (int i = 0; i < 5; ++i) spawn("militia", 1, -2, i - 2);
    for (int i = 0; i < 4; ++i) spawn("archer", 1, -4, i - 2);
    for (int i = 0; i < 4; ++i) spawn("militia", 2, 4, i - 2);
    for (int i = 0; i < 3; ++i) spawn("scout", 2, 6, i - 1);
    for (int r = 1; r < 6; ++r) {
        if (game_->debug_spawn(data_.type("watch_tower"), 1, {c.x - 3, c.y - 3 - r})) break;
    }
    input_.select(mine);
    camera_.center_on_world({static_cast<float>(c.x), static_cast<float>(c.y)});
}

void GameApp::draw_frame(float alpha, float fps) {
    window_.clear(sf::Color(18, 22, 28));

    // ─── Mundo (vista de la cámara) ──────────────────────────────────────
    window_.setView(camera_.view());
    map_renderer_.draw(window_, camera_.visible_rect(), view_options_.grid);
    entity_renderer_.draw_world(window_, *game_, camera_, alpha, input_.selection());
    entity_renderer_.draw_markers(window_, input_.markers());
    if (const auto& placing = input_.placement()) {
        entity_renderer_.draw_placement(window_, *game_, placing->type, placing->tile, placing->valid);
    }

    const sf::Vector2i mouse = sf::Mouse::getPosition(window_);
    const sf::Vector2f mouse_iso = window_.mapPixelToCoords(mouse, camera_.view());
    const auto hovered = entity_renderer_.pick(mouse_iso);
    const sf::Vector2f mouse_world = render::iso_to_world(mouse_iso);
    const core::TilePos hovered_tile{static_cast<int32_t>(std::floor(mouse_world.x)), static_cast<int32_t>(std::floor(mouse_world.y))};

    // ─── Interfaz (píxeles de pantalla) ──────────────────────────────────
    const sf::Vector2f size(window_.getSize());
    window_.setView(sf::View(sf::FloatRect({0.f, 0.f}, size)));
    entity_renderer_.draw_labels(window_, *game_, camera_, view_options_, hovered, input_.selection());

    render::HudInfo info;
    info.local_player = input_.local_player();
    info.selection = input_.selection();
    info.hovered_tile = hovered_tile;
    info.fps = fps;
    info.drag_rect = input_.drag_rect();
    hud_.draw(window_, *game_, info, view_options_);
    panel_.draw(window_, font_, sf::Vector2f(mouse));

    window_.display();
}

}  // namespace app
