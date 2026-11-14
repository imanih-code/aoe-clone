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
#include "render/fog_renderer.hpp"
#include "render/map_renderer.hpp"
#include "render/minimap.hpp"
#include "sim/ai/scripted_ai.hpp"
#include "sim/game/game_state.hpp"
#include "sim/replay/replay.hpp"
#include "ui/command_panel.hpp"

namespace app {

struct AppOptions {
    std::filesystem::path data_dir;
    sim::GameConfig game;
    std::filesystem::path screenshot;  // si no está vacío: guarda una captura en screenshot_frame y sale
    int screenshot_frame = 90;
    bool demo = false;                 // economía automática al empezar (Fase 3)
    bool battle = false;               // dos ejércitos frente a frente (Fase 4)
    bool spectate = false;             // todos los jugadores son IA y se ve todo el mapa
    bool reveal = false;               // sin niebla de guerra
    bool no_ai = false;                // los rivales no hacen nada (para probar)
    int speed = 1;                     // 1, 2, 4 u 8
    std::filesystem::path replay_in;   // reproducir este replay
    std::filesystem::path replay_out = "replays/last_replay.json";
};

class GameApp {
public:
    explicit GameApp(AppOptions options);
    int run();

private:
    void draw_frame(float alpha, float fps);
    void draw_game_over();
    void simulate_tick();
    void save_replay();
    void start_demo();
    void start_battle();
    std::optional<data::PlayerId> viewer() const;

    AppOptions options_;
    data::DataRegistry data_;
    std::optional<sim::Replay> playback_;  // se carga antes que la partida (define su configuración)
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
    render::FogRenderer fog_;
    render::Minimap minimap_;

    std::vector<std::unique_ptr<sim::ScriptedAI>> ais_;
    sim::Replay recording_;
    std::unique_ptr<sim::ReplayPlayer> replay_player_;
    bool paused_ = false;
    bool dragging_minimap_ = false;
    bool replay_saved_ = false;
};

}  // namespace app
