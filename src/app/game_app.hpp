#pragma once

#include <filesystem>
#include <memory>

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderWindow.hpp>

#include "data/data_registry.hpp"
#include "input/input_controller.hpp"
#include "render/camera.hpp"
#include "render/entity_renderer.hpp"
#include "render/hud.hpp"
#include "render/map_renderer.hpp"
#include "sim/game/game_state.hpp"
#include "ui/command_panel.hpp"

namespace app {

struct AppOptions {
    std::filesystem::path data_dir;
    sim::GameConfig game;
    std::filesystem::path screenshot;  // si no está vacío: guarda una captura en screenshot_frame y sale
    int screenshot_frame = 90;
    bool demo = false;                 // selecciona las unidades propias y las mueve al empezar
};

class GameApp {
public:
    explicit GameApp(AppOptions options);
    int run();

private:
    void draw_frame(float alpha, float fps);
    void start_demo();

    AppOptions options_;
    data::DataRegistry data_;
    sf::RenderWindow window_;
    sf::Font font_;
    std::unique_ptr<sim::GameState> game_;
    render::Camera camera_;
    render::MapRenderer map_renderer_;
    render::EntityRenderer entity_renderer_;
    render::Hud hud_;
    render::ViewOptions view_options_;
    input::InputController input_;
    ui::CommandPanel panel_;
};

}  // namespace app
