#include <doctest.h>

#include <filesystem>

#include "data/data_registry.hpp"
#include "sim/game/game_state.hpp"
#include "sim/world/pathfinder.hpp"

namespace {

const data::DataRegistry& registry() {
    static const data::DataRegistry reg = data::DataRegistry::load(std::filesystem::path(AOE_SOURCE_DIR) / "data");
    return reg;
}

}  // namespace

TEST_CASE("Datos: se cargan y las referencias cuadran") {
    const auto& reg = registry();
    CHECK(reg.find_type("villager"));
    CHECK(reg.find_type("town_center"));
    CHECK_FALSE(reg.find_type("no_existe"));
    const auto& tc = reg.sheet(reg.type("town_center"));
    CHECK(tc.stats.footprint_w == 4);
    CHECK(tc.is_building());
    CHECK(reg.sheet(reg.type("villager")).can_move());
    CHECK(reg.start().players.size() >= 2);
}

TEST_CASE("Pathfinder: rodea un muro y no corta esquinas") {
    sim::TileMap map(10, 10);
    map.add_blocker(5, 0, 1, 9);  // muro vertical con hueco abajo (y = 9)
    sim::Pathfinder pf;
    auto r = pf.find(map, {1, 1}, {8, 1});
    REQUIRE(r);
    CHECK(r->reached_goal);
    CHECK(r->tiles.back() == core::TilePos{8, 1});
    for (auto t : r->tiles) CHECK(map.is_passable(t));
    bool used_gap = false;
    for (auto t : r->tiles) used_gap = used_gap || (t.x == 5 && t.y == 9);
    CHECK(used_gap);
}

TEST_CASE("Pathfinder: destino bloqueado → tile libre más cercano") {
    sim::TileMap map(10, 10);
    map.add_blocker(4, 4, 2, 2);
    sim::Pathfinder pf;
    auto r = pf.find(map, {0, 0}, {4, 4});
    REQUIRE(r);
    CHECK(map.is_passable(r->goal));
}

TEST_CASE("Partida: se genera con bases y recursos") {
    sim::GameState game(registry(), sim::GameConfig{7, 96, 96, 2});
    for (data::PlayerId p = 1; p <= 2; ++p) {
        const auto& ps = game.player(p);
        CHECK(ps.buildings.size() >= 1);
        CHECK(ps.units.size() == 4);
        CHECK(ps.population_current == 4);
        CHECK(ps.population_cap == 10);
    }
    CHECK(game.entities().size() > 200);  // árboles, minas, bayas
}

TEST_CASE("Partida: las unidades llegan a su destino") {
    sim::GameState game(registry(), sim::GameConfig{7, 96, 96, 2});
    const auto villager = game.player(1).units.front();
    const core::Vec2F start = game.entity(villager)->position;
    const core::Vec2F target{start.x + core::Fixed::from_int(3), start.y + core::Fixed::from_int(2)};
    sim::Command cmd{0, 1, sim::CommandType::Move, {villager}, target, {}};
    game.step(std::span<const sim::Command>(&cmd, 1));
    for (int i = 0; i < 20 * 30; ++i) game.step({});
    const auto* e = game.entity(villager);
    REQUIRE(e);
    CHECK_FALSE(e->move);
    CHECK(core::length(e->position - target) < core::Fixed::from_int(2));
}

TEST_CASE("Partida: no se pueden mover unidades de otro jugador") {
    sim::GameState game(registry(), sim::GameConfig{7, 96, 96, 2});
    const auto enemy = game.player(2).units.front();
    const core::Vec2F before = game.entity(enemy)->position;
    sim::Command cmd{0, 1, sim::CommandType::Move, {enemy}, {core::Fixed::from_int(10), core::Fixed::from_int(10)}, {}};
    game.step(std::span<const sim::Command>(&cmd, 1));
    for (int i = 0; i < 40; ++i) game.step({});
    CHECK(game.entity(enemy)->position == before);
}

TEST_CASE("Determinismo: misma seed y comandos → mismo estado tick a tick") {
    sim::GameState a(registry(), sim::GameConfig{123, 96, 96, 3});
    sim::GameState b(registry(), sim::GameConfig{123, 96, 96, 3});
    CHECK(a.state_hash() == b.state_hash());

    auto units_a = a.player(1).units;
    auto units_b = b.player(1).units;
    for (int t = 0; t < 400; ++t) {
        std::vector<sim::Command> ca, cb;
        if (t % 50 == 0) {
            const core::Vec2F target{core::Fixed::from_int(20 + t / 10), core::Fixed::from_int(30 + t / 20)};
            ca.push_back({static_cast<uint32_t>(t), 1, sim::CommandType::Move, units_a, target, {}});
            cb.push_back({static_cast<uint32_t>(t), 1, sim::CommandType::Move, units_b, target, {}});
        }
        a.step(ca);
        b.step(cb);
        REQUIRE(a.state_hash() == b.state_hash());
    }

    sim::GameState c(registry(), sim::GameConfig{124, 96, 96, 3});
    CHECK(c.state_hash() != a.state_hash());
}
