#include "sim/replay/replay.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace sim {

using nlohmann::json;

namespace {

json handle_json(core::EntityHandle h) { return json::array({h.index, h.generation}); }
core::EntityHandle handle_from(const json& j) { return {j.at(0).get<uint32_t>(), j.at(1).get<uint32_t>()}; }

json command_json(const Command& c) {
    json actors = json::array();
    for (auto h : c.actors) actors.push_back(handle_json(h));
    // Las coordenadas se guardan en crudo (punto fijo): nada de float en un replay
    return json{{"t", c.tick},
                {"p", c.issuer},
                {"c", static_cast<int>(c.type)},
                {"a", std::move(actors)},
                {"pos", json::array({c.target_pos.x.raw, c.target_pos.y.raw})},
                {"tgt", handle_json(c.target)},
                {"type", c.type_arg.value},
                {"tile", json::array({c.tile.x, c.tile.y})},
                {"i", c.index},
                {"q", c.queued}};
}

Command command_from(const json& j) {
    Command c;
    c.tick = j.at("t").get<uint32_t>();
    c.issuer = j.at("p").get<data::PlayerId>();
    c.type = static_cast<CommandType>(j.at("c").get<int>());
    for (const auto& a : j.at("a")) c.actors.push_back(handle_from(a));
    c.target_pos = {core::Fixed::from_raw(j.at("pos").at(0).get<int32_t>()), core::Fixed::from_raw(j.at("pos").at(1).get<int32_t>())};
    c.target = handle_from(j.at("tgt"));
    c.type_arg = data::TypeId{j.at("type").get<uint16_t>()};
    c.tile = {j.at("tile").at(0).get<int32_t>(), j.at("tile").at(1).get<int32_t>()};
    c.index = j.at("i").get<int32_t>();
    c.queued = j.value("q", false);
    return c;
}

}  // namespace

void Replay::after_step(const GameState& game) {
    final_tick = game.tick();
    if (game.tick() % kCheckpointEvery == 0) checkpoints.emplace_back(game.tick(), game.state_hash());
}

void Replay::save(const std::filesystem::path& path) const {
    json cmds = json::array();
    for (const auto& c : commands) cmds.push_back(command_json(c));
    json checks = json::array();
    for (const auto& [t, h] : checkpoints) checks.push_back(json::array({t, h}));
    const json j{{"version", kVersion},
                 {"config", {{"seed", config.seed}, {"width", config.map_width}, {"height", config.map_height},
                             {"players", config.num_players}, {"civs", config.civs}}},
                 {"final_tick", final_tick},
                 {"commands", std::move(cmds)},
                 {"checkpoints", std::move(checks)}};
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) throw std::runtime_error("No se pudo escribir el replay en " + path.string());
    out << j.dump();
}

Replay Replay::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("No se pudo abrir el replay " + path.string());
    try {
        const json j = json::parse(in);
        if (j.at("version").get<int>() != kVersion) throw std::runtime_error("Versión de replay no soportada");
        Replay r;
        const json& cfg = j.at("config");
        r.config.seed = cfg.at("seed").get<uint32_t>();
        r.config.map_width = cfg.at("width").get<int>();
        r.config.map_height = cfg.at("height").get<int>();
        r.config.num_players = cfg.at("players").get<int>();
        r.config.civs = cfg.at("civs").get<std::vector<std::string>>();
        r.final_tick = j.at("final_tick").get<uint32_t>();
        for (const auto& c : j.at("commands")) r.commands.push_back(command_from(c));
        for (const auto& c : j.at("checkpoints")) r.checkpoints.emplace_back(c.at(0).get<uint32_t>(), c.at(1).get<uint64_t>());
        return r;
    } catch (const json::exception& e) {
        throw std::runtime_error(path.string() + ": replay inválido: " + e.what());
    }
}

std::vector<Command> ReplayPlayer::commands_for(uint32_t tick) {
    std::vector<Command> out;
    while (next_command_ < replay_.commands.size() && replay_.commands[next_command_].tick <= tick) {
        if (replay_.commands[next_command_].tick == tick) out.push_back(replay_.commands[next_command_]);
        ++next_command_;
    }
    return out;
}

bool ReplayPlayer::verify(const GameState& game) {
    while (next_checkpoint_ < replay_.checkpoints.size() && replay_.checkpoints[next_checkpoint_].first < game.tick()) ++next_checkpoint_;
    if (next_checkpoint_ < replay_.checkpoints.size() && replay_.checkpoints[next_checkpoint_].first == game.tick()) {
        if (replay_.checkpoints[next_checkpoint_].second != game.state_hash()) desynced_ = true;
        ++next_checkpoint_;
    }
    return !desynced_;
}

}  // namespace sim
