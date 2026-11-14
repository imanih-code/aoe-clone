#include <doctest.h>

#include <filesystem>
#include <optional>
#include <set>
#include <utility>

#include "data/data_registry.hpp"
#include "sim/game/game_state.hpp"

namespace {

const data::DataRegistry& registry() {
    static const data::DataRegistry reg = data::DataRegistry::load(std::filesystem::path(AOE_SOURCE_DIR) / "data");
    return reg;
}

using core::Fixed;
data::TypeId T(const char* key) { return registry().type(key); }
constexpr std::size_t kFood = data::index_of(data::ResourceTag::Food);

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

// Zona de tierra libre de w×h tiles cerca de `near` (para montar cada escenario)
std::optional<core::TilePos> open_area(const sim::GameState& g, core::TilePos near, int w, int h, int min_r = 0) {
    for (int r = min_r; r < 30; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const core::TilePos t{near.x + dx, near.y + dy};
                if (g.map().is_area_free(t.x, t.y, w, h)) return t;
            }
        }
    }
    return std::nullopt;
}

core::TilePos base(const sim::GameState& g) { return core::tile_of(g.start_position(1)); }

bool is_gathering(const sim::SpawnableEntity* e) {
    return e && e->action && e->action->kind() == sim::ActionKind::Gather && static_cast<const sim::GatherAction&>(*e->action).is_gathering();
}

core::EntityHandle find_type(const sim::GameState& g, data::TypeId t) {
    core::EntityHandle out;
    g.entities().for_each([&](core::EntityHandle h, const sim::SpawnableEntity& e) {
        if (!out && e.type == t) out = h;
    });
    return out;
}

}  // namespace

TEST_CASE("Granja: el granjero camina por el campo de casilla en casilla; un solo granjero") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto spot = open_area(g, base(g), 3, 3, 4);
    REQUIRE(spot);
    const auto farm = g.debug_spawn(T("farm"), 1, *spot);
    REQUIRE(farm);
    CHECK(g.map().is_passable(spot->x + 1, spot->y + 1));  // transitable
    CHECK_FALSE(g.can_place(T("house"), *spot));           // pero no se construye encima

    const auto a = g.debug_spawn(T("villager"), 1, {spot->x - 1, spot->y});
    const auto b = g.debug_spawn(T("villager"), 1, {spot->x - 1, spot->y + 1});
    run(g, 4);
    auto c = cmd(sim::CommandType::Gather, {a, b});
    c.target = farm;
    send(g, c);

    std::set<std::pair<int, int>> tiles;
    int max_farmers = 0;
    for (int t = 0; t < 30 * sim::kTicksPerSecond; ++t) {
        g.step({});
        int farmers = 0;
        for (auto h : {a, b}) {
            const auto* e = g.entity(h);
            // Solo cuenta si trabaja ESTA granja (el que no cabe se va, con razón, a otra fuente de comida)
            if (is_gathering(e) && static_cast<const sim::GatherAction&>(*e->action).target() == farm) {
                ++farmers;
                const auto tile = core::tile_of(e->position);
                CHECK(tile.x >= spot->x);  // trabaja DENTRO del campo
                CHECK(tile.x < spot->x + 3);
                tiles.insert({tile.x, tile.y});
            }
        }
        max_farmers = std::max(max_farmers, farmers);
    }
    CHECK(tiles.size() >= 2);   // cambió de casilla
    CHECK(max_farmers == 1);    // nunca dos a la vez en la misma granja
}

TEST_CASE("Plazas: si un recurso está lleno, el aldeano va a otro cercano") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto spot = open_area(g, base(g), 6, 3, 5);
    REQUIRE(spot);
    const auto t1 = g.debug_spawn(T("tree"), data::kGaia, {spot->x + 2, spot->y + 1});
    const auto t2 = g.debug_spawn(T("tree"), data::kGaia, {spot->x + 4, spot->y + 1});
    REQUIRE(t1);
    REQUIRE(t2);
    std::vector<core::EntityHandle> vs;
    for (int i = 0; i < 4; ++i) vs.push_back(g.debug_spawn(T("villager"), 1, {spot->x, spot->y + i % 3}));
    run(g, 4);
    auto c = cmd(sim::CommandType::Gather, vs);
    c.target = t1;
    send(g, c);
    run(g, 15 * sim::kTicksPerSecond);
    CHECK(g.gatherers_on(t1) <= 2);  // el árbol admite 2
    int on_other = 0;
    for (auto h : vs) {
        const auto* e = g.entity(h);
        if (e && e->action && e->action->kind() == sim::ActionKind::Gather &&
            static_cast<const sim::GatherAction&>(*e->action).target() != t1) {
            ++on_other;
        }
    }
    CHECK(on_other >= 1);
}

TEST_CASE("Caza: el aldeano abate al ciervo y recolecta la carne") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto spot = open_area(g, base(g), 6, 4, 5);
    REQUIRE(spot);
    const auto deer = g.debug_spawn(T("deer"), data::kGaia, {spot->x + 4, spot->y + 1});
    const auto v = g.debug_spawn(T("villager"), 1, {spot->x, spot->y + 1});
    run(g, 4);
    auto c = cmd(sim::CommandType::Gather, {v});
    c.target = deer;
    send(g, c);
    bool saw_carcass = false;
    for (int t = 0; t < 60 * sim::kTicksPerSecond; ++t) {
        g.step({});
        saw_carcass = saw_carcass || static_cast<bool>(find_type(g, T("carcass")));
    }
    CHECK_FALSE(g.entity(deer));
    CHECK(saw_carcass);
    CHECK(g.player(1).kills_by_type.at(T("deer")) == 1);
    const auto* e = g.entity(v);
    REQUIRE(e);
    CHECK((e->carry.amount > Fixed{} || g.player(1).resources[kFood] > 200));
}

TEST_CASE("Jabalí: neutral hasta que lo hieren; entonces se defiende") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto spot = open_area(g, base(g), 6, 4, 5);
    REQUIRE(spot);
    const auto boar = g.debug_spawn(T("boar"), data::kGaia, {spot->x + 2, spot->y + 1});
    const auto v = g.debug_spawn(T("villager"), 1, {spot->x, spot->y + 1});
    run(g, 5 * sim::kTicksPerSecond);
    CHECK(g.entity(v)->hp == g.stats_of(*g.entity(v)).max_hp);  // a 2 tiles y no ataca
    auto c = cmd(sim::CommandType::Gather, {v});
    c.target = boar;
    send(g, c);
    run(g, 6 * sim::kTicksPerSecond);
    const auto* e = g.entity(v);
    CHECK((!e || e->hp < g.stats_for(1, T("villager")).max_hp));  // se revolvió
}

TEST_CASE("Lobo: ataca sin que lo provoquen y no deja comida") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto spot = open_area(g, base(g), 6, 4, 5);
    REQUIRE(spot);
    const auto v = g.debug_spawn(T("villager"), 1, {spot->x, spot->y + 1});
    g.debug_spawn(T("wolf"), data::kGaia, {spot->x + 3, spot->y + 1});
    run(g, 8 * sim::kTicksPerSecond);
    const auto* e = g.entity(v);
    CHECK((!e || e->hp < g.stats_for(1, T("villager")).max_hp));

    // Unas tropas lo matan solas (es hostil) y no aparece carne
    const auto spot2 = open_area(g, base(g), 6, 4, 14);
    REQUIRE(spot2);
    for (int i = 0; i < 3; ++i) g.debug_spawn(T("militia"), 1, {spot2->x, spot2->y + i});
    const auto wolf2 = g.debug_spawn(T("wolf"), data::kGaia, {spot2->x + 3, spot2->y + 1});
    const std::size_t carcasses_before = [&] {
        std::size_t n = 0;
        g.entities().for_each([&](core::EntityHandle, const sim::SpawnableEntity& x) { n += x.type == T("carcass"); });
        return n;
    }();
    run(g, 20 * sim::kTicksPerSecond);
    CHECK_FALSE(g.entity(wolf2));
    std::size_t carcasses = 0;
    g.entities().for_each([&](core::EntityHandle, const sim::SpawnableEntity& x) { carcasses += x.type == T("carcass"); });
    CHECK(carcasses == carcasses_before);
}

TEST_CASE("Ganado: la cabra pasa al primer jugador que llega; el dueño la puede llevar") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto spot = open_area(g, base(g), 4, 3, 15);
    REQUIRE(spot);
    const auto goat = g.debug_spawn(T("goat"), data::kGaia, {spot->x + 2, spot->y + 1});
    CHECK(g.entity(goat)->current_player == data::kGaia);
    g.debug_spawn(T("villager"), 1, {spot->x, spot->y + 1});
    run(g, 8);
    CHECK(g.entity(goat)->current_player == 1);
    auto move = cmd(sim::CommandType::Move, {goat});
    move.target_pos = g.start_position(1);
    send(g, move);
    run(g, 2);
    CHECK(g.entity(goat)->action);
    CHECK_FALSE(g.player(1).is_defeated);
}

TEST_CASE("Niebla: lo no explorado se supone transitable; al descubrir el muro, replanifica y llega") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto spot = open_area(g, base(g), 12, 9, 16);
    REQUIRE(spot);
    // Un muro de árboles (sin explorar) entre el explorador y su destino, con un hueco abajo
    for (int y = 0; y < 8; ++y) g.debug_spawn(T("tree"), data::kGaia, {spot->x + 6, spot->y + y});
    const auto scout_spot = core::TilePos{spot->x + 1, spot->y + 3};
    const auto unit = g.debug_spawn(T("villager"), 1, scout_spot);
    const core::Vec2F goal = core::tile_center({spot->x + 11, spot->y + 3});
    REQUIRE_FALSE(g.visibility(1).explored(spot->x + 6, spot->y + 3));

    const auto plan = g.plan_path(*g.entity(unit), goal);
    REQUIRE(plan);
    CHECK(plan->waypoints.size() == 1);  // cree que puede ir en línea recta: no sabe que hay árboles

    auto c = cmd(sim::CommandType::Move, {unit});
    c.target_pos = goal;
    send(g, c);
    run(g, 40 * sim::kTicksPerSecond);
    CHECK(core::length(g.entity(unit)->position - goal) < Fixed::from_int(1));
}

TEST_CASE("Iniciativa: tras construir un depósito o una granja, el aldeano se pone a trabajar") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    g.debug_set_resources(1, {500, 500, 500, 500});
    const auto spot = open_area(g, base(g), 6, 3, 3);
    REQUIRE(spot);
    for (int y = 0; y < 3; ++y) g.debug_spawn(T("tree"), data::kGaia, {spot->x + 3, spot->y + y});
    const auto v = g.debug_spawn(T("villager"), 1, {spot->x, spot->y + 2});
    run(g, 4);
    auto place = cmd(sim::CommandType::PlaceBuilding, {v});
    place.type_arg = T("lumber_camp");
    place.tile = {spot->x + 1, spot->y};
    send(g, place);
    run(g, 45 * sim::kTicksPerSecond);
    const auto* e = g.entity(v);
    REQUIRE(e);
    REQUIRE(e->action);
    CHECK(e->action->kind() == sim::ActionKind::Gather);
    CHECK(static_cast<const sim::GatherAction&>(*e->action).resource() == data::ResourceTag::Wood);

    // Granja: quien la construye la trabaja
    const auto farm_spot = open_area(g, base(g), 3, 3, 4);
    REQUIRE(farm_spot);
    const auto f = g.debug_spawn(T("villager"), 1, {farm_spot->x - 1, farm_spot->y});
    run(g, 4);
    auto farm = cmd(sim::CommandType::PlaceBuilding, {f});
    farm.type_arg = T("farm");
    farm.tile = *farm_spot;
    send(g, farm);
    run(g, 25 * sim::kTicksPerSecond);
    CHECK(is_gathering(g.entity(f)));
}

TEST_CASE("Cola con Shift: las órdenes se cumplen en orden de llegada") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    g.debug_set_resources(1, {500, 500, 500, 500});
    // Dos solares para casas, explorados (a la vista de la plaza mayor) y separados
    std::vector<core::TilePos> lots;
    const core::TilePos c = base(g);
    for (int r = 3; r < 10 && lots.size() < 2; ++r) {
        for (int dy = -r; dy <= r && lots.size() < 2; ++dy) {
            for (int dx = -r; dx <= r && lots.size() < 2; ++dx) {
                const core::TilePos t{c.x + dx, c.y + dy};
                if (!g.map().is_area_free(t.x - 1, t.y - 1, 4, 4) || !g.visibility(1).explored(t) ||
                    !g.visibility(1).explored({t.x + 1, t.y + 1})) {
                    continue;
                }
                const bool apart = lots.empty() || std::abs(lots[0].x - t.x) + std::abs(lots[0].y - t.y) >= 5;
                if (apart) lots.push_back(t);
            }
        }
    }
    REQUIRE(lots.size() == 2);
    const auto v = g.debug_spawn(T("villager"), 1, c);
    run(g, 4);
    const core::Vec2F a = core::tile_center({lots[0].x - 1, lots[0].y - 1});
    const core::Vec2F b = core::tile_center({lots[1].x - 1, lots[1].y - 1});
    auto m1 = cmd(sim::CommandType::Move, {v});
    m1.target_pos = a;
    auto m2 = m1;
    m2.target_pos = b;
    m2.queued = true;
    // Dos casas encoladas tras el paseo
    auto h1 = cmd(sim::CommandType::PlaceBuilding, {v});
    h1.type_arg = T("house");
    h1.tile = lots[0];
    h1.queued = true;
    auto h2 = h1;
    h2.tile = lots[1];
    const std::vector<sim::Command> orders{m1, m2, h1, h2};
    g.step(orders);
    CHECK(g.entity(v)->orders.size() == 3);

    bool reached_a = false, reached_b_after_a = false;
    for (int t = 0; t < 120 * sim::kTicksPerSecond; ++t) {
        g.step({});
        const auto p = g.entity(v)->position;
        if (core::length(p - a) < Fixed::ratio(1, 2)) reached_a = true;
        if (reached_a && core::length(p - b) < Fixed::ratio(1, 2)) reached_b_after_a = true;
    }
    CHECK(reached_a);
    CHECK(reached_b_after_a);
    int houses_done = 0;
    for (auto h : g.player(1).buildings) {
        const auto* b2 = g.entity(h);
        houses_done += b2 && b2->type == T("house") && b2->is_complete() && (b2->anchor == lots[0] || b2->anchor == lots[1]);
    }
    CHECK(houses_done == 2);
}

TEST_CASE("Agua: los aldeanos pescan en la orilla y las barcas en aguas profundas") {
    sim::GameState g(registry(), sim::GameConfig{7, 96, 96, 2, {}});
    const auto shore = find_type(g, T("shore_fish"));
    const auto deep = find_type(g, T("deep_fish"));
    REQUIRE(shore);
    REQUIRE(deep);

    // Aldeano junto a la orilla
    const auto fish_tile = g.entity(shore)->anchor;
    const auto v = g.debug_spawn(T("villager"), 1, fish_tile);  // aparece en el tile de tierra más cercano
    REQUIRE(v);
    run(g, 4);
    auto c = cmd(sim::CommandType::Gather, {v});
    c.target = shore;
    send(g, c);
    run(g, 30 * sim::kTicksPerSecond);
    CHECK(g.entity(v)->carry.amount > Fixed{});

    // Muelle en la orilla más cercana al banco profundo, y una barca
    const auto deep_tile = g.entity(deep)->anchor;
    std::optional<core::TilePos> dock_spot;
    for (int r = 1; r < 25 && !dock_spot; ++r) {
        for (int dy = -r; dy <= r && !dock_spot; ++dy) {
            for (int dx = -r; dx <= r && !dock_spot; ++dx) {
                const core::TilePos t{deep_tile.x + dx, deep_tile.y + dy};
                if (g.can_place(T("dock"), t)) dock_spot = t;
            }
        }
    }
    REQUIRE(dock_spot);
    REQUIRE(g.debug_spawn(T("dock"), 1, *dock_spot));
    const auto boat = g.debug_spawn(T("fishing_boat"), 1, deep_tile);
    REQUIRE(boat);
    CHECK(g.map().is_water(core::tile_of(g.entity(boat)->position).x, core::tile_of(g.entity(boat)->position).y));
    const int32_t food = g.player(1).resources[kFood];
    run(g, 4);
    auto fish = cmd(sim::CommandType::Gather, {boat});
    fish.target = deep;
    send(g, fish);
    run(g, 90 * sim::kTicksPerSecond);
    CHECK(g.player(1).resources[kFood] > food);  // pescó y descargó en el muelle
}
