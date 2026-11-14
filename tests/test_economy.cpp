#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <optional>

#include "data/data_registry.hpp"
#include "sim/game/game_state.hpp"

namespace {

const data::DataRegistry& registry() {
    static const data::DataRegistry reg = data::DataRegistry::load(std::filesystem::path(AOE_SOURCE_DIR) / "data");
    return reg;
}

using core::Fixed;

constexpr std::size_t kFood = data::index_of(data::ResourceTag::Food);
constexpr std::size_t kWood = data::index_of(data::ResourceTag::Wood);

sim::Command cmd(sim::CommandType type, std::vector<core::EntityHandle> actors) {
    sim::Command c;
    c.issuer = 1;
    c.type = type;
    c.actors = std::move(actors);
    return c;
}

void run(sim::GameState& g, int ticks) {
    for (int i = 0; i < ticks; ++i) g.step({});
}

void send(sim::GameState& g, const sim::Command& c) { g.step(std::span<const sim::Command>(&c, 1)); }

std::vector<core::EntityHandle> villagers(const sim::GameState& g) {
    std::vector<core::EntityHandle> out;
    for (auto h : g.player(1).units) {
        if (g.sheet_of(*g.entity(h)).stats.has(data::Capability::Gather)) out.push_back(h);
    }
    return out;
}

void write(const std::filesystem::path& p, const std::string& text) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

}  // namespace

TEST_CASE("Datos: extends hereda y sobrescribe; las plantillas abstractas no son tipos") {
    const auto& reg = registry();
    CHECK_FALSE(reg.find_type("_camp_base"));
    CHECK_FALSE(reg.find_type("_unit_base"));

    const auto& camp = reg.sheet(reg.type("lumber_camp"));
    CHECK(camp.stats.max_hp == 600);       // de _camp_base
    CHECK(camp.stats.footprint_w == 2);    // de _camp_base
    CHECK(camp.stats.cost[kWood] == 100);  // de _camp_base
    CHECK(camp.stats.dropoff.test(kWood)); // propio
    CHECK_FALSE(camp.stats.dropoff.test(kFood));
    CHECK(camp.visual.label == "Aserradero");            // merge de objetos: solo cambia label...
    CHECK(camp.visual.height == doctest::Approx(0.8f));  // ...y conserva height del padre
    CHECK(camp.stats.sight == 3);                        // de _building_base (dos niveles)

    const auto& villager = reg.sheet(reg.type("villager"));
    CHECK(villager.stats.population_cost == 1);  // de _unit_base
    CHECK(villager.stats.has(data::Capability::Gather));
    CHECK(villager.can_build(reg.type("house")));
    CHECK(villager.stats.train_ticks == 25u * sim::kTicksPerSecond);
    CHECK(reg.sheet(reg.type("town_center")).can_train(reg.type("villager")));
}

TEST_CASE("Datos: herencia circular y referencias rotas dan error legible") {
    const auto dir = std::filesystem::temp_directory_path() / "aoe_clone_test_data";
    std::filesystem::remove_all(dir);
    write(dir / "classes.json", R"({ "classes": ["unit"] })");
    write(dir / "start.json", R"({ "players": [ { "name": "A", "color": "#ffffff" } ] })");
    write(dir / "entities/a.json", R"({ "key": "a", "extends": "b", "kind": "unit" })");
    write(dir / "entities/b.json", R"({ "key": "b", "extends": "a", "kind": "unit" })");
    CHECK_THROWS_WITH_AS(data::DataRegistry::load(dir), doctest::Contains("herencia circular"), data::DataError);

    write(dir / "entities/a.json", R"({ "key": "a", "kind": "unit" })");
    write(dir / "entities/b.json", R"({ "key": "b", "kind": "building", "trains": ["no_existe"] })");
    CHECK_THROWS_WITH_AS(data::DataRegistry::load(dir), doctest::Contains("no_existe"), data::DataError);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Economía: recolectar bayas y depositar suma comida") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2});
    const auto vs = villagers(g);
    REQUIRE(vs.size() == 3);
    const auto berry = g.nearest_resource(data::ResourceTag::Food, g.start_position(1), 16);
    REQUIRE(berry);
    const int32_t before = g.player(1).resources[kFood];
    const int32_t berry_before = g.entity(berry)->resource_node->amount_remaining;

    auto c = cmd(sim::CommandType::Gather, vs);
    c.target = berry;
    send(g, c);
    run(g, 60 * sim::kTicksPerSecond);

    CHECK(g.player(1).resources[kFood] > before + 20);
    if (const auto* b = g.entity(berry)) CHECK(b->resource_node->amount_remaining < berry_before);
    for (auto h : vs) CHECK(g.entity(h)->action);  // siguen trabajando
}

TEST_CASE("Construcción: la casa se paga al colocarla y da población al terminar") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2});
    const auto vs = villagers(g);
    const data::TypeId house = registry().type("house");
    const core::TilePos s = core::tile_of(g.start_position(1));
    std::optional<core::TilePos> spot;
    for (int r = 4; r < 12 && !spot; ++r) {
        if (g.can_place(house, {s.x - r, s.y + 1})) spot = core::TilePos{s.x - r, s.y + 1};
    }
    REQUIRE(spot);

    const int32_t wood = g.player(1).resources[kWood];
    const int32_t cap = g.player(1).population_cap;
    auto c = cmd(sim::CommandType::PlaceBuilding, {vs[0], vs[1]});
    c.type_arg = house;
    c.tile = *spot;
    send(g, c);
    CHECK(g.player(1).resources[kWood] == wood - 25);
    CHECK_FALSE(g.can_place(house, *spot));
    CHECK(g.player(1).population_cap == cap);  // aún en obra

    // Dos constructores: 1 + 1/3 por tick → 500 ticks de trabajo / (4/3) ≈ 375 ticks + el camino
    run(g, 30 * sim::kTicksPerSecond);
    CHECK(g.player(1).population_cap == cap + 5);
    for (auto h : g.player(1).buildings) CHECK(g.entity(h)->is_complete());
}

TEST_CASE("Producción: entrenar cuesta, tarda y crea la unidad; cancelar devuelve") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2});
    const auto tc = g.player(1).buildings.front();
    const data::TypeId villager = registry().type("villager");
    const int32_t food = g.player(1).resources[kFood];
    const std::size_t units = villagers(g).size();  // las cabras de la base también son unidades: contar aldeanos
    const int32_t pop = g.player(1).population_current;

    auto train = cmd(sim::CommandType::Train, {tc});
    train.type_arg = villager;
    send(g, train);
    send(g, train);
    CHECK(g.player(1).resources[kFood] == food - 100);
    CHECK(g.entity(tc)->production.size() == 2);

    auto cancel = cmd(sim::CommandType::CancelTrain, {tc});
    cancel.index = 1;
    send(g, cancel);
    CHECK(g.player(1).resources[kFood] == food - 50);

    run(g, 25 * sim::kTicksPerSecond + 2);
    CHECK(villagers(g).size() == units + 1);
    CHECK(g.player(1).population_current == pop + 1);
    CHECK(g.entity(tc)->production.empty());
}

TEST_CASE("Producción: el punto de reunión manda la unidad nueva allí") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2});
    const auto tc = g.player(1).buildings.front();
    const core::Vec2F rally{g.start_position(1).x + Fixed::from_int(5), g.start_position(1).y + Fixed::from_int(5)};
    auto set = cmd(sim::CommandType::SetRally, {tc});
    set.target_pos = rally;
    send(g, set);
    auto train = cmd(sim::CommandType::Train, {tc});
    train.type_arg = registry().type("villager");
    send(g, train);
    run(g, 40 * sim::kTicksPerSecond);
    const auto* unit = g.entity(villagers(g).back());  // el aldeano recién entrenado
    REQUIRE(unit);
    CHECK(core::length(unit->position - rally) < Fixed::from_int(2));
}

TEST_CASE("Determinismo: economía completa, mismo hash tick a tick") {
    auto play = [](sim::GameState& g) {
        const auto vs = villagers(g);
        auto gather = cmd(sim::CommandType::Gather, {vs[0], vs[1]});
        gather.target = g.nearest_resource(data::ResourceTag::Food, g.start_position(1), 16);
        auto train = cmd(sim::CommandType::Train, {g.player(1).buildings.front()});
        train.type_arg = g.data().type("villager");
        const std::vector<sim::Command> first{gather, train};
        g.step(first);
    };
    sim::GameState a(registry(), sim::GameConfig{99, 96, 96, 2});
    sim::GameState b(registry(), sim::GameConfig{99, 96, 96, 2});
    play(a);
    play(b);
    for (int t = 0; t < 1500; ++t) {
        a.step({});
        b.step({});
        REQUIRE(a.state_hash() == b.state_hash());
    }
}
