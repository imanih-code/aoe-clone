#include "app/game_app.hpp"

#include <algorithm>
#include <iostream>

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>

#include "render/iso.hpp"
#include "render/text.hpp"

namespace app {

GameApp::GameApp(AppOptions options)
    : options_(std::move(options)),
      data_(data::DataRegistry::load(options_.data_dir)),
      playback_(options_.replay_in.empty() ? std::nullopt : std::optional<sim::Replay>(sim::Replay::load(options_.replay_in))),
      window_(sf::VideoMode({1280u, 720u}), render::utf8("AoE Clone — prototipo")),
      font_(render::load_ui_font()),
      game_(std::make_unique<sim::GameState>(data_, playback_ ? playback_->config : options_.game)),
      camera_(window_.getSize(), game_->map().width(), game_->map().height()),
      entity_renderer_(font_, data_),
      hud_(font_),
      input_(1) {
    window_.setVerticalSyncEnabled(true);
    map_renderer_.build(game_->map());
    minimap_.build(game_->map());
    const core::Vec2F start = game_->start_position(input_.local_player());
    camera_.center_on_world({start.x.to_float(), start.y.to_float() + 2.f});

    if (playback_) {
        // Reproducir: ni IA ni órdenes del jugador; solo lo grabado
        replay_player_ = std::make_unique<sim::ReplayPlayer>(*playback_);
        options_.spectate = true;
    } else {
        recording_.config = game_->config();
        const int first_ai = options_.spectate ? 1 : 2;
        if (!options_.no_ai) {
            for (int p = first_ai; p <= game_->num_players(); ++p) {
                ais_.push_back(std::make_unique<sim::ScriptedAI>(static_cast<data::PlayerId>(p)));
            }
        }
    }
    options_.speed = std::clamp(options_.speed, 1, 8);
}

std::optional<data::PlayerId> GameApp::viewer() const {
    if (options_.spectate || options_.reveal) return std::nullopt;  // se ve todo
    return input_.local_player();
}

void GameApp::simulate_tick() {
    std::vector<sim::Command> commands;
    if (replay_player_) {
        commands = replay_player_->commands_for(game_->tick());
    } else {
        auto mine = input_.take_commands(game_->tick());
        if (!options_.spectate) commands = std::move(mine);
        for (auto& ai : ais_) {
            auto orders = ai->think(*game_);
            for (auto& c : orders) c.tick = game_->tick();
            commands.insert(commands.end(), orders.begin(), orders.end());
        }
        recording_.record(commands);
    }
    game_->step(commands);
    if (replay_player_) {
        if (!replay_player_->verify(*game_) && game_->tick() % 100 == 0) {
            std::cerr << "Replay desincronizado en el tick " << game_->tick() << "\n";
        }
    } else {
        recording_.after_step(*game_);
    }
}

void GameApp::save_replay() {
    if (replay_player_ || replay_saved_ || options_.replay_out.empty()) return;
    try {
        recording_.save(options_.replay_out);
        replay_saved_ = true;
        std::cout << "Replay guardado en " << std::filesystem::absolute(options_.replay_out).string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
    }
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

            // Teclas de la aplicación: velocidad, pausa, salir al terminar
            if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                using K = sf::Keyboard::Key;
                if (key->code == K::Add || key->code == K::Equal) { options_.speed = std::min(8, options_.speed * 2); continue; }
                if (key->code == K::Subtract || key->code == K::Hyphen) { options_.speed = std::max(1, options_.speed / 2); continue; }
                if (key->code == K::P || key->code == K::Pause) { paused_ = !paused_; continue; }
                if (game_->is_game_over() && (key->code == K::Escape || key->code == K::Enter)) { window_.close(); break; }
            }
            // Minimapa: clic o arrastre mueve la cámara
            if (const auto* press = event->getIf<sf::Event::MouseButtonPressed>()) {
                const sf::Vector2f px(press->position);
                if (press->button == sf::Mouse::Button::Left && minimap_.contains(px)) {
                    camera_.center_on_world(minimap_.pixel_to_world(px));
                    dragging_minimap_ = true;
                    continue;
                }
            }
            if (const auto* moved = event->getIf<sf::Event::MouseMoved>(); moved && dragging_minimap_) {
                camera_.center_on_world(minimap_.pixel_to_world(sf::Vector2f(moved->position)));
                continue;
            }
            if (event->is<sf::Event::MouseButtonReleased>() && dragging_minimap_) {
                dragging_minimap_ = false;
                continue;
            }
            input_.handle_event(*event, window_, camera_, *game_, entity_renderer_, panel_, view_options_);
        }
        if (!window_.isOpen()) break;

        const float dt = std::min(clock.restart().asSeconds(), 0.25f);  // evita la espiral de la muerte
        camera_.update(dt, window_);
        input_.update(dt, *game_, window_, camera_);

        // Simulación a tick fijo; el render interpola con alpha (architecture.md §9.1).
        // La velocidad solo cambia cuántos ticks se ejecutan por segundo real: el resultado es el mismo.
        const bool running = !paused_ && !game_->is_game_over() && !(replay_player_ && replay_player_->finished(*game_));
        if (running) accumulator += dt * static_cast<float>(options_.speed);
        else accumulator = 0.f;
        int steps = 0;
        while (accumulator >= kStep && steps++ < 40) {
            simulate_tick();
            accumulator -= kStep;
        }
        if (game_->is_game_over()) save_replay();

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
    save_replay();
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
        const auto berry = game_->nearest_resource(data::ResourceTag::Food, start, 16, me_id);
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
    const auto who = viewer();
    map_renderer_.draw(window_, camera_.visible_rect(), view_options_.grid);
    entity_renderer_.draw_world(window_, *game_, camera_, alpha, input_.selection(), who);
    if (who) {
        // Niebla encima del mundo: tapa lo no explorado y oscurece lo que no se ve ahora
        fog_.update(game_->visibility(*who));
        fog_.draw(window_);
    }
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
    if (replay_player_) info.mode = replay_player_->desynced() ? "REPLAY · ¡DESINCRONIZADO!" : "REPLAY";
    else if (options_.spectate) info.mode = "ESPECTADOR";
    if (paused_) info.mode += info.mode.empty() ? "PAUSA" : " · PAUSA";
    info.speed = options_.speed;
    info.minimap_top = minimap_.rect().position.y;
    hud_.draw(window_, *game_, info, view_options_);
    if (!options_.spectate) panel_.draw(window_, font_, sf::Vector2f(mouse));
    minimap_.draw(window_, *game_, who, camera_);
    if (game_->is_game_over()) draw_game_over();

    window_.display();
}

void GameApp::draw_game_over() {
    const sf::Vector2f size(window_.getSize());
    sf::RectangleShape shade(size);
    shade.setFillColor(sf::Color(0, 0, 0, 120));
    window_.draw(shade);

    const auto winner = game_->winner();
    std::string title;
    if (options_.spectate) title = winner ? "Gana " + game_->player(*winner).name : "Empate";
    else title = winner && *winner == input_.local_player() ? "¡Victoria!" : "Derrota";

    sf::RectangleShape box({460.f, 90.f + 26.f * static_cast<float>(game_->num_players())});
    box.setOrigin(box.getSize() * 0.5f);
    box.setPosition(size * 0.5f);
    box.setFillColor(sf::Color(28, 22, 16, 240));
    box.setOutlineColor(sf::Color(200, 170, 90));
    box.setOutlineThickness(2.f);
    window_.draw(box);

    sf::Text text(font_, render::utf8(title), 34);
    text.setStyle(sf::Text::Bold);
    text.setFillColor(sf::Color(250, 220, 140));
    const sf::FloatRect tb = text.getLocalBounds();
    text.setOrigin({tb.position.x + tb.size.x * 0.5f, 0.f});
    text.setPosition({size.x * 0.5f, box.getPosition().y - box.getSize().y * 0.5f + 12.f});
    window_.draw(text);

    // Resumen: bajas causadas y sufridas por jugador
    float y = box.getPosition().y - box.getSize().y * 0.5f + 62.f;
    for (int p = 1; p <= game_->num_players(); ++p) {
        const auto& ps = game_->player(static_cast<data::PlayerId>(p));
        uint32_t kills = 0, losses = 0;
        for (const auto& [t, n] : ps.kills_by_type) kills += n;
        for (const auto& [t, n] : ps.losses_by_type) losses += n;
        const std::string line = ps.name + (ps.civ ? " (" + ps.civ->name + ")" : std::string()) + "   bajas causadas " +
                                 std::to_string(kills) + " · sufridas " + std::to_string(losses) + (ps.is_defeated ? "   ✗" : "");
        sf::Text row(font_, render::utf8(line), 15);
        row.setFillColor(render::mix(render::rgba(ps.color), sf::Color::White, 0.5f));
        row.setPosition({box.getPosition().x - box.getSize().x * 0.5f + 18.f, y});
        window_.draw(row);
        y += 26.f;
    }
    sf::Text hint(font_, render::utf8("Esc o Intro para salir · el replay se guardó en replays/"), 13);
    hint.setFillColor(sf::Color(200, 200, 200));
    hint.setPosition({box.getPosition().x - box.getSize().x * 0.5f + 18.f, y + 4.f});
    window_.draw(hint);
}

}  // namespace app
