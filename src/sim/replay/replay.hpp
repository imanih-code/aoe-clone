#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "sim/commands/command.hpp"
#include "sim/game/game_state.hpp"

namespace sim {

// Replay = GameConfig + todas las órdenes (humanas y de la IA) + hashes de control.
// Funciona porque la simulación es determinista (architecture.md §9.2, §11.6).
struct Replay {
    static constexpr int kVersion = 1;
    static constexpr uint32_t kCheckpointEvery = 100;  // ticks

    GameConfig config;
    std::vector<Command> commands;                           // ordenadas por tick
    std::vector<std::pair<uint32_t, uint64_t>> checkpoints;  // (tick tras el paso, hash)
    uint32_t final_tick = 0;

    void record(std::span<const Command> tick_commands) { commands.insert(commands.end(), tick_commands.begin(), tick_commands.end()); }
    // Llamar después de cada step()
    void after_step(const GameState& game);

    void save(const std::filesystem::path& path) const;
    static Replay load(const std::filesystem::path& path);  // lanza std::runtime_error
};

// Reproduce un replay tick a tick y comprueba los hashes.
class ReplayPlayer {
public:
    explicit ReplayPlayer(const Replay& replay) : replay_(replay) {}

    // Órdenes que tocan en este tick
    std::vector<Command> commands_for(uint32_t tick);
    // Tras step(): false si el hash no coincide con el grabado (desincronización)
    bool verify(const GameState& game);
    bool finished(const GameState& game) const { return game.tick() >= replay_.final_tick; }
    bool desynced() const { return desynced_; }

private:
    const Replay& replay_;
    std::size_t next_command_ = 0;
    std::size_t next_checkpoint_ = 0;
    bool desynced_ = false;
};

}  // namespace sim
