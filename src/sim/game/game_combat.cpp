// Fase 4: stats efectivos, combate, investigación, edades, líneas de mejora y civilizaciones.

#include <algorithm>

#include "sim/game/game_state.hpp"
#include "sim/modifiers/modifier_resolver.hpp"

namespace sim {

using core::Fixed;
using core::Vec2F;

namespace {

int64_t dist_sq_raw(Vec2F a, Vec2F b) {
    const int64_t dx = a.x.raw - b.x.raw;
    const int64_t dy = a.y.raw - b.y.raw;
    return dx * dx + dy * dy;
}

template <class T>
bool contains(const std::vector<T>& v, const T& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

}  // namespace

// ─── Stats efectivos ─────────────────────────────────────────────────────────

const data::EntityStats& GameState::stats_for(data::PlayerId p, data::TypeId t) const {
    const PlayerState& ps = players_.at(p);
    if (ps.stats_cache.size() != data_.sheets().size()) ps.stats_cache.assign(data_.sheets().size(), std::nullopt);
    auto& slot = ps.stats_cache[t.value];
    if (!slot) slot = compute_effective(data_.sheet(t), ps.modifiers);
    return *slot;
}

void GameState::on_modifiers_changed(data::PlayerId p) {
    // La vida máxima puede cambiar: se conserva el porcentaje de vida de cada entidad
    struct Old {
        core::EntityHandle h;
        int32_t max_hp;
    };
    std::vector<Old> old;
    entities_.for_each([&](core::EntityHandle h, const SpawnableEntity& e) {
        if (e.original_player == p && !e.projectile && !e.construction) old.push_back({h, stats_for(p, e.type).max_hp});
    });
    PlayerState& ps = players_[p];
    ps.stats_cache.assign(data_.sheets().size(), std::nullopt);
    ++ps.modifier_epoch;
    for (const Old& o : old) {
        SpawnableEntity& e = *entities_.get(o.h);
        const int32_t now = stats_for(p, e.type).max_hp;
        if (now != o.max_hp && o.max_hp > 0) e.hp = std::max<int32_t>(1, static_cast<int32_t>(static_cast<int64_t>(e.hp) * now / o.max_hp));
    }
}

// ─── Qué puede hacer cada jugador ────────────────────────────────────────────

bool GameState::is_disabled(data::PlayerId p, data::TypeId t) const {
    const auto* civ = players_.at(p).civ;
    return civ && contains(civ->disabled_types, t);
}

std::vector<data::TypeId> GameState::trainable_at(data::PlayerId p, data::TypeId building) const {
    const PlayerState& ps = players_.at(p);
    std::vector<data::TypeId> out;
    auto add = [&](data::TypeId base) {
        const data::TypeId t = ps.resolve(base);  // línea de mejora: militia → man_at_arms
        if (!is_disabled(p, t) && !is_disabled(p, base) && !contains(out, t)) out.push_back(t);
    };
    for (data::TypeId base : data_.sheet(building).trains) add(base);
    if (ps.civ) {
        for (const auto& extra : ps.civ->extra_trains) {
            if (extra.building == building) add(extra.unit);
        }
    }
    return out;
}

std::vector<data::TypeId> GameState::buildable_by(data::PlayerId p, const data::ObjSheet& unit) const {
    std::vector<data::TypeId> out;
    if (!unit.stats.has(data::Capability::Build)) return out;
    for (data::TypeId t : unit.builds) {
        if (!is_disabled(p, t)) out.push_back(t);
    }
    return out;
}

TechStatus GameState::tech_status(data::PlayerId p, data::TechId t) const {
    const PlayerState& ps = players_.at(p);
    const auto& tech = data_.tech(t);
    if (ps.civ && contains(ps.civ->disabled_techs, t)) return TechStatus::Disabled;
    if (ps.has_researched(t)) return TechStatus::Researched;
    for (core::EntityHandle h : ps.buildings) {
        if (const SpawnableEntity* b = entities_.get(h)) {
            for (const auto& job : b->production) {
                if (job.tech && *job.tech == t) return TechStatus::Queued;
            }
        }
    }
    if (ps.age < tech.min_age) return TechStatus::NeedsAge;
    for (data::TechId req : tech.requires_techs) {
        if (!ps.has_researched(req)) return TechStatus::NeedsTech;
    }
    // Subir a una edad que ya se tiene no tiene sentido
    if (tech.advances_to_age && *tech.advances_to_age <= ps.age) return TechStatus::Researched;
    return TechStatus::Available;
}

std::vector<data::TechId> GameState::techs_at(data::PlayerId p, data::TypeId building) const {
    std::vector<data::TechId> out;
    for (const auto& tech : data_.techs()) {
        if (tech.researched_at != building) continue;
        const TechStatus s = tech_status(p, tech.id);
        if (s == TechStatus::Researched || s == TechStatus::Disabled) continue;
        out.push_back(tech.id);
    }
    return out;
}

// ─── Investigación ───────────────────────────────────────────────────────────

void GameState::retype(SpawnableEntity& e, data::TypeId to) {
    const int32_t old_max = stats_of(e).max_hp;
    e.type = to;
    e.classes = data_.sheet(to).stats.classes;
    const int32_t new_max = stats_of(e).max_hp;
    if (old_max > 0) e.hp = std::max<int32_t>(1, static_cast<int32_t>(static_cast<int64_t>(e.hp) * new_max / old_max));
}

void GameState::complete_research(data::PlayerId p, data::TechId t) {
    PlayerState& ps = players_.at(p);
    if (ps.has_researched(t)) return;
    const auto& tech = data_.tech(t);
    if (ps.researched.size() != data_.techs().size()) ps.researched.assign(data_.techs().size(), false);
    ps.researched[t] = true;

    if (tech.advances_to_age) ps.age = std::max(ps.age, *tech.advances_to_age);

    if (!tech.modifiers.empty()) {
        ps.modifiers.insert(ps.modifiers.end(), tech.modifiers.begin(), tech.modifiers.end());
        on_modifiers_changed(p);
    }

    // Líneas de mejora (architecture.md §11.8): las unidades existentes y las colas cambian de tipo
    for (const data::Upgrade& up : tech.upgrades) {
        for (auto& [from, to] : ps.upgraded) {
            if (to == up.from) to = up.to;  // cadenas: A→B y luego B→C deja A→C
        }
        ps.upgraded[up.from] = up.to;
        entities_.for_each([&](core::EntityHandle, SpawnableEntity& e) {
            if (e.original_player == p && e.type == up.from) retype(e, up.to);
        });
        for (core::EntityHandle h : ps.buildings) {
            if (SpawnableEntity* b = entities_.get(h)) {
                for (auto& job : b->production) {
                    if (!job.tech && job.type == up.from) job.type = up.to;
                }
            }
        }
    }
}

// ─── Combate ─────────────────────────────────────────────────────────────────

bool GameState::is_enemy(const SpawnableEntity& a, const SpawnableEntity& b) const {
    if (a.current_player == b.current_player) return false;
    if (a.current_player == data::kGaia || b.current_player == data::kGaia) return false;
    if (b.projectile || b.hp <= 0) return false;
    return true;
}

Fixed GameState::distance_to(const SpawnableEntity& from, const SpawnableEntity& to) const {
    const auto& ts = sheet_of(to);
    if (ts.is_unit()) {
        // Unidades: distancia entre centros menos el radio del objetivo
        const Fixed d = core::length(to.position - from.position) - stats_of(to).radius;
        return std::max(Fixed{}, d);
    }
    // Edificios y recursos: distancia al borde del footprint
    const Fixed x0 = Fixed::from_int(to.anchor.x), y0 = Fixed::from_int(to.anchor.y);
    const Fixed x1 = x0 + Fixed::from_int(ts.stats.footprint_w), y1 = y0 + Fixed::from_int(ts.stats.footprint_h);
    const Fixed dx = std::max({x0 - from.position.x, Fixed{}, from.position.x - x1});
    const Fixed dy = std::max({y0 - from.position.y, Fixed{}, from.position.y - y1});
    return core::length({dx, dy});
}

bool GameState::in_attack_range(const SpawnableEntity& attacker, const SpawnableEntity& target) const {
    const auto& s = stats_of(attacker);
    const Fixed d = distance_to(attacker, target);
    if (s.range <= Fixed{}) return d <= Fixed::ratio(4, 5);  // cuerpo a cuerpo
    // A distancia: desde el borde del propio edificio si quien dispara es una torre o la plaza
    const auto& as = sheet_of(attacker);
    const Fixed own = as.is_building() ? Fixed::ratio(std::max(as.stats.footprint_w, as.stats.footprint_h), 2) : Fixed{};
    return d <= s.range + own + Fixed::ratio(1, 2) && d >= s.min_range;
}

core::EntityHandle GameState::find_enemy_near(const SpawnableEntity& e, int radius_tiles) const {
    std::vector<core::EntityHandle> found;
    const Fixed r = Fixed::from_int(radius_tiles);
    spatial_.query_rect({e.position.x - r, e.position.y - r}, {e.position.x + r, e.position.y + r}, found);
    core::EntityHandle best;
    bool best_is_building = true;
    int64_t best_d = 0;
    const int64_t max_d = static_cast<int64_t>(r.raw) * r.raw;
    for (core::EntityHandle h : found) {
        const SpawnableEntity* o = entities_.get(h);
        if (!o || !is_enemy(e, *o)) continue;
        const auto& os = sheet_of(*o);
        if (!os.is_unit() && !os.is_building()) continue;
        const int64_t d = dist_sq_raw(o->position, e.position);
        if (d > max_d) continue;
        const bool is_building = os.is_building();
        // Preferir unidades a edificios; luego la más cercana; desempate por índice
        const bool better = !best || (best_is_building && !is_building) ||
                            (best_is_building == is_building && (d < best_d || (d == best_d && h.index < best.index)));
        if (better) {
            best = h;
            best_d = d;
            best_is_building = is_building;
        }
    }
    return best;
}

void GameState::perform_attack(const SpawnableEntity& attacker, const SpawnableEntity& target) {
    const auto& as = stats_of(attacker);
    const int32_t damage = data::compute_damage(as, stats_of(target));
    if (as.projectile) {
        SpawnRequest req;
        req.type = *as.projectile;
        req.owner = data::kGaia;
        req.exact_position = attacker.position;
        req.projectile = ProjectileData{attacker.self, target.self, target.position, damage, attacker.current_player, attacker.type};
        pending_spawns_.push_back(std::move(req));
    } else {
        damage_.push_back({target.self, attacker.self, damage, attacker.current_player, attacker.type});
    }
}

void GameState::projectile_system() {
    entities_.for_each([&](core::EntityHandle h, SpawnableEntity& e) {
        if (!e.projectile) return;
        const Fixed step = sheet_of(e).stats.move_speed / Fixed::from_int(kTicksPerSecond);
        const Vec2F before = e.position;
        const Vec2F d = e.projectile->aim - e.position;
        const Fixed dist = core::length(d);
        if (dist > step) {
            e.position = e.position + Vec2F{d.x * step / dist, d.y * step / dist};
            spatial_.move(h, before, e.position);
            return;
        }
        // Impacto: acierta si el objetivo sigue cerca del punto al que se apuntó
        e.position = e.projectile->aim;
        spatial_.move(h, before, e.position);
        const SpawnableEntity* target = entities_.get(e.projectile->target);
        if (target && target->hp > 0) {
            SpawnableEntity probe;
            probe.position = e.projectile->aim;
            if (distance_to(probe, *target) <= Fixed::ratio(3, 5)) {
                damage_.push_back({e.projectile->target, e.projectile->source, e.projectile->damage, e.projectile->owner,
                                   e.projectile->source_type});
            }
        }
        request_despawn(h);
    });
}

void GameState::resolve_damage() {
    for (const DamageEvent& ev : damage_) {
        SpawnableEntity* victim = entities_.get(ev.target);
        if (!victim || victim->hp <= 0) continue;
        victim->hp -= ev.amount;
        if (victim->hp <= 0) {
            victim->hp = 0;
            victim->action.reset();
            players_[ev.attacker_player].kills_by_type[victim->type]++;
            players_[victim->current_player].losses_by_type[victim->type]++;
            request_despawn(ev.target);
            continue;
        }
        // Represalia: una unidad militar ociosa responde a quien la ataca
        const SpawnableEntity* attacker = entities_.get(ev.source);
        if (!victim->action && attacker && attacker->hp > 0 && sheet_of(*victim).can_attack() && stats_of(*victim).aggressive &&
            is_enemy(*victim, *attacker)) {
            victim->action = std::make_unique<AttackAction>(ev.source, true);
        }
    }
    damage_.clear();
}

void GameState::auto_attack_system() {
    // Cada entidad mira a su alrededor una vez cada 8 ticks (escalonado por índice)
    entities_.for_each([&](core::EntityHandle h, SpawnableEntity& e) {
        if ((h.index + tick_) % 8 != 0 || e.action || e.hp <= 0 || e.construction || e.current_player == data::kGaia) return;
        const auto& sheet = sheet_of(e);
        if (!sheet.can_attack()) return;
        const auto& s = stats_of(e);
        if (!s.aggressive) return;
        const int radius = std::max<int>(s.sight, (s.range + Fixed::from_int(1)).floor_int() + (sheet.is_building() ? sheet.stats.footprint_w / 2 : 0));
        const core::EntityHandle enemy = find_enemy_near(e, radius);
        if (enemy) e.action = std::make_unique<AttackAction>(enemy, true);
    });
}

// ─── Herramientas ────────────────────────────────────────────────────────────

core::EntityHandle GameState::debug_spawn(data::TypeId type, data::PlayerId owner, core::TilePos tile) {
    return spawn_now({type, owner, tile, false, std::nullopt, std::nullopt});
}

}  // namespace sim
