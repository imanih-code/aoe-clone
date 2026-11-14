#include "sim/ai/scripted_ai.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <map>

#include "sim/game/game_state.hpp"

namespace sim {

using core::Fixed;

namespace {

int64_t dist_sq(core::Vec2F a, core::Vec2F b) {
    const int64_t dx = a.x.raw - b.x.raw;
    const int64_t dy = a.y.raw - b.y.raw;
    return dx * dx + dy * dy;
}

std::optional<data::TypeId> type_of(const GameState& g, const char* key) { return g.data().find_type(key); }

// Reparto objetivo de aldeanos: comida, madera, oro, piedra
constexpr std::array<int, data::kResourceCount> kRatio = {45, 35, 20, 0};

}  // namespace

struct ScriptedAI::View {
    const PlayerState* ps = nullptr;
    core::TilePos base;
    const SpawnableEntity* tc = nullptr;
    std::vector<core::EntityHandle> villagers;
    std::vector<core::EntityHandle> idle_villagers;
    std::vector<core::EntityHandle> military;       // todas las unidades militares
    std::vector<core::EntityHandle> idle_military;  // sin orden
    std::array<int, data::kResourceCount> gatherers{};
    std::map<data::TypeId, int> building_count;  // terminados + en obra
    std::vector<const SpawnableEntity*> producers;  // edificios terminados que entrenan
    int under_construction = 0;
    int queued_units = 0;
    int pop_room = 0;
};

Command ScriptedAI::make(CommandType type, std::vector<core::EntityHandle> actors) const {
    Command c;
    c.issuer = me_;
    c.type = type;
    c.actors = std::move(actors);
    return c;
}

std::vector<Command> ScriptedAI::think(const GameState& game) {
    std::vector<Command> out;
    if (game.is_game_over()) return out;
    const PlayerState& ps = game.player(me_);
    if (!ps.active || ps.is_defeated) return out;
    if ((game.tick() + me_ * 3u) % 10 != 0) return out;  // dos veces por segundo, escalonado por jugador

    reserved_.clear();
    View v;
    v.ps = &ps;
    v.base = core::tile_of(game.start_position(me_));
    for (core::EntityHandle h : ps.buildings) {
        const SpawnableEntity* b = game.entity(h);
        if (!b) continue;
        ++v.building_count[b->type];
        if (!b->is_complete()) ++v.under_construction;
        if (b->is_complete() && !game.trainable_at(me_, b->type).empty()) v.producers.push_back(b);
        for (const auto& job : b->production) {
            if (!job.tech) v.queued_units += game.data().sheet(job.type).stats.population_cost;
        }
        if (game.sheet_of(*b).key == "town_center" && !v.tc) {
            v.tc = b;
            v.base = core::tile_of(b->position);
        }
    }
    for (core::EntityHandle h : ps.units) {
        const SpawnableEntity* u = game.entity(h);
        if (!u || u->hp <= 0) continue;
        const auto& s = game.sheet_of(*u);
        if (s.stats.has(data::Capability::Gather)) {
            v.villagers.push_back(h);
            if (!u->action) v.idle_villagers.push_back(h);
            else if (u->action->kind() == ActionKind::Gather) {
                ++v.gatherers[data::index_of(static_cast<const GatherAction&>(*u->action).resource())];
            }
        } else if (s.can_attack() && s.can_move()) {
            v.military.push_back(h);
            if (!u->action) v.idle_military.push_back(h);
        }
    }
    v.pop_room = ps.population_cap - ps.population_current - v.queued_units;

    military(game, v, out);  // primero: que el cuartel no se quede sin recursos frente a la plaza mayor
    buildings(game, v, out);
    economy(game, v, out);
    assign_idle_villagers(game, v, out);
    return out;
}

// ─── Economía: aldeanos y edades ─────────────────────────────────────────────

void ScriptedAI::economy(const GameState& game, const View& v, std::vector<Command>& out) {
    if (!v.tc) return;
    const auto villager = type_of(game, "villager");
    // Con cuartel, uno de cada tres huecos de población es para el ejército
    const bool army_behind = !v.producers.empty() && v.producers.size() > 1 && v.military.size() * 3 < v.villagers.size();
    const std::size_t max_queue = army_behind ? 1 : 2;
    // Si el ejército va por detrás y un cuartel está parado, ahorrar para él (el miliciano cuesta más que el aldeano)
    if (army_behind && v.villagers.size() >= 10) {
        const bool idle_producer = std::any_of(v.producers.begin(), v.producers.end(),
                                               [&](const SpawnableEntity* b) { return b != v.tc && b->production.empty(); });
        if (idle_producer) return;
    }
    if (villager && v.villagers.size() < 22 && v.tc->production.size() < max_queue && v.pop_room > 0 &&
        game.can_afford(me_, game.data().sheet(*villager).stats.cost)) {
        auto c = make(CommandType::Train, {v.tc->self});
        c.type_arg = *villager;
        out.push_back(std::move(c));
        return;
    }
    // Subir de edad cuando la economía está en marcha
    if (v.villagers.size() >= 16 && v.tc->production.empty()) {
        for (data::TechId id : game.techs_at(me_, v.tc->type)) {
            const auto& tech = game.data().tech(id);
            if (tech.advances_to_age && game.tech_status(me_, id) == TechStatus::Available && game.can_afford(me_, tech.cost)) {
                auto c = make(CommandType::Research, {v.tc->self});
                c.index = id;
                out.push_back(std::move(c));
                return;
            }
        }
    }
}

// ─── Construcción ────────────────────────────────────────────────────────────

std::optional<core::TilePos> ScriptedAI::find_spot(const GameState& game, data::TypeId building, core::TilePos near, int min_r,
                                                   int max_r) const {
    const auto& s = game.data().sheet(building);
    const int w = s.stats.footprint_w, h = s.stats.footprint_h;
    const VisibilityMap& fog = game.visibility(me_);
    for (int r = min_r; r <= max_r; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const core::TilePos t{near.x + dx - w / 2, near.y + dy - h / 2};
                // Un tile de margen alrededor para no encerrar a nadie
                if (!game.map().is_area_free(t.x - 1, t.y - 1, w + 2, h + 2)) continue;
                if (!fog.explored(t) || !fog.explored({t.x + w - 1, t.y + h - 1})) continue;
                return t;
            }
        }
    }
    return std::nullopt;
}

bool ScriptedAI::place(const GameState& game, const View& v, const char* key, core::TilePos near, int min_r, int max_r,
                       std::vector<Command>& out) {
    const auto type = type_of(game, key);
    if (!type || v.villagers.empty()) return false;
    const auto& s = game.data().sheet(*type);
    if (v.ps->age < s.stats.min_age || !game.can_afford(me_, s.stats.cost) || game.is_disabled(me_, *type)) return false;
    const auto spot = find_spot(game, *type, near, min_r, max_r);
    if (!spot) return false;
    // Constructor: el aldeano más cercano (preferiblemente ocioso)
    const auto& pool = v.idle_villagers.empty() ? v.villagers : v.idle_villagers;
    const core::Vec2F target = core::tile_center(*spot);
    core::EntityHandle builder = pool.front();
    int64_t best = INT64_MAX;
    for (auto h : pool) {
        const int64_t d = dist_sq(game.entity(h)->position, target);
        if (d < best) {
            best = d;
            builder = h;
        }
    }
    auto c = make(CommandType::PlaceBuilding, {builder});
    c.type_arg = *type;
    c.tile = *spot;
    out.push_back(std::move(c));
    reserved_.push_back(builder);  // que el reparto de aldeanos no le dé otra orden en este mismo think()
    return true;
}

void ScriptedAI::buildings(const GameState& game, const View& v, std::vector<Command>& out) {
    // Casas antes que nada (y sin esperar a otras obras): quedarse sin población lo para todo
    const int housing = v.ps->population_cap - v.ps->population_current - v.queued_units;
    bool house_building = false;
    for (core::EntityHandle h : v.ps->buildings) {
        const SpawnableEntity* b = game.entity(h);
        house_building = house_building || (b && !b->is_complete() && game.sheet_of(*b).key == "house");
    }
    if (housing <= 5 && !house_building && v.ps->population_cap < kMaxPopulation && place(game, v, "house", v.base, 4, 14, out)) {
        return;
    }

    if (v.under_construction > 0) {
        // De una en una. Si la obra se quedó sin constructor (lo mataron, se atascó...), mandar a otro.
        for (core::EntityHandle h : v.ps->buildings) {
            const SpawnableEntity* site = game.entity(h);
            if (!site || site->is_complete()) continue;
            const bool someone = std::any_of(v.villagers.begin(), v.villagers.end(), [&](core::EntityHandle u) {
                const SpawnableEntity* e = game.entity(u);
                return e->action && e->action->kind() == ActionKind::Build && static_cast<const BuildAction&>(*e->action).site() == h;
            });
            if (someone || v.villagers.empty()) continue;
            const auto& pool = v.idle_villagers.empty() ? v.villagers : v.idle_villagers;
            core::EntityHandle builder = pool.front();
            int64_t best = INT64_MAX;
            for (auto u : pool) {
                const int64_t d = dist_sq(game.entity(u)->position, site->position);
                if (d < best) {
                    best = d;
                    builder = u;
                }
            }
            auto c = make(CommandType::Build, {builder});
            c.target = h;
            out.push_back(std::move(c));
            reserved_.push_back(builder);
        }
        return;
    }
    auto count = [&](const char* key) {
        const auto t = type_of(game, key);
        if (!t) return 1;  // si el tipo no existe, como si ya estuviera
        auto it = v.building_count.find(*t);
        return it == v.building_count.end() ? 0 : it->second;
    };
    auto near_resource = [&](data::ResourceTag r) -> std::optional<core::TilePos> {
        const core::EntityHandle n = game.nearest_resource(r, core::tile_center(v.base), 25, me_);
        const SpawnableEntity* e = game.entity(n);
        if (!e || !game.is_known_to(me_, *e)) return std::nullopt;
        return core::tile_of(e->position);
    };
    const auto n = v.villagers.size();
    if (n >= 5 && count("lumber_camp") == 0) {
        if (auto t = near_resource(data::ResourceTag::Wood); t && place(game, v, "lumber_camp", *t, 2, 5, out)) return;
    }
    if (n >= 7 && count("mill") == 0) {
        if (auto t = near_resource(data::ResourceTag::Food); t && place(game, v, "mill", *t, 2, 5, out)) return;
    }
    if (n >= 9 && count("mining_camp") == 0) {
        if (auto t = near_resource(data::ResourceTag::Gold); t && place(game, v, "mining_camp", *t, 2, 5, out)) return;
    }
    if (n >= 11 && count("barracks") == 0 && place(game, v, "barracks", v.base, 6, 16, out)) return;
    if (v.ps->age >= 1 && count("archery_range") == 0 && place(game, v, "archery_range", v.base, 6, 16, out)) return;
    if (v.ps->age >= 1 && n >= 20 && count("barracks") < 2 && place(game, v, "barracks", v.base, 7, 18, out)) return;
}

// ─── Ejército ────────────────────────────────────────────────────────────────

void ScriptedAI::military(const GameState& game, const View& v, std::vector<Command>& out) {
    // Entrenar en cada edificio militar libre
    for (const SpawnableEntity* b : v.producers) {
        if (v.tc && b == v.tc) continue;  // la plaza mayor solo entrena aldeanos
        if (b->production.size() >= 2 || v.pop_room <= 0) continue;
        for (data::TypeId t : game.trainable_at(me_, b->type)) {
            const auto& s = game.data().sheet(t);
            if (v.ps->age < s.stats.min_age || !game.can_afford(me_, s.stats.cost)) continue;
            auto c = make(CommandType::Train, {b->self});
            c.type_arg = t;
            out.push_back(std::move(c));
            break;
        }
    }
    std::erase_if(attacking_, [&](core::EntityHandle h) { return !game.entity(h); });

    // Defensa: enemigos a la vista cerca de la base
    if (v.tc) {
        const core::EntityHandle threat = game.find_enemy_near(*v.tc, 14);
        if (const SpawnableEntity* e = game.entity(threat); e && !v.military.empty()) {
            auto c = make(CommandType::AttackMove, v.military);
            c.target_pos = e->position;
            out.push_back(std::move(c));
            return;
        }
    }

    // Explorar una vez con la caballería ligera
    if (!scouted_ && !v.idle_military.empty()) {
        for (auto h : v.idle_military) {
            if (game.stats_of(*game.entity(h)).sight >= 6) {
                auto c = make(CommandType::Move, {h});
                c.target_pos = {Fixed::from_int(game.map().width() / 2), Fixed::from_int(game.map().height() / 2)};
                out.push_back(std::move(c));
                scouted_ = true;
                return;
            }
        }
    }

    // Oleadas: cada vez un poco más grandes
    std::vector<core::EntityHandle> ready;
    for (auto h : v.idle_military) {
        if (std::find(attacking_.begin(), attacking_.end(), h) == attacking_.end()) ready.push_back(h);
    }
    const std::size_t wave_size = static_cast<std::size_t>(5 + 3 * wave_);
    if (ready.size() >= wave_size && game.tick() >= next_wave_tick_) {
        // Objetivo: la base inicial del enemigo vivo más cercano
        std::optional<core::Vec2F> target;
        int64_t best = INT64_MAX;
        for (int p = 1; p <= game.num_players(); ++p) {
            const auto pid = static_cast<data::PlayerId>(p);
            if (pid == me_ || game.player(pid).is_defeated) continue;
            const int64_t d = dist_sq(game.start_position(pid), core::tile_center(v.base));
            if (d < best) {
                best = d;
                target = game.start_position(pid);
            }
        }
        if (target) {
            auto c = make(CommandType::AttackMove, ready);
            c.target_pos = *target;
            out.push_back(std::move(c));
            attacking_.insert(attacking_.end(), ready.begin(), ready.end());
            ++wave_;
            next_wave_tick_ = game.tick() + 45u * kTicksPerSecond;
            return;
        }
    }

    // Las oleadas que llegaron y se quedaron sin objetivo buscan el edificio enemigo conocido más cercano
    std::vector<core::EntityHandle> stranded;
    for (auto h : attacking_) {
        const SpawnableEntity* u = game.entity(h);
        if (u && !u->action) stranded.push_back(h);
    }
    if (stranded.empty()) return;
    const core::Vec2F from = game.entity(stranded.front())->position;
    std::optional<core::Vec2F> goal;
    int64_t best = INT64_MAX;
    game.entities().for_each([&](core::EntityHandle, const SpawnableEntity& e) {
        if (e.current_player == me_ || e.current_player == data::kGaia || e.projectile) return;
        if (!game.sheet_of(e).is_building() || !game.is_known_to(me_, e)) return;
        const int64_t d = dist_sq(e.position, from);
        if (d < best) {
            best = d;
            goal = e.position;
        }
    });
    if (goal) {
        auto c = make(CommandType::AttackMove, stranded);
        c.target_pos = *goal;
        out.push_back(std::move(c));
    }
}

// ─── Reparto de aldeanos ─────────────────────────────────────────────────────

void ScriptedAI::assign_idle_villagers(const GameState& game, const View& v, std::vector<Command>& out) {
    if (v.idle_villagers.empty()) return;
    farm_placed_ = false;
    auto counts = v.gatherers;
    const int total = static_cast<int>(v.villagers.size());
    for (core::EntityHandle h : v.idle_villagers) {
        if (std::find(reserved_.begin(), reserved_.end(), h) != reserved_.end()) continue;
        // El recurso con más déficit respecto al reparto objetivo
        std::array<std::size_t, data::kResourceCount> order = {0, 1, 2, 3};
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return (kRatio[a] * total / 100 - counts[a]) > (kRatio[b] * total / 100 - counts[b]);
        });
        const SpawnableEntity* villager = game.entity(h);
        for (std::size_t r : order) {
            if (kRatio[r] == 0 && r != order.back()) continue;
            const core::EntityHandle node = game.nearest_resource(static_cast<data::ResourceTag>(r), villager->position, 30, me_);
            const SpawnableEntity* n = game.entity(node);
            if (!n || !game.is_known_to(me_, *n)) {
                // Sin bayas a mano: plantar una granja cerca de la base (una por think)
                if (r == data::index_of(data::ResourceTag::Food) && !farm_placed_) {
                    farm_placed_ = place(game, v, "farm", v.base, 3, 12, out);
                    if (farm_placed_ && reserved_.back() == h) {
                        ++counts[r];
                        break;
                    }
                }
                continue;
            }
            auto c = make(CommandType::Gather, {h});
            c.target = node;
            out.push_back(std::move(c));
            ++counts[r];
            break;
        }
    }
}

}  // namespace sim
