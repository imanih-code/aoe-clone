#include <algorithm>

#include "sim/actions/action.hpp"
#include "sim/game/game_state.hpp"

namespace sim {

using core::Fixed;

// ─── Mover ───────────────────────────────────────────────────────────────────

ActionStatus MoveAction::tick(SpawnableEntity& self, GameState& game) {
    if (!plan_) {
        plan_ = game.plan_path(self, target_);
        if (!plan_) return ActionStatus::Done;
    }
    switch (game.step_along(self, *plan_)) {
    case StepResult::Arrived: return ActionStatus::Done;
    case StepResult::Blocked:
        plan_.reset();  // replanificar el próximo tick
        return ++replans_ > 5 ? ActionStatus::Done : ActionStatus::Running;
    case StepResult::Moving: break;
    }
    return ActionStatus::Running;
}

// ─── Recolectar: ir → recolectar → depositar → volver ───────────────────────

const char* GatherAction::label() const {
    switch (resource_) {
    case data::ResourceTag::Food: return "Recolectando comida";
    case data::ResourceTag::Wood: return "Talando madera";
    case data::ResourceTag::Gold: return "Extrayendo oro";
    case data::ResourceTag::Stone: return "Extrayendo piedra";
    }
    return "Recolectando";
}

ActionStatus GatherAction::tick(SpawnableEntity& self, GameState& game) {
    switch (phase_) {
    case Phase::ToResource: return to_resource(self, game);
    case Phase::Gathering: return gathering(self, game);
    case Phase::ToDropoff: return to_dropoff(self, game);
    }
    return ActionStatus::Done;
}

bool GatherAction::retarget(SpawnableEntity& self, GameState& game) {
    target_ = game.nearest_resource(resource_, has_spot_ ? last_spot_ : self.position, 10);
    plan_.reset();
    return static_cast<bool>(target_);
}

ActionStatus GatherAction::to_resource(SpawnableEntity& self, GameState& game) {
    const SpawnableEntity* node = game.entity(target_);
    if (!node || !node->resource_node || node->resource_node->amount_remaining <= 0) {
        if (!retarget(self, game)) {
            // No queda nada cerca: entregar lo que lleve y quedarse ocioso
            if (self.carry.amount > Fixed{}) {
                phase_ = Phase::ToDropoff;
                return ActionStatus::Running;
            }
            return ActionStatus::Done;
        }
        node = game.entity(target_);
    }

    if (game.in_reach(self, *node)) {
        phase_ = Phase::Gathering;
        last_spot_ = node->position;
        has_spot_ = true;
        plan_.reset();
        stuck_ = 0;
        return ActionStatus::Running;
    }

    if (!plan_) {
        plan_ = game.plan_path_to(self, *node);
        if (!plan_) {  // inaccesible: probar con otro
            target_ = {};
            return ++stuck_ > 4 ? ActionStatus::Done : ActionStatus::Running;
        }
    }
    switch (game.step_along(self, *plan_)) {
    case StepResult::Moving: break;
    case StepResult::Arrived:
    case StepResult::Blocked:
        plan_.reset();
        if (++stuck_ > 4) {
            target_ = {};
            stuck_ = 0;
        }
        break;
    }
    return ActionStatus::Running;
}

ActionStatus GatherAction::gathering(SpawnableEntity& self, GameState& game) {
    SpawnableEntity* node = game.entity_mut(target_);
    if (!node || !node->resource_node || node->resource_node->amount_remaining <= 0 || !game.in_reach(self, *node)) {
        phase_ = Phase::ToResource;
        return ActionStatus::Running;
    }

    // Cambiar de recurso tira lo que se llevaba
    if (self.carry.resource != resource_) self.carry = CarryState{resource_, Fixed{}};

    const auto& stats = game.stats_of(self);  // con bonus de civ y tecnologías
    const int32_t before = self.carry.amount.floor_int();
    self.carry.amount += stats.gather_per_tick[data::index_of(resource_)];
    int32_t gained = self.carry.amount.floor_int() - before;
    if (gained > 0) {
        int32_t& remaining = node->resource_node->amount_remaining;
        if (gained > remaining) {
            self.carry.amount -= Fixed::from_int(gained - remaining);
            gained = remaining;
        }
        remaining -= gained;
        if (remaining <= 0) game.request_despawn(node->self);
    }

    if (self.carry.amount >= Fixed::from_int(stats.carry_capacity)) {
        self.carry.amount = Fixed::from_int(stats.carry_capacity);
        phase_ = Phase::ToDropoff;
        dropoff_ = {};
        plan_.reset();
    } else if (node->resource_node->amount_remaining <= 0) {
        phase_ = Phase::ToResource;
    }
    return ActionStatus::Running;
}

ActionStatus GatherAction::to_dropoff(SpawnableEntity& self, GameState& game) {
    SpawnableEntity* drop = game.entity_mut(dropoff_);
    if (!drop || !drop->is_complete()) {
        dropoff_ = game.nearest_dropoff(self.current_player, self.carry.resource, self.position);
        plan_.reset();
        drop = game.entity_mut(dropoff_);
        if (!drop) return ActionStatus::Done;  // no hay dónde depositar: se queda con la carga
    }

    if (game.in_reach(self, *drop)) {
        game.player_mut(self.current_player).resources[data::index_of(self.carry.resource)] += self.carry.amount.floor_int();
        self.carry.amount = Fixed{};
        phase_ = Phase::ToResource;
        plan_.reset();
        stuck_ = 0;
        return ActionStatus::Running;
    }

    if (!plan_) {
        plan_ = game.plan_path_to(self, *drop);
        if (!plan_) return ActionStatus::Done;
    }
    switch (game.step_along(self, *plan_)) {
    case StepResult::Moving: break;
    case StepResult::Arrived:
    case StepResult::Blocked:
        plan_.reset();
        if (++stuck_ > 6) return ActionStatus::Done;
        break;
    }
    return ActionStatus::Running;
}

// ─── Atacar ──────────────────────────────────────────────────────────────────

ActionStatus AttackAction::tick(SpawnableEntity& self, GameState& game) {
    const auto& stats = game.stats_of(self);
    const SpawnableEntity* target = game.entity(target_);
    if (!target || target->hp <= 0 || !game.is_enemy(self, *target)) {
        // Objetivo muerto o perdido: las unidades agresivas buscan otro cerca
        if (!stats.aggressive) return ActionStatus::Done;
        target_ = game.find_enemy_near(self, stats.sight);
        plan_.reset();
        target = game.entity(target_);
        if (!target) return ActionStatus::Done;
    }
    if (!has_origin_) {
        origin_ = self.position;
        has_origin_ = true;
    }
    if (cooldown_ > 0) --cooldown_;

    if (game.in_attack_range(self, *target)) {
        plan_.reset();
        if (cooldown_ == 0) {
            game.perform_attack(self, *target);
            cooldown_ = stats.reload_ticks;
        }
        return ActionStatus::Running;
    }

    const auto& sheet = game.sheet_of(self);
    if (!sheet.can_move()) return ActionStatus::Done;  // torres: el objetivo salió del alcance
    // Lo que se decidió solo no se persigue lejos (no arrastrar el ejército por el mapa)
    if (automatic_ && core::length(self.position - origin_) > Fixed::from_int(10)) return ActionStatus::Done;

    // Objetivos que se mueven: replanificar cada medio segundo
    if (!plan_ || --repath_ <= 0) {
        plan_ = game.sheet_of(*target).is_unit() ? game.plan_path(self, target->position) : game.plan_path_to(self, *target);
        repath_ = 10;
        if (!plan_) return ActionStatus::Done;
    }
    if (game.step_along(self, *plan_) != StepResult::Moving) plan_.reset();
    return ActionStatus::Running;
}

// ─── Construir ───────────────────────────────────────────────────────────────

ActionStatus BuildAction::tick(SpawnableEntity& self, GameState& game) {
    SpawnableEntity* site = game.entity_mut(site_);
    if (!site || site->is_complete()) return ActionStatus::Done;

    if (game.in_reach(self, *site)) {
        plan_.reset();
        return game.contribute_build(*site) ? ActionStatus::Done : ActionStatus::Running;
    }
    if (!plan_) {
        plan_ = game.plan_path_to(self, *site);
        if (!plan_) return ActionStatus::Done;
    }
    switch (game.step_along(self, *plan_)) {
    case StepResult::Moving: break;
    case StepResult::Arrived:
    case StepResult::Blocked:
        plan_.reset();
        if (++stuck_ > 6) return ActionStatus::Done;
        break;
    }
    return ActionStatus::Running;
}

}  // namespace sim
