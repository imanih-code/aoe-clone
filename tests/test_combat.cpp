#include <doctest.h>

#include <filesystem>
#include <optional>

#include "data/data_registry.hpp"
#include "sim/game/game_state.hpp"
#include "sim/modifiers/modifier_resolver.hpp"

namespace {

const data::DataRegistry& registry() {
    static const data::DataRegistry reg = data::DataRegistry::load(std::filesystem::path(AOE_SOURCE_DIR) / "data");
    return reg;
}

using core::Fixed;

data::TypeId T(const char* key) { return registry().type(key); }
data::TechId Tech(const char* key) { return *registry().find_tech(key); }

// Jugador 1 = Valdoria, jugador 2 = Tarvesh (data/start.json)
sim::GameConfig config(uint32_t seed = 7) { return sim::GameConfig{seed, 96, 96, 2, {"valdoria", "tarvesh"}}; }

void run(sim::GameState& g, int ticks) {
    for (int i = 0; i < ticks; ++i) g.step({});
}

void send(sim::GameState& g, const sim::Command& c) { g.step(std::span<const sim::Command>(&c, 1)); }

// Un sitio libre lejos de las bases para que las plazas mayores no intervengan
core::TilePos battlefield(const sim::GameState& g) {
    const core::Vec2F a = g.start_position(1), b = g.start_position(2);
    // Punto medio desplazado en perpendicular, para alejarse también del centro
    const int mx = (a.x.floor_int() + b.x.floor_int()) / 2;
    const int my = (a.y.floor_int() + b.y.floor_int()) / 2;
    for (int r = 0; r < 20; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                bool free = true;
                for (int y = -3; y <= 3 && free; ++y)
                    for (int x = -3; x <= 3 && free; ++x) free = g.map().is_passable(mx + dx + x, my + dy + y);
                if (free) return {mx + dx, my + dy};
            }
        }
    }
    return {mx, my};
}

int count_alive(const sim::GameState& g, data::PlayerId p, data::TypeId t) {
    int n = 0;
    for (auto h : g.player(p).units) {
        const auto* e = g.entity(h);
        if (e && e->type == t && e->hp > 0) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("Daño: cuadra con la hoja de cálculo (architecture.md §4.5)") {
    const auto& reg = registry();
    const auto& militia = reg.sheet(T("militia")).stats;
    const auto& knight = reg.sheet(T("knight")).stats;
    const auto& spear = reg.sheet(T("spearman")).stats;
    const auto& archer = reg.sheet(T("archer")).stats;
    const auto& house = reg.sheet(T("house")).stats;

    CHECK(data::compute_damage(militia, militia) == 4);     // 4 melee - 0
    CHECK(data::compute_damage(militia, knight) == 2);      // 4 - 2
    CHECK(data::compute_damage(spear, knight) == 16);       // (3-2) + (15-0) bonus vs caballería
    CHECK(data::compute_damage(spear, militia) == 3);       // el bonus no cuenta: la milicia no tiene armadura "cavalry"
    CHECK(data::compute_damage(archer, militia) == 3);      // 4 pierce - 1
    CHECK(data::compute_damage(archer, house) == 1);        // 4 - 7 → mínimo 1
    CHECK(data::compute_damage(militia, house) == 4);       // (4-2) + (2-0) vs edificio
}

TEST_CASE("Modificadores: el orden de aplicación no cambia el resultado") {
    data::Modifier add2{"a", std::nullopt, {}, {{data::StatTag::MaxHp, 0, data::ModifierOp::AddFlat, Fixed::from_int(2), 0}}};
    data::Modifier pct50{"b", std::nullopt, {}, {{data::StatTag::MaxHp, 0, data::ModifierOp::AddPercent, Fixed::from_int(50), 0}}};
    data::Modifier mul2{"c", std::nullopt, {}, {{data::StatTag::MaxHp, 0, data::ModifierOp::MultiplyFlat, Fixed::from_int(2), 0}}};
    const data::ClassSet none;
    const std::vector<data::Modifier> abc{add2, pct50, mul2}, cba{mul2, pct50, add2};
    // (10 + 2) × 1.5 × 2 = 36
    CHECK(sim::resolve_stat(Fixed::from_int(10), data::StatTag::MaxHp, 0, abc, {}, none) == Fixed::from_int(36));
    CHECK(sim::resolve_stat(Fixed::from_int(10), data::StatTag::MaxHp, 0, cba, {}, none) == Fixed::from_int(36));

    data::Modifier set_low{"x", std::nullopt, {}, {{data::StatTag::MaxHp, 0, data::ModifierOp::SetValue, Fixed::from_int(5), 1}}};
    data::Modifier set_high{"y", std::nullopt, {}, {{data::StatTag::MaxHp, 0, data::ModifierOp::SetValue, Fixed::from_int(7), 2}}};
    const std::vector<data::Modifier> s1{set_low, set_high, add2}, s2{set_high, add2, set_low};
    CHECK(sim::resolve_stat(Fixed::from_int(10), data::StatTag::MaxHp, 0, s1, {}, none) == Fixed::from_int(7));
    CHECK(sim::resolve_stat(Fixed::from_int(10), data::StatTag::MaxHp, 0, s2, {}, none) == Fixed::from_int(7));
}

TEST_CASE("Tecnologías: solo afectan al jugador que las investiga y a las clases indicadas") {
    sim::GameState g(registry(), config());
    const auto melee = *registry().find_armor_class("melee");
    CHECK(g.stats_for(1, T("militia")).attack_vs(melee) == 4);
    g.debug_complete_research(1, Tech("forge_blades"));
    CHECK(g.stats_for(1, T("militia")).attack_vs(melee) == 5);
    CHECK(g.stats_for(1, T("knight")).attack_vs(melee) == 11);
    CHECK(g.stats_for(2, T("militia")).attack_vs(melee) == 4);   // el otro jugador no
    CHECK(g.stats_for(1, T("archer")).attack_vs(melee) == 0);    // los arqueros no son infantería ni caballería

    // Recolección: +20 % de madera solo para aldeanos (Valdoria ya tenía +10 %)
    const auto wood = data::index_of(data::ResourceTag::Wood);
    const Fixed before = g.stats_for(1, T("villager")).gather_per_tick[wood];
    g.debug_complete_research(1, Tech("double_axe"));
    const Fixed after = g.stats_for(1, T("villager")).gather_per_tick[wood];
    CHECK(after > before);
    const Fixed base = registry().sheet(T("villager")).stats.gather_per_tick[wood];
    CHECK((after - base * Fixed::ratio(13, 10)).raw <= 2);  // base × (1 + 10% + 20%), con redondeo de punto fijo
}

TEST_CASE("Civilizaciones: la misma unidad tiene stats distintos según la civ") {
    sim::GameState g(registry(), config());
    CHECK(g.player(1).civ->key == "valdoria");
    CHECK(g.player(2).civ->key == "tarvesh");
    CHECK(g.stats_for(1, T("knight")).max_hp == 100);
    CHECK(g.stats_for(2, T("knight")).max_hp == 120);             // Tarvesh: caballería +20 %
    CHECK(g.stats_for(2, T("scout")).move_speed > g.stats_for(1, T("scout")).move_speed);
    CHECK(g.stats_for(1, T("militia")).max_hp == 44);             // Valdoria: infantería +10 %

    // Unidades únicas y tecnologías desactivadas
    const auto barracks_p1 = g.trainable_at(1, T("barracks"));
    const auto barracks_p2 = g.trainable_at(2, T("barracks"));
    CHECK(std::find(barracks_p1.begin(), barracks_p1.end(), T("valdorian_guard")) != barracks_p1.end());
    CHECK(std::find(barracks_p2.begin(), barracks_p2.end(), T("valdorian_guard")) == barracks_p2.end());
    const auto stable_p2 = g.trainable_at(2, T("stable"));
    CHECK(std::find(stable_p2.begin(), stable_p2.end(), T("jade_raider")) != stable_p2.end());
    CHECK(g.tech_status(2, Tech("crossbow")) == sim::TechStatus::Disabled);

    sim::GameState o(registry(), sim::GameConfig{7, 96, 96, 2, {"ostreva", "valdoria"}});
    const auto stable_o = o.trainable_at(1, T("stable"));
    CHECK(std::find(stable_o.begin(), stable_o.end(), T("knight")) == stable_o.end());  // Ostreva: sin caballeros
    CHECK(o.stats_for(1, T("archer")).range == o.stats_for(2, T("archer")).range + Fixed::from_int(1));
}

TEST_CASE("Líneas de mejora: las unidades existentes y lo que se entrena cambian de tipo") {
    sim::GameState g(registry(), config());
    const auto spot = battlefield(g);
    const auto m = g.debug_spawn(T("militia"), 1, spot);
    REQUIRE(m);
    const auto barracks = g.trainable_at(1, T("barracks"));
    CHECK(std::find(barracks.begin(), barracks.end(), T("militia")) != barracks.end());

    g.debug_complete_research(1, Tech("veteran_infantry"));
    CHECK(g.entity(m)->type == T("man_at_arms"));
    CHECK(g.entity(m)->hp == g.stats_of(*g.entity(m)).max_hp);  // conserva el % de vida
    const auto after = g.trainable_at(1, T("barracks"));
    CHECK(std::find(after.begin(), after.end(), T("militia")) == after.end());
    CHECK(std::find(after.begin(), after.end(), T("man_at_arms")) != after.end());
    // El otro jugador sigue con milicianos
    const auto other = g.trainable_at(2, T("barracks"));
    CHECK(std::find(other.begin(), other.end(), T("militia")) != other.end());
}

TEST_CASE("Edades: investigar la edad desbloquea unidades") {
    sim::GameState g(registry(), config());
    g.debug_set_resources(1, {2000, 2000, 2000, 2000});
    core::TilePos spot = battlefield(g);
    const auto range = g.debug_spawn(T("archery_range"), 1, {spot.x - 1, spot.y - 1});
    REQUIRE(range);

    sim::Command train;
    train.issuer = 1;
    train.type = sim::CommandType::Train;
    train.actors = {range};
    train.type_arg = T("archer");
    send(g, train);
    CHECK(g.entity(range)->production.empty());  // edad 0: el arquero requiere la edad 1

    CHECK(g.tech_status(1, Tech("age_3")) == sim::TechStatus::NeedsAge);
    sim::Command research;
    research.issuer = 1;
    research.type = sim::CommandType::Research;
    research.actors = {g.player(1).buildings.front()};
    research.index = Tech("age_2");
    send(g, research);
    CHECK(g.tech_status(1, Tech("age_2")) == sim::TechStatus::Queued);
    CHECK(g.player(1).resources[0] == 1500);
    run(g, 131 * sim::kTicksPerSecond);
    CHECK(g.player(1).age == 1);
    CHECK(g.tech_status(1, Tech("age_2")) == sim::TechStatus::Researched);

    send(g, train);
    CHECK(g.entity(range)->production.size() == 1);
}

TEST_CASE("Combate: las tecnologías deciden una batalla igualada, siempre igual") {
    auto battle = [](bool upgrades) {
        sim::GameState g(registry(), config(11));
        if (upgrades) {
            g.debug_complete_research(1, Tech("forge_blades"));
            g.debug_complete_research(1, Tech("forge_mail"));
        }
        const auto spot = battlefield(g);
        for (int i = 0; i < 4; ++i) {
            g.debug_spawn(T("militia"), 1, {spot.x - 2, spot.y + i - 2});
            g.debug_spawn(T("militia"), 2, {spot.x + 2, spot.y + i - 2});
        }
        run(g, 60 * sim::kTicksPerSecond);  // son agresivas: se buscan solas
        return std::pair{count_alive(g, 1, T("militia")), count_alive(g, 2, T("militia"))};
    };
    const auto [mine, theirs] = battle(true);
    CHECK(mine > 0);
    CHECK(theirs == 0);
    CHECK(battle(true) == std::pair{mine, theirs});  // determinista
}

TEST_CASE("Combate: las muertes se registran por tipo") {
    sim::GameState g(registry(), config(11));
    g.debug_complete_research(1, Tech("forge_blades"));
    const auto spot = battlefield(g);
    for (int i = 0; i < 3; ++i) g.debug_spawn(T("militia"), 1, {spot.x - 1, spot.y + i});
    g.debug_spawn(T("villager"), 2, {spot.x + 1, spot.y});
    run(g, 30 * sim::kTicksPerSecond);
    CHECK(g.player(1).kills_by_type.at(T("villager")) == 1);
    CHECK(g.player(2).losses_by_type.at(T("villager")) == 1);
}

TEST_CASE("Proyectiles: el arquero dispara flechas que existen como entidades") {
    sim::GameState g(registry(), config(11));
    const auto spot = battlefield(g);
    const auto archer = g.debug_spawn(T("archer"), 1, {spot.x - 3, spot.y});
    const auto target = g.debug_spawn(T("villager"), 2, {spot.x + 1, spot.y});
    REQUIRE(archer);
    REQUIRE(target);
    sim::Command attack;
    attack.issuer = 1;
    attack.type = sim::CommandType::Attack;
    attack.actors = {archer};
    attack.target = target;
    send(g, attack);

    bool saw_arrow = false;
    for (int t = 0; t < 40 * sim::kTicksPerSecond && g.entity(target); ++t) {
        g.step({});
        g.entities().for_each([&](core::EntityHandle, const sim::SpawnableEntity& e) { saw_arrow = saw_arrow || e.projectile.has_value(); });
    }
    CHECK(saw_arrow);
    CHECK_FALSE(g.entity(target));  // el aldeano no huye: acaba muerto
}

TEST_CASE("Torres: disparan solas a los enemigos que entran en alcance") {
    sim::GameState g(registry(), config(11));
    const auto spot = battlefield(g);
    const auto tower = g.debug_spawn(T("watch_tower"), 1, spot);
    REQUIRE(tower);
    const auto scout = g.debug_spawn(T("scout"), 2, {spot.x + 5, spot.y});
    REQUIRE(scout);
    const int32_t hp = g.entity(scout)->hp;
    run(g, 6 * sim::kTicksPerSecond);
    const auto* s = g.entity(scout);
    CHECK((!s || s->hp < hp));
}

TEST_CASE("Determinismo: combate con flechas, mismo hash tick a tick") {
    auto setup = [](sim::GameState& g) {
        const auto spot = battlefield(g);
        for (int i = 0; i < 3; ++i) {
            g.debug_spawn(T("archer"), 1, {spot.x - 3, spot.y + i - 1});
            g.debug_spawn(T("militia"), 2, {spot.x + 3, spot.y + i - 1});
        }
    };
    sim::GameState a(registry(), config(5));
    sim::GameState b(registry(), config(5));
    setup(a);
    setup(b);
    for (int t = 0; t < 900; ++t) {
        a.step({});
        b.step({});
        REQUIRE(a.state_hash() == b.state_hash());
    }
}
