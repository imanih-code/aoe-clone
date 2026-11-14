// Fase 6 (mutability.md): capabilities dinámicas, ActionConfig, modificadores por instancia,
// conversión, curación, guarnición, triggers y habilidades.

#include <algorithm>

#include "sim/game/game_state.hpp"
#include "sim/modifiers/modifier_resolver.hpp"

namespace sim {

using core::Fixed;
using core::Vec2F;

namespace {

void merge_into(std::vector<std::pair<data::Capability, data::ActionConfig>>& configs, data::Capability cap, const data::ActionConfig& cfg) {
    for (auto& [c, existing] : configs) {
        if (c == cap) {
            existing = data::merge_config(existing, cfg);
            return;
        }
    }
    configs.emplace_back(cap, cfg);
}

template <class T>
void erase_value(std::vector<T>& v, const T& x) {
    v.erase(std::remove(v.begin(), v.end(), x), v.end());
}

}  // namespace

// ─── Stats con modificadores propios ─────────────────────────────────────────

const data::EntityStats& GameState::stats_of(const SpawnableEntity& e) const {
    if (e.instance_mods.empty()) return stats_for(e.original_player, e.type);
    const PlayerState& owner = players_.at(e.original_player);
    if (!e.instance_stats || e.instance_stats_epoch != owner.modifier_epoch || e.instance_stats_version != e.instance_version ||
        false) {
        // Primero los del jugador original, después los de la entidad (architecture.md §4.4)
        std::vector<data::Modifier> all = owner.modifiers;
        all.insert(all.end(), e.instance_mods.begin(), e.instance_mods.end());
        e.instance_stats = compute_effective(data_.sheet(e.type), all);
        e.instance_stats_epoch = owner.modifier_epoch;
        e.instance_stats_version = e.instance_version;
    }
    return *e.instance_stats;
}

void GameState::add_instance_modifier(SpawnableEntity& e, const data::Modifier& m, uint32_t until_tick) {
    const int32_t old_max = stats_of(e).max_hp;
    e.instance_mods.push_back(m);
    e.instance_mod_until.push_back(until_tick);
    ++e.instance_version;
    const int32_t new_max = stats_of(e).max_hp;
    if (new_max != old_max && old_max > 0) e.hp = std::max<int32_t>(1, static_cast<int32_t>(static_cast<int64_t>(e.hp) * new_max / old_max));
}

// ─── Capabilities (rebuild_capabilities de mutability.md) ────────────────────

const CapabilityCache& GameState::caps_of(const SpawnableEntity& e) const {
    CapabilityCache& c = e.caps;
    const uint32_t epoch = players_.at(e.original_player).modifier_epoch;
    if (c.valid && c.epoch == epoch && c.type == e.type && c.instance_version == e.instance_version) return c;

    const auto& sheet = data_.sheet(e.type);
    c.valid = true;
    c.epoch = epoch;
    c.type = e.type;
    c.instance_version = e.instance_version;
    // 1. Lo que trae la ficha
    c.caps = sheet.stats.capabilities;
    c.configs = sheet.action_configs;
    c.triggers = sheet.triggers;
    c.skills = sheet.skills;
    // 2. Modificadores del jugador original y 3. los de la propia entidad, en orden
    auto apply = [&](const data::Modifier& m) {
        if (!m.applies_to(e.type, e.classes)) return;
        for (const auto& om : m.object_mods) {
            if (om.grant) {
                c.caps.set(static_cast<std::size_t>(*om.grant));
                if (om.config) merge_into(c.configs, *om.grant, *om.config);
            }
            if (om.remove) {
                c.caps.reset(static_cast<std::size_t>(*om.remove));
                std::erase_if(c.configs, [&](const auto& p) { return p.first == *om.remove; });
            }
            if (!om.grant_trigger.empty() && std::find(c.triggers.begin(), c.triggers.end(), om.grant_trigger) == c.triggers.end()) {
                c.triggers.push_back(om.grant_trigger);
            }
            if (!om.remove_trigger.empty()) erase_value(c.triggers, om.remove_trigger);
            if (!om.grant_skill.empty() && std::find(c.skills.begin(), c.skills.end(), om.grant_skill) == c.skills.end()) {
                c.skills.push_back(om.grant_skill);
            }
            if (!om.remove_skill.empty()) erase_value(c.skills, om.remove_skill);
        }
    };
    for (const auto& m : players_.at(e.original_player).modifiers) apply(m);
    for (const auto& m : e.instance_mods) apply(m);
    return c;
}

const data::ActionConfig* GameState::config_of(const SpawnableEntity& e, data::Capability cap) const {
    const auto& c = caps_of(e);
    if (!c.caps.test(static_cast<std::size_t>(cap)) && cap != data::Capability::Garrison) return nullptr;
    for (const auto& [k, cfg] : c.configs) {
        if (k == cap) return &cfg;
    }
    return nullptr;
}

bool GameState::can_move(const SpawnableEntity& e) const {
    return has_cap(e, data::Capability::Move) && stats_of(e).move_speed > Fixed{} && !e.garrisoned_in;
}

bool GameState::can_attack(const SpawnableEntity& e) const {
    return has_cap(e, data::Capability::Attack) && !stats_of(e).attacks.empty() && !e.garrisoned_in;
}

bool GameState::can_build(const SpawnableEntity& e, data::TypeId building) const {
    if (!has_cap(e, data::Capability::Build)) return false;
    const auto list = buildable_by(e.current_player, e);
    return std::find(list.begin(), list.end(), building) != list.end();
}

std::vector<data::TypeId> GameState::buildable_by(data::PlayerId p, const SpawnableEntity& unit) const {
    std::vector<data::TypeId> out;
    if (!has_cap(unit, data::Capability::Build)) return out;
    // Lista de la ficha + lo que añaden los modificadores (BuildConfig); vacía = la de la ficha
    std::vector<data::TypeId> list = sheet_of(unit).builds;
    if (const auto* cfg = config_of(unit, data::Capability::Build)) {
        const auto& extra = std::get<data::BuildConfig>(*cfg).buildable;
        // Una unidad sin lista propia (p. ej. infantería con "Ingeniería de campo") solo construye lo de la config
        if (list.empty()) list = extra;
        else {
            for (auto t : extra) {
                if (std::find(list.begin(), list.end(), t) == list.end()) list.push_back(t);
            }
        }
    }
    for (auto t : list) {
        if (!is_disabled(p, t)) out.push_back(t);
    }
    return out;
}

bool GameState::can_convert(const SpawnableEntity& a, const SpawnableEntity& b) const {
    const auto* cfg = config_of(a, data::Capability::Convert);
    if (!cfg || a.garrisoned_in || b.garrisoned_in) return false;
    if (b.current_player == data::kGaia || !is_enemy(a, b)) return false;
    const auto& c = std::get<data::ConvertConfig>(*cfg);
    const auto& bs = sheet_of(b);
    if (bs.is_building()) return c.buildings;
    if (!bs.is_unit()) return false;
    return !c.classes.bits.any() || c.classes.intersects(b.classes);
}

bool GameState::can_heal(const SpawnableEntity& a, const SpawnableEntity& b) const {
    const auto* cfg = config_of(a, data::Capability::Heal);
    if (!cfg || a.self == b.self || b.garrisoned_in || b.current_player != a.current_player || b.hp <= 0) return false;
    const auto& bs = sheet_of(b);
    if (!bs.is_unit() || bs.stats.animal != data::AnimalBehavior::None || b.hp >= stats_of(b).max_hp) return false;
    const auto& c = std::get<data::HealConfig>(*cfg);
    return !c.classes.bits.any() || c.classes.intersects(b.classes);
}

const data::GarrisonConfig* GameState::garrison_config(const SpawnableEntity& building) const {
    for (const auto& [cap, cfg] : caps_of(building).configs) {
        if (cap == data::Capability::Garrison) return &std::get<data::GarrisonConfig>(cfg);
    }
    return nullptr;
}

bool GameState::can_garrison(const SpawnableEntity& u, const SpawnableEntity& b) const {
    if (u.garrisoned_in || !has_cap(u, data::Capability::Garrison) || !sheet_of(u).is_unit()) return false;
    if (b.current_player != u.current_player || !b.is_complete()) return false;
    const auto* cfg = garrison_config(b);
    if (!cfg || static_cast<int>(b.garrison.size()) >= cfg->capacity) return false;
    return !cfg->classes.bits.any() || cfg->classes.intersects(u.classes);
}

uint32_t GameState::skill_cooldown(const SpawnableEntity& e, const std::string& id) const {
    for (const auto& s : e.skill_states) {
        if (s.id == id) return s.cooldown;
    }
    return 0;
}

// ─── Conversión y guarnición ─────────────────────────────────────────────────

void GameState::convert_entity(core::EntityHandle target, data::PlayerId new_owner, core::EntityHandle by) {
    SpawnableEntity* t = entities_.get(target);
    if (!t) return;
    const auto& s = sheet_of(*t);
    if (s.is_building()) {
        ungarrison(target);  // los de dentro siguen siendo del antiguo dueño
        if (t->is_complete()) {
            players_[t->current_player].population_cap -= s.stats.population_provided;
            players_[new_owner].population_cap = std::min(kMaxPopulation, players_[new_owner].population_cap + s.stats.population_provided);
        }
        t->production.clear();
    }
    convert(target, new_owner);  // cambia current_player y las listas; original_player no cambia
    if (SpawnableEntity* converter = entities_.get(by)) fire(data::TriggerEvent::OnConvert, *converter, target);
}

bool GameState::garrison(core::EntityHandle unit, core::EntityHandle building) {
    SpawnableEntity* u = entities_.get(unit);
    SpawnableEntity* b = entities_.get(building);
    if (!u || !b || !can_garrison(*u, *b)) return false;
    spatial_.remove(unit, u->position);  // dentro del edificio: ni se ve, ni se ataca, ni se selecciona
    u->garrisoned_in = building;
    u->action.reset();
    u->orders.clear();
    u->position = u->prev_position = b->position;
    b->garrison.push_back(unit);
    return true;
}

void GameState::ungarrison(core::EntityHandle building) {
    SpawnableEntity* b = entities_.get(building);
    if (!b || b->garrison.empty()) return;
    const auto& bs = sheet_of(*b);
    const core::TilePos exit{b->anchor.x + bs.stats.footprint_w, b->anchor.y + bs.stats.footprint_h};
    const std::vector<core::EntityHandle> inside = std::move(b->garrison);
    b->garrison.clear();
    for (core::EntityHandle h : inside) {
        SpawnableEntity* u = entities_.get(h);
        if (!u) continue;
        PathQuery q;
        q.layer = move_layer(*u);
        const auto tile = Pathfinder::nearest_passable(map_, exit, 12, q).value_or(exit);
        u->garrisoned_in = {};
        u->position = u->prev_position = core::tile_center(tile);
        spatial_.insert(h, u->position);
    }
}

// ─── Triggers ────────────────────────────────────────────────────────────────

void GameState::fire(data::TriggerEvent event, SpawnableEntity& source, core::EntityHandle related) {
    const CapabilityCache& caps = caps_of(source);
    if (caps.triggers.empty()) return;
    const SpawnableEntity* other = entities_.get(related);
    const std::vector<std::string> ids = caps.triggers;  // copia: los efectos pueden invalidar la caché
    for (const auto& id : ids) {
        const data::TriggerTemplate* t = data_.find_trigger(id);
        if (!t || t->event != event) continue;
        if (t->filter_classes.bits.any() && (!other || !t->filter_classes.intersects(other->classes))) continue;
        uint32_t& uses = source.trigger_uses[id];
        if (t->charges >= 0 && uses >= static_cast<uint32_t>(t->charges)) continue;
        if (t->probability_permille < 1000 && rng_.below(1000) >= t->probability_permille) continue;
        ++uses;
        // Los efectos que crean entidades se aplican al final del tick (sin cadenas infinitas en el mismo tick)
        for (const auto& sp : t->spawns) {
            for (int i = 0; i < sp.count; ++i) {
                SpawnRequest req;
                req.type = sp.type;
                req.owner = sp.inherit_current ? source.current_player : source.original_player;
                req.original_owner = sp.inherit_original ? source.original_player : req.owner;
                req.tile = core::tile_of(source.position);
                req.hp_fraction = sp.hp_fraction;
                req.nearby_ok = true;
                pending_spawns_.push_back(req);
            }
        }
        for (const auto& r : t->resources) {
            players_[r.to_original ? source.original_player : source.current_player].resources[data::index_of(r.resource)] += r.amount;
        }
        for (const auto& m : t->self_modifiers) add_instance_modifier(source, m, 0);
    }
}

// ─── Habilidades ─────────────────────────────────────────────────────────────

bool GameState::use_skill(SpawnableEntity& e, std::size_t index) {
    const auto skills = caps_of(e).skills;
    if (index >= skills.size() || e.garrisoned_in || e.hp <= 0) return false;
    const data::SkillTemplate* skill = data_.find_skill(skills[index]);
    if (!skill) return false;
    auto it = std::find_if(e.skill_states.begin(), e.skill_states.end(), [&](const SkillState& s) { return s.id == skill->id; });
    if (it == e.skill_states.end()) {
        e.skill_states.push_back({skill->id, 0, 0});
        it = e.skill_states.end() - 1;
    }
    if (it->cooldown > 0) return false;

    switch (skill->behavior) {
    case data::SkillBehavior::Morph: {
        // Transformarse en lo que diga su MorphConfig (mutability.md, caso 3)
        const auto* cfg = config_of(e, data::Capability::Morph);
        if (!cfg) return false;
        const auto& morph = std::get<data::MorphConfig>(*cfg);
        const auto& into = data_.sheet(morph.target);
        if (morph.consume) {
            const core::TilePos at{e.position.x.floor_int() - into.stats.footprint_w / 2, e.position.y.floor_int() - into.stats.footprint_h / 2};
            if (into.is_building() && !map_.is_area_free(at.x, at.y, into.stats.footprint_w, into.stats.footprint_h)) return false;
            SpawnRequest req;
            req.type = morph.target;
            req.owner = e.current_player;
            req.original_owner = e.original_player;
            req.tile = into.is_building() ? at : core::tile_of(e.position);
            if (morph.keep_hp_percent) req.hp_fraction = Fixed::ratio(e.hp, std::max(1, stats_of(e).max_hp));
            pending_spawns_.push_back(req);
            request_despawn(e.self);
            e.action.reset();
            e.orders.clear();
        } else {
            retype(e, morph.target);
        }
        break;
    }
    case data::SkillBehavior::HealAura:
        it->active = skill->duration_ticks;
        break;
    case data::SkillBehavior::StatBoost:
        for (const auto& m : skill->modifiers) add_instance_modifier(e, m, tick_ + skill->duration_ticks);
        break;
    }
    // `it` puede invalidarse si add_instance_modifier... no toca skill_states: es seguro
    it->cooldown = std::max<uint32_t>(1, skill->cooldown_ticks);
    return true;
}

void GameState::skill_system() {
    entities_.for_each([&](core::EntityHandle, SpawnableEntity& e) {
        // Modificadores temporales que caducan
        if (!e.instance_mods.empty()) {
            bool changed = false;
            for (std::size_t i = e.instance_mods.size(); i-- > 0;) {
                if (e.instance_mod_until[i] != 0 && e.instance_mod_until[i] <= tick_) {
                    e.instance_mods.erase(e.instance_mods.begin() + static_cast<std::ptrdiff_t>(i));
                    e.instance_mod_until.erase(e.instance_mod_until.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                }
            }
            if (changed) {
                ++e.instance_version;
                e.hp = std::min(e.hp, stats_of(e).max_hp);
            }
        }
        for (auto& s : e.skill_states) {
            if (s.cooldown > 0) --s.cooldown;
            if (s.active == 0) continue;
            --s.active;
            // Aura de curación: una vez por segundo, a las unidades propias del radio
            const data::SkillTemplate* skill = data_.find_skill(s.id);
            if (!skill || skill->behavior != data::SkillBehavior::HealAura || tick_ % kTicksPerSecond != 0) continue;
            query_.clear();
            const Fixed r = skill->radius;
            spatial_.query_rect({e.position.x - r, e.position.y - r}, {e.position.x + r, e.position.y + r}, query_);
            const int32_t amount = std::max(1, skill->heal_per_second.floor_int());
            for (core::EntityHandle h : query_) {
                SpawnableEntity* u = entities_.get(h);
                if (!u || u->current_player != e.current_player || !sheet_of(*u).is_unit() || u->hp <= 0) continue;
                if (core::length(u->position - e.position) > r) continue;
                u->hp = std::min(stats_of(*u).max_hp, u->hp + amount);
            }
        }
    });
}

// Sanadores ociosos (monjes) atienden solos a las unidades propias heridas que ven
void GameState::heal_system() {
    entities_.for_each([&](core::EntityHandle h, SpawnableEntity& e) {
        if ((h.index + tick_) % 8 != 4 || e.action || e.garrisoned_in || e.hp <= 0) return;
        if (!has_cap(e, data::Capability::Heal)) return;
        query_.clear();
        const Fixed r = Fixed::from_int(stats_of(e).sight);
        spatial_.query_rect({e.position.x - r, e.position.y - r}, {e.position.x + r, e.position.y + r}, query_);
        core::EntityHandle best;
        Fixed best_d;
        for (core::EntityHandle o : query_) {
            const SpawnableEntity* u = entities_.get(o);
            if (!u || !can_heal(e, *u)) continue;
            const Fixed d = core::length(u->position - e.position);
            if (d > r) continue;
            if (!best || d < best_d) {
                best = o;
                best_d = d;
            }
        }
        if (best) e.action = std::make_unique<HealAction>(best);
    });
}

void GameState::debug_apply_modifier(data::PlayerId p, const data::Modifier& m) {
    players_.at(p).modifiers.push_back(m);
    on_modifiers_changed(p);
}

}  // namespace sim
