#include <doctest.h>

#include <filesystem>
#include <optional>

#include "data/data_registry.hpp"
#include "sim/game/game_state.hpp"

namespace {

const data::DataRegistry& registry() {
    static const data::DataRegistry reg = data::DataRegistry::load(std::filesystem::path(AOE_SOURCE_DIR) / "data");
    return reg;
}

using core::Fixed;
data::TypeId T(const char* key) { return registry().type(key); }
data::TechId Tech(const char* key) { return *registry().find_tech(key); }
constexpr std::size_t kGold = data::index_of(data::ResourceTag::Gold);

sim::GameConfig config(uint32_t seed = 7) { return sim::GameConfig{seed, 96, 96, 2, {"valdoria", "tarvesh"}}; }

sim::Command cmd(sim::CommandType type, std::vector<core::EntityHandle> actors, data::PlayerId who = 1) {
    sim::Command c;
    c.issuer = who;
    c.type = type;
    c.actors = std::move(actors);
    return c;
}
void run(sim::GameState& g, int ticks) {
    for (int i = 0; i < ticks; ++i) g.step({});
}
void send(sim::GameState& g, const sim::Command& c) { g.step(std::span<const sim::Command>(&c, 1)); }

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

std::vector<core::EntityHandle> of_type(const sim::GameState& g, data::PlayerId p, data::TypeId t) {
    std::vector<core::EntityHandle> out;
    for (auto h : g.player(p).units) if (g.entity(h) && g.entity(h)->type == t) out.push_back(h);
    for (auto h : g.player(p).buildings) if (g.entity(h) && g.entity(h)->type == t) out.push_back(h);
    return out;
}

}  // namespace

TEST_CASE("Caso 1: Ingeniería de campo da a la infantería la capacidad de construir torres (y solo torres)") {
    sim::GameState g(registry(), config());
    g.debug_set_resources(1, {1000, 1000, 1000, 1000});
    const auto spot = open_area(g, base(g), 4, 3, 4);
    REQUIRE(spot);
    const auto m = g.debug_spawn(T("militia"), 1, {spot->x, spot->y});
    const auto v = g.player(1).units.front();
    CHECK_FALSE(g.has_cap(*g.entity(m), data::Capability::Build));

    g.debug_complete_research(1, Tech("field_engineering"));
    CHECK(g.has_cap(*g.entity(m), data::Capability::Build));
    CHECK(g.can_build(*g.entity(m), T("watch_tower")));
    CHECK_FALSE(g.can_build(*g.entity(m), T("house")));
    CHECK(g.can_build(*g.entity(v), T("house")));  // el aldeano sigue igual

    g.debug_complete_research(1, Tech("age_2"));  // la torre pide la Edad de la Villa
    run(g, 4);
    auto place = cmd(sim::CommandType::PlaceBuilding, {m});
    place.type_arg = T("watch_tower");
    place.tile = {spot->x + 2, spot->y + 1};
    send(g, place);
    run(g, 90 * sim::kTicksPerSecond);
    bool built = false;
    for (auto h : of_type(g, 1, T("watch_tower"))) built = built || g.entity(h)->is_complete();
    CHECK(built);
}

TEST_CASE("Caso 2: Caballería mística convierte infantería (no arqueros); el convertido conserva sus stats") {
    sim::GameState g(registry(), config());
    const auto spot = open_area(g, base(g), 6, 4, 6);
    REQUIRE(spot);
    const auto knight = g.debug_spawn(T("knight"), 1, {spot->x, spot->y + 1});
    const auto enemy = g.debug_spawn(T("militia"), 2, {spot->x + 2, spot->y + 1});
    const auto archer = g.debug_spawn(T("archer"), 2, {spot->x + 2, spot->y + 3});
    run(g, 4);
    CHECK_FALSE(g.can_convert(*g.entity(knight), *g.entity(enemy)));
    g.debug_complete_research(1, Tech("mystic_cavalry"));
    CHECK(g.can_convert(*g.entity(knight), *g.entity(enemy)));
    CHECK_FALSE(g.can_convert(*g.entity(knight), *g.entity(archer)));

    auto c = cmd(sim::CommandType::Convert, {knight});
    c.target = enemy;
    send(g, c);
    run(g, 20 * sim::kTicksPerSecond);
    const auto* e = g.entity(enemy);
    REQUIRE(e);
    CHECK(e->current_player == 1);
    CHECK(e->original_player == 2);
    // Sus stats salen de Tarvesh (el jugador original), no de Valdoria (+10 % de vida a la infantería)
    CHECK(g.stats_of(*e).max_hp == g.stats_for(2, T("militia")).max_hp);
    CHECK(g.stats_of(*e).max_hp != g.stats_for(1, T("militia")).max_hp);
}

TEST_CASE("Caso 3: el Guerrero ritual se sacrifica y en su lugar aparece una Torre del ritual") {
    sim::GameState g(registry(), config());
    const auto spot = open_area(g, base(g), 3, 3, 5);
    REQUIRE(spot);
    const auto w = g.debug_spawn(T("ritual_warrior"), 1, {spot->x + 1, spot->y + 1});
    REQUIRE(w);
    REQUIRE(g.skills_of(*g.entity(w)).size() == 1);
    auto use = cmd(sim::CommandType::UseSkill, {w});
    use.index = 0;
    send(g, use);
    run(g, 2);
    CHECK_FALSE(g.entity(w));
    const auto towers = of_type(g, 1, T("ritual_tower"));
    REQUIRE(towers.size() == 1);
    const auto* t = g.entity(towers.front());
    CHECK(t->is_complete());
    CHECK(core::tile_of(t->position) == core::TilePos{spot->x + 1, spot->y + 1});
}

TEST_CASE("Caso 4: el Caballero de la muerte cae y su jinete se levanta como hombre de armas") {
    sim::GameState g(registry(), config());
    const auto spot = open_area(g, base(g), 6, 5, 6);
    REQUIRE(spot);
    const auto dk = g.debug_spawn(T("death_knight"), 1, {spot->x + 1, spot->y + 2});
    for (int i = 0; i < 6; ++i) g.debug_spawn(T("man_at_arms"), 2, {spot->x + 4, spot->y + i % 5});
    const std::size_t before = of_type(g, 1, T("man_at_arms")).size();
    for (int t = 0; t < 60 * sim::kTicksPerSecond && g.entity(dk); ++t) g.step({});
    REQUIRE_FALSE(g.entity(dk));
    g.step({});
    const auto risen = of_type(g, 1, T("man_at_arms"));
    REQUIRE(risen.size() == before + 1);
    const auto* r = g.entity(risen.back());
    CHECK(r->original_player == 1);
    CHECK(r->hp <= g.stats_for(1, T("man_at_arms")).max_hp / 2 + 1);
}

TEST_CASE("Caso 5 (solo datos): Botín da oro por cada muerte causada por la infantería") {
    sim::GameState g(registry(), config());
    const auto spot = open_area(g, base(g), 5, 4, 15);
    REQUIRE(spot);
    g.debug_complete_research(1, Tech("plunder"));
    for (int i = 0; i < 3; ++i) g.debug_spawn(T("militia"), 1, {spot->x, spot->y + i});
    const auto victim = g.debug_spawn(T("villager"), 2, {spot->x + 2, spot->y + 1});
    const int32_t gold = g.player(1).resources[kGold];
    for (int t = 0; t < 30 * sim::kTicksPerSecond && g.entity(victim); ++t) g.step({});
    REQUIRE_FALSE(g.entity(victim));
    CHECK(g.player(1).resources[kGold] == gold + 5);
}

TEST_CASE("Monje: convierte enemigos y cura sin que se lo manden") {
    sim::GameState g(registry(), config());
    const auto spot = open_area(g, base(g), 8, 4, 15);
    REQUIRE(spot);
    const auto monk = g.debug_spawn(T("monk"), 1, {spot->x, spot->y + 1});
    const auto enemy = g.debug_spawn(T("villager"), 2, {spot->x + 5, spot->y + 1});
    run(g, 4);
    auto c = cmd(sim::CommandType::Convert, {monk});
    c.target = enemy;
    send(g, c);
    run(g, 15 * sim::kTicksPerSecond);
    CHECK(g.entity(enemy)->current_player == 1);

    // Un herido propio cerca: el monje ocioso lo cura
    const auto hurt = g.debug_spawn(T("militia"), 1, {spot->x + 1, spot->y + 2});
    g.entity_mut(hurt)->hp = 10;
    run(g, 12 * sim::kTicksPerSecond);
    CHECK(g.entity(hurt)->hp > 20);
}

TEST_CASE("Guarnición: los aldeanos entran, desaparecen del mapa, suman flechas y vuelven a salir") {
    sim::GameState g(registry(), config());
    const auto tc = g.player(1).buildings.front();
    std::vector<core::EntityHandle> vs;
    for (auto h : g.player(1).units) {
        if (g.has_cap(*g.entity(h), data::Capability::Gather)) vs.push_back(h);
    }
    REQUIRE(vs.size() == 3);
    auto in = cmd(sim::CommandType::Garrison, vs);
    in.target = tc;
    send(g, in);
    run(g, 20 * sim::kTicksPerSecond);
    for (auto h : vs) CHECK(g.entity(h)->garrisoned_in == tc);
    CHECK(g.entity(tc)->garrison.size() == 3);
    std::vector<core::EntityHandle> near;
    const auto p = g.entity(tc)->position;
    g.spatial().query_rect({p.x - Fixed::from_int(4), p.y - Fixed::from_int(4)}, {p.x + Fixed::from_int(4), p.y + Fixed::from_int(4)}, near);
    for (auto h : vs) CHECK(std::find(near.begin(), near.end(), h) == near.end());  // no están en el mapa

    // Un enemigo a tiro: la andanada lleva 1 + 3 flechas
    const auto enemy = g.debug_spawn(T("militia"), 2, {core::tile_of(p).x + 5, core::tile_of(p).y});
    REQUIRE(enemy);
    std::size_t max_arrows = 0;
    for (int t = 0; t < 4 * sim::kTicksPerSecond; ++t) {
        g.step({});
        std::size_t arrows = 0;
        g.entities().for_each([&](core::EntityHandle, const sim::SpawnableEntity& e) { arrows += e.projectile.has_value(); });
        max_arrows = std::max(max_arrows, arrows);
    }
    CHECK(max_arrows >= 4);

    send(g, cmd(sim::CommandType::Ungarrison, {tc}));
    for (auto h : vs) CHECK_FALSE(g.entity(h)->garrisoned_in);
    CHECK(g.entity(tc)->garrison.empty());
}

TEST_CASE("Capabilities dinámicas: un modificador puede quitar lo que da la ficha") {
    sim::GameState g(registry(), config());
    const auto v = g.player(1).units.front();
    CHECK(g.has_cap(*g.entity(v), data::Capability::Gather));
    data::Modifier strike;
    strike.id = "huelga";
    strike.target_type = T("villager");
    data::ObjectModifier om;
    om.remove = data::Capability::Gather;
    strike.object_mods.push_back(om);
    g.debug_apply_modifier(1, strike);
    CHECK_FALSE(g.has_cap(*g.entity(v), data::Capability::Gather));
    CHECK(g.has_cap(*g.entity(g.player(2).units.front()), data::Capability::Gather));  // solo al jugador 1

    const auto berry = g.nearest_resource(data::ResourceTag::Food, g.start_position(1), 16);
    auto c = cmd(sim::CommandType::Gather, {v});
    c.target = berry;
    send(g, c);
    CHECK((!g.entity(v)->action || g.entity(v)->action->kind() != sim::ActionKind::Gather));
}

TEST_CASE("Habilidades: Grito de guerra sube el ataque un rato; luego vuelve y hay que esperar el enfriamiento") {
    sim::GameState g(registry(), config());
    const auto spot = open_area(g, base(g), 3, 3, 5);
    REQUIRE(spot);
    const auto guard = g.debug_spawn(T("valdorian_guard"), 1, *spot);
    const auto melee = *registry().find_armor_class("melee");
    const int32_t base_attack = g.stats_of(*g.entity(guard)).attack_vs(melee);
    auto use = cmd(sim::CommandType::UseSkill, {guard});
    use.index = 0;
    send(g, use);
    CHECK(g.stats_of(*g.entity(guard)).attack_vs(melee) == base_attack + 3);
    CHECK(g.skill_cooldown(*g.entity(guard), "battle_cry") > 0);
    run(g, 11 * sim::kTicksPerSecond);
    CHECK(g.stats_of(*g.entity(guard)).attack_vs(melee) == base_attack);
}

TEST_CASE("Determinismo: conversiones (con su azar) y triggers, mismo hash tick a tick") {
    auto setup = [](sim::GameState& g) {
        const auto spot = open_area(g, base(g), 10, 5, 15);
        g.debug_complete_research(1, Tech("plunder"));
        for (int i = 0; i < 2; ++i) g.debug_spawn(T("monk"), 1, {spot->x, spot->y + i});
        for (int i = 0; i < 3; ++i) g.debug_spawn(T("militia"), 1, {spot->x + 1, spot->y + i});
        for (int i = 0; i < 3; ++i) g.debug_spawn(T("death_knight"), 2, {spot->x + 6, spot->y + i});
    };
    sim::GameState a(registry(), config(5));
    sim::GameState b(registry(), config(5));
    setup(a);
    setup(b);
    for (int t = 0; t < 1200; ++t) {
        a.step({});
        b.step({});
        REQUIRE(a.state_hash() == b.state_hash());
    }
}
