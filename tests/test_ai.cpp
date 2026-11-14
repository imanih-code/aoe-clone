#include <doctest.h>

#include <filesystem>
#include <memory>
#include <optional>

#include "data/data_registry.hpp"
#include "sim/ai/scripted_ai.hpp"
#include "sim/game/game_state.hpp"
#include "sim/replay/replay.hpp"

namespace {

const data::DataRegistry& registry() {
    static const data::DataRegistry reg = data::DataRegistry::load(std::filesystem::path(AOE_SOURCE_DIR) / "data");
    return reg;
}

data::TypeId T(const char* key) { return registry().type(key); }

// Partida IA contra IA. Graba todas las órdenes en `replay` si se pasa.
void play_ai(sim::GameState& g, int ticks, sim::Replay* replay = nullptr) {
    std::vector<std::unique_ptr<sim::ScriptedAI>> ais;
    for (int p = 1; p <= g.num_players(); ++p) ais.push_back(std::make_unique<sim::ScriptedAI>(static_cast<data::PlayerId>(p)));
    for (int t = 0; t < ticks && !g.is_game_over(); ++t) {
        std::vector<sim::Command> cmds;
        for (auto& ai : ais) {
            auto c = ai->think(g);
            for (auto& x : c) x.tick = g.tick();
            cmds.insert(cmds.end(), c.begin(), c.end());
        }
        if (replay) replay->record(cmds);
        g.step(cmds);
        if (replay) replay->after_step(g);
    }
}

int count_type(const sim::GameState& g, data::PlayerId p, data::TypeId t) {
    int n = 0;
    for (auto h : g.player(p).units) n += g.entity(h) && g.entity(h)->type == t;
    for (auto h : g.player(p).buildings) n += g.entity(h) && g.entity(h)->type == t;
    return n;
}

}  // namespace

TEST_CASE("Niebla: cada jugador ve lo suyo y recuerda lo explorado") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const core::TilePos mine = core::tile_of(g.start_position(1));
    const core::TilePos theirs = core::tile_of(g.start_position(2));
    CHECK(g.visibility(1).visible(mine));
    CHECK_FALSE(g.visibility(1).visible(theirs));
    CHECK_FALSE(g.visibility(1).explored(theirs));
    const auto enemy_tc = g.player(2).buildings.front();
    CHECK_FALSE(g.is_visible_to(1, *g.entity(enemy_tc)));
    CHECK(g.is_visible_to(2, *g.entity(enemy_tc)));

    // El explorador va y vuelve: lo visto queda explorado pero ya no visible
    core::EntityHandle scout;
    for (auto h : g.player(1).units) {
        if (g.entity(h)->type == T("scout")) scout = h;
    }
    REQUIRE(scout);
    const core::Vec2F home = g.entity(scout)->position;
    sim::Command go;
    go.issuer = 1;
    go.type = sim::CommandType::Move;
    go.actors = {scout};
    go.target_pos = {home.x + core::Fixed::from_int(12), home.y};
    g.step(std::span<const sim::Command>(&go, 1));
    for (int i = 0; i < 20 * 12; ++i) g.step({});
    const core::TilePos far = core::tile_of(g.entity(scout)->position);
    CHECK(g.visibility(1).visible(far));
    go.target_pos = home;
    g.step(std::span<const sim::Command>(&go, 1));
    for (int i = 0; i < 20 * 12; ++i) g.step({});
    CHECK(g.visibility(1).explored(far));
}

TEST_CASE("Niebla: no se puede atacar lo que no se ve") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto enemy_tc = g.player(2).buildings.front();
    sim::Command attack;
    attack.issuer = 1;
    attack.type = sim::CommandType::Attack;
    attack.actors = g.player(1).units;
    attack.target = enemy_tc;
    g.step(std::span<const sim::Command>(&attack, 1));
    for (auto h : g.player(1).units) CHECK_FALSE(g.entity(h)->action);
}

TEST_CASE("Victoria: sin unidades ni edificios el jugador pierde y la partida acaba") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    CHECK_FALSE(g.is_game_over());
    g.debug_destroy_player(2);
    for (int i = 0; i < 20; ++i) g.step({});
    CHECK(g.player(2).is_defeated);
    CHECK(g.is_game_over());
    REQUIRE(g.winner());
    CHECK(*g.winner() == 1);
}

TEST_CASE("Avanzar atacando: pelea con lo que encuentra y sigue") {
    sim::GameState g(registry(), sim::GameConfig{11, 96, 96, 2, {"valdoria", "tarvesh"}});
    // Una franja libre (sin agua ni árboles) lejos de las bases
    const core::Vec2F a = g.start_position(1), b = g.start_position(2);
    const core::TilePos center{(a.x.floor_int() + b.x.floor_int()) / 2, (a.y.floor_int() + b.y.floor_int()) / 2};
    std::optional<core::TilePos> found;
    for (int r = 0; r < 30 && !found; ++r) {
        for (int dy = -r; dy <= r && !found; ++dy) {
            for (int dx = -r; dx <= r && !found; ++dx) {
                bool free = true;
                for (int y = -3; y <= 3 && free; ++y)
                    for (int x = -8; x <= 8 && free; ++x) free = g.map().is_passable(center.x + dx + x, center.y + dy + y);
                if (free) found = core::TilePos{center.x + dx, center.y + dy};
            }
        }
    }
    REQUIRE(found);
    const core::TilePos mid = *found;
    std::vector<core::EntityHandle> army;
    for (int i = 0; i < 5; ++i) army.push_back(g.debug_spawn(T("militia"), 1, {mid.x - 6, mid.y + i - 2}));
    const auto victim = g.debug_spawn(T("villager"), 2, {mid.x, mid.y});
    REQUIRE(victim);
    sim::Command c;
    c.issuer = 1;
    c.type = sim::CommandType::AttackMove;
    c.actors = army;
    c.target_pos = core::tile_center({mid.x + 6, mid.y});
    g.step(std::span<const sim::Command>(&c, 1));
    for (int i = 0; i < 20 * 40; ++i) g.step({});
    CHECK_FALSE(g.entity(victim));  // lo mataron por el camino
    int arrived = 0;
    for (auto h : army) {
        if (const auto* e = g.entity(h)) arrived += core::length(e->position - c.target_pos) < core::Fixed::from_int(3);
    }
    CHECK(arrived >= 3);
}

TEST_CASE("IA: crece, construye y entrena ejército") {
    sim::GameState g(registry(), sim::GameConfig{21, 96, 96, 2, {}});
    play_ai(g, 10 * 60 * sim::kTicksPerSecond);  // 10 minutos
    for (data::PlayerId p = 1; p <= 2; ++p) {
        INFO("jugador " << int(p));
        CHECK(count_type(g, p, T("villager")) >= 15);
        CHECK(count_type(g, p, T("house")) >= 3);
        CHECK(count_type(g, p, T("lumber_camp")) >= 1);
        CHECK(count_type(g, p, T("barracks")) >= 1);
        CHECK(g.player(p).population_current >= 15);
        const int army = count_type(g, p, T("militia")) + count_type(g, p, T("man_at_arms")) + count_type(g, p, T("spearman")) +
                         count_type(g, p, T("archer")) + count_type(g, p, T("valdorian_guard"));
        CHECK(army + static_cast<int>(g.player(p).losses_by_type.size()) >= 1);  // entrenó tropas (aunque ya hayan caído)
    }
}

TEST_CASE("IA: determinista, y su replay reproduce la partida tick a tick") {
    const sim::GameConfig cfg{33, 96, 96, 2, {"ostreva", "tarvesh"}};
    sim::Replay replay;
    replay.config = cfg;
    sim::GameState original(registry(), cfg);
    play_ai(original, 5 * 60 * sim::kTicksPerSecond, &replay);
    REQUIRE(replay.checkpoints.size() > 10);
    CHECK_FALSE(replay.commands.empty());

    // Guardar y cargar de disco
    const auto path = std::filesystem::temp_directory_path() / "aoe_clone_test_replay.json";
    replay.save(path);
    const sim::Replay loaded = sim::Replay::load(path);
    std::filesystem::remove(path);
    CHECK(loaded.commands.size() == replay.commands.size());

    // Reproducir sin IA: solo las órdenes grabadas
    sim::GameState copy(registry(), loaded.config);
    sim::ReplayPlayer player(loaded);
    while (!player.finished(copy)) {
        const auto cmds = player.commands_for(copy.tick());
        copy.step(cmds);
        REQUIRE(player.verify(copy));
    }
    CHECK(copy.state_hash() == original.state_hash());
}

TEST_CASE("IA: sus oleadas llegan al rival y causan bajas") {
    sim::GameState g(registry(), sim::GameConfig{21, 96, 96, 2, {}});
    play_ai(g, 20 * 60 * sim::kTicksPerSecond);  // 20 minutos (o hasta que alguien gane)
    uint32_t kills = 0;
    for (data::PlayerId p = 1; p <= 2; ++p) {
        for (const auto& [type, n] : g.player(p).kills_by_type) kills += n;
    }
    MESSAGE("tick final " << g.tick() << ", bajas totales " << kills << std::string(g.is_game_over() ? ", partida terminada" : ""));
    CHECK(kills > 0);
}
