#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "app/game_app.hpp"

namespace fs = std::filesystem;

namespace {

fs::path find_data_dir(const char* argv0) {
    std::error_code ec;
    const fs::path exe_dir = fs::absolute(argv0, ec).parent_path();
    const fs::path candidates[] = {exe_dir / "data", fs::current_path() / "data", fs::path(AOE_SOURCE_DIR) / "data"};
    for (const auto& c : candidates) {
        if (fs::exists(c / "classes.json", ec)) return c;
    }
    throw std::runtime_error("No se encontró la carpeta data/ (junto al ejecutable o en el directorio actual)");
}

void print_usage() {
    std::cout << "Uso: aoe_clone [--seed N] [--players N] [--size N] [--civ valdoria|tarvesh|ostreva] [--demo] [--battle] [--showcase] [--spectate] [--reveal] [--no-ai] [--speed 1|2|4|8] [--replay fichero.json] [--record fichero.json] [--screenshot fichero.png] [--frame N]\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        app::AppOptions options;
        options.data_dir = find_data_dir(argv[0]);

        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("Falta el valor de " + std::string(arg));
                return argv[++i];
            };
            if (arg == "--seed") options.game.seed = static_cast<uint32_t>(std::stoul(next()));
            else if (arg == "--players") options.game.num_players = std::stoi(next());
            else if (arg == "--size") options.game.map_width = options.game.map_height = std::stoi(next());
            else if (arg == "--screenshot") options.screenshot = next();
            else if (arg == "--frame") options.screenshot_frame = std::stoi(next());
            else if (arg == "--demo") options.demo = true;
            else if (arg == "--battle") options.battle = true;
            else if (arg == "--showcase") options.showcase = true;
            else if (arg == "--spectate") options.spectate = true;
            else if (arg == "--reveal") options.reveal = true;
            else if (arg == "--no-ai") options.no_ai = true;
            else if (arg == "--speed") options.speed = std::stoi(next());
            else if (arg == "--replay") options.replay_in = next();
            else if (arg == "--record") options.replay_out = next();
            else if (arg == "--civ") options.game.civs = {next()};
            else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            } else {
                print_usage();
                return 2;
            }
        }

        app::GameApp game(std::move(options));
        return game.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
