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

// ─── Recolectar: ir → (cazar) → recolectar → depositar → volver ─────────────

GatherAction::GatherAction(core::EntityHandle target, data::ResourceTag resource) : target_(target), resource_(resource) {}
GatherAction::~GatherAction() = default;

const char* GatherAction::label() const {
    if (phase_ == Phase::Hunting) return "Cazando";
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
    case Phase::Hunting: return hunting(self, game);
    case Phase::Gathering: return gathering(self, game);
    case Phase::ToDropoff: return to_dropoff(self, game);
    }
    return ActionStatus::Done;
}

bool GatherAction::retarget(SpawnableEntity& self, GameState& game, core::EntityHandle exclude) {
    target_ = game.nearest_resource(resource_, has_spot_ ? last_spot_ : self.position, 10, self.current_player,
                                    /*skip_full=*/true, game.move_layer(self), exclude);
    plan_.reset();
    field_spot_.reset();
    return static_cast<bool>(target_);
}

StepResult GatherAction::follow(SpawnableEntity& self, GameState& game, const SpawnableEntity& target) {
    if (!plan_) {
        plan_ = game.plan_path_to(self, target);
        if (!plan_) return StepResult::Blocked;
    }
    const StepResult r = game.step_along(self, *plan_);
    if (r != StepResult::Moving) plan_.reset();
    return r;
}

ActionStatus GatherAction::to_resource(SpawnableEntity& self, GameState& game) {
    const SpawnableEntity* node = game.entity(target_);
    if (node && !node->resource_node && game.is_huntable(*node)) {
        phase_ = Phase::Hunting;  // un animal: primero hay que cazarlo
        return ActionStatus::Running;
    }
    if (!node || !node->resource_node || node->resource_node->amount_remaining <= 0) {
        if (wait_ > 0) {  // la carne del animal cazado aparece al final del tick
            --wait_;
            retarget(self, game);
            return ActionStatus::Running;
        }
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

    // En una granja hay que estar DENTRO del campo para empezar; en lo demás, basta con estar al lado
    const bool arrived = game.sheet_of(*node).stats.walkable ? game.distance_to(self, *node) == Fixed{} : game.in_reach(self, *node);
    if (arrived) {
        // ¿Hay sitio? Si otros lo ocupan todo, probar otro cercano del mismo tipo; si no hay, esperar.
        if (game.gatherers_on(target_) >= game.stats_of(*node).gather_slots) {
            const core::EntityHandle full = target_;
            if (!retarget(self, game, full)) target_ = full;
            return ActionStatus::Running;
        }
        phase_ = Phase::Gathering;
        game.claim_gather(target_);
        last_spot_ = node->position;
        has_spot_ = true;
        plan_.reset();
        field_spot_.reset();
        stuck_ = 0;
        return ActionStatus::Running;
    }

    switch (follow(self, game, *node)) {
    case StepResult::Moving: break;
    case StepResult::Arrived:
    case StepResult::Blocked:
        if (++stuck_ > 4) {  // inaccesible: probar con otro
            const core::EntityHandle bad = target_;
            stuck_ = 0;
            if (!retarget(self, game, bad)) return ActionStatus::Done;
        }
        break;
    }
    return ActionStatus::Running;
}

ActionStatus GatherAction::hunting(SpawnableEntity& self, GameState& game) {
    const SpawnableEntity* prey = game.entity(target_);
    if (!prey || prey->hp <= 0) {
        // Abatido: la carne aparece al final de este tick donde cayó el animal
        phase_ = Phase::ToResource;
        target_ = {};
        wait_ = 3;
        hunt_.reset();
        plan_.reset();
        return ActionStatus::Running;
    }
    last_spot_ = prey->position;
    has_spot_ = true;
    if (!hunt_) hunt_ = std::make_unique<AttackAction>(target_, false);
    if (hunt_->tick(self, game) == ActionStatus::Done) {
        hunt_.reset();
        return ActionStatus::Done;  // se escapó o no se puede alcanzar
    }
    return ActionStatus::Running;
}

ActionStatus GatherAction::gathering(SpawnableEntity& self, GameState& game) {
    SpawnableEntity* node = game.entity_mut(target_);
    if (!node || !node->resource_node || node->resource_node->amount_remaining <= 0 || !game.in_reach(self, *node)) {
        phase_ = Phase::ToResource;
        return ActionStatus::Running;
    }


    // Granja (recurso transitable): trabajar una casilla al azar y, al rato, cambiar a otra
    const auto& node_sheet = game.sheet_of(*node);
    if (node_sheet.stats.walkable) {
        if (!field_spot_) {
            const int w = node_sheet.stats.footprint_w, h = node_sheet.stats.footprint_h;
            const auto cell = static_cast<int32_t>(game.random(static_cast<uint32_t>(w * h)));
            field_spot_ = core::tile_center({node->anchor.x + cell % w, node->anchor.y + cell / w});
            plan_ = MovePlan{{*field_spot_}, 0};
        }
        if (plan_ && !plan_->done()) {
            if (game.step_along(self, *plan_) == StepResult::Moving) return ActionStatus::Running;
            plan_.reset();
        }
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
        field_gained_ += gained;
        if (node_sheet.stats.walkable && field_gained_ >= 3) {  // a otra casilla del campo
            field_gained_ = 0;
            field_spot_.reset();
        }
    }

    if (self.carry.amount >= Fixed::from_int(stats.carry_capacity)) {
        self.carry.amount = Fixed::from_int(stats.carry_capacity);
        phase_ = Phase::ToDropoff;
        dropoff_ = {};
        plan_.reset();
        field_spot_.reset();
    } else if (node->resource_node->amount_remaining <= 0) {
        phase_ = Phase::ToResource;
    }
    return ActionStatus::Running;
}

ActionStatus GatherAction::to_dropoff(SpawnableEntity& self, GameState& game) {
    SpawnableEntity* drop = game.entity_mut(dropoff_);
    if (!drop || !drop->is_complete()) {
        dropoff_ = game.nearest_dropoff(self.current_player, self.carry.resource, self.position, game.move_layer(self));
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

    switch (follow(self, game, *drop)) {
    case StepResult::Moving: break;
    case StepResult::Arrived:
    case StepResult::Blocked:
        if (++stuck_ > 6) return ActionStatus::Done;
        break;
    }
    return ActionStatus::Running;
}

// ─── Atacar ──────────────────────────────────────────────────────────────────

ActionStatus AttackAction::tick(SpawnableEntity& self, GameState& game) {
    const auto& stats = game.stats_of(self);
    const SpawnableEntity* target = game.entity(target_);
    if (!target || target->hp <= 0 || !game.can_target(self, *target)) {
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

// ─── Avanzar atacando ────────────────────────────────────────────────────────

ActionStatus AttackMoveAction::tick(SpawnableEntity& self, GameState& game) {
    if (engaged_) {
        if (engaged_->tick(self, game) == ActionStatus::Running) return ActionStatus::Running;
        engaged_.reset();  // ganó o perdió el rastro: seguir hacia el destino
        plan_.reset();
    }
    // Mirar alrededor cada 8 ticks (escalonado) por si hay enemigos a la vista
    if ((game.tick() + self.self.index) % 8 == 0 && game.sheet_of(self).can_attack()) {
        const core::EntityHandle enemy = game.find_enemy_near(self, game.stats_of(self).sight);
        if (enemy) {
            engaged_.emplace(enemy, true);
            return ActionStatus::Running;
        }
    }
    if (!plan_) {
        plan_ = game.plan_path(self, destination_);
        if (!plan_) return ActionStatus::Done;
    }
    switch (game.step_along(self, *plan_)) {
    case StepResult::Arrived: return ActionStatus::Done;
    case StepResult::Blocked:
        plan_.reset();
        return ++replans_ > 5 ? ActionStatus::Done : ActionStatus::Running;
    case StepResult::Moving: break;
    }
    return ActionStatus::Running;
}

// ─── Construir ───────────────────────────────────────────────────────────────

ActionStatus BuildAction::tick(SpawnableEntity& self, GameState& game) {
    SpawnableEntity* site = game.entity_mut(site_);
    if (!site || site->is_complete()) return ActionStatus::Done;

    if (game.in_reach(self, *site)) {
        plan_.reset();
        if (!game.contribute_build(*site)) return ActionStatus::Running;
        game.continue_after_build(self, site_);  // depósito o granja: ponerse a trabajar sin quedarse ocioso
        return ActionStatus::Done;
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
