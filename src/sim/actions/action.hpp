#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/entity_handle.hpp"
#include "core/fixed.hpp"
#include "data/ids.hpp"

namespace sim {

class GameState;
struct SpawnableEntity;

// Camino a seguir: puntos de paso ya suavizados.
struct MovePlan {
    std::vector<core::Vec2F> waypoints;
    std::size_t next = 0;
    bool done() const { return next >= waypoints.size(); }
    core::Vec2F current() const { return waypoints[next]; }
};

enum class StepResult { Moving, Arrived, Blocked };
enum class ActionStatus { Running, Done };
enum class ActionKind { Move, Gather, Build, Attack, AttackMove, Convert, Heal, Garrison };

// Acción ordenada por el jugador (IActionBehavior en mutability.md).
// Reglas: guarda EntityHandle (nunca punteros) y nunca reasigna self.action.
class IAction {
public:
    virtual ~IAction() = default;
    virtual ActionKind kind() const = 0;
    virtual ActionStatus tick(SpawnableEntity& self, GameState& game) = 0;
    virtual const char* label() const = 0;  // UTF-8, para el HUD
};

class MoveAction final : public IAction {
public:
    explicit MoveAction(core::Vec2F target) : target_(target) {}
    ActionKind kind() const override { return ActionKind::Move; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override { return "Moviéndose"; }

private:
    core::Vec2F target_;
    std::optional<MovePlan> plan_;
    int replans_ = 0;
};

class AttackAction;

// Recolectar: ir → (cazar si es un animal) → recolectar → depositar → volver.
// - Si el recurso está lleno de recolectores, prueba otro cercano del mismo tipo.
// - En una granja (recurso transitable) trabaja una casilla al azar y luego otra, hasta llenar la carga.
class GatherAction final : public IAction {
public:
    GatherAction(core::EntityHandle target, data::ResourceTag resource);
    ~GatherAction() override;
    ActionKind kind() const override { return ActionKind::Gather; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override;
    data::ResourceTag resource() const { return resource_; }
    core::EntityHandle target() const { return target_; }
    bool is_gathering() const { return phase_ == Phase::Gathering; }

private:
    enum class Phase { ToResource, Hunting, Gathering, ToDropoff };
    ActionStatus to_resource(SpawnableEntity& self, GameState& game);
    ActionStatus hunting(SpawnableEntity& self, GameState& game);
    ActionStatus gathering(SpawnableEntity& self, GameState& game);
    ActionStatus to_dropoff(SpawnableEntity& self, GameState& game);
    bool retarget(SpawnableEntity& self, GameState& game, core::EntityHandle exclude = {});
    StepResult follow(SpawnableEntity& self, GameState& game, const SpawnableEntity& target);

    core::EntityHandle target_;
    data::ResourceTag resource_;
    Phase phase_ = Phase::ToResource;
    core::EntityHandle dropoff_;
    core::Vec2F last_spot_;
    bool has_spot_ = false;
    std::optional<MovePlan> plan_;
    int stuck_ = 0;
    int wait_ = 0;                          // ticks esperando a que aparezca la carne del animal cazado
    std::unique_ptr<AttackAction> hunt_;    // atacando al animal
    std::optional<core::Vec2F> field_spot_; // casilla de la granja que se está trabajando
    int field_gained_ = 0;
};

// Atacar: acercarse hasta tener al objetivo a tiro y golpear/disparar cada recarga.
// automatic = la unidad lo decidió sola (auto-ataque o represalia): no persigue más
// de unos tiles desde donde empezó.
class AttackAction final : public IAction {
public:
    AttackAction(core::EntityHandle target, bool automatic) : target_(target), automatic_(automatic) {}
    ActionKind kind() const override { return ActionKind::Attack; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override { return "Atacando"; }
    core::EntityHandle target() const { return target_; }

private:
    core::EntityHandle target_;
    bool automatic_;
    core::Vec2F origin_;
    bool has_origin_ = false;
    std::optional<MovePlan> plan_;
    uint32_t cooldown_ = 0;
    int repath_ = 0;
};

// Avanzar atacando: ir hacia un punto y pelear con lo que se encuentre por el camino.
// Lo usa la IA para sus oleadas y el jugador con Ctrl + clic derecho.
class AttackMoveAction final : public IAction {
public:
    explicit AttackMoveAction(core::Vec2F destination) : destination_(destination) {}
    ActionKind kind() const override { return ActionKind::AttackMove; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override { return engaged_ ? "Atacando" : "Avanzando"; }

private:
    core::Vec2F destination_;
    std::optional<MovePlan> plan_;
    std::optional<AttackAction> engaged_;
    int replans_ = 0;
};

// Convertir: acercarse a tiro y "predicar" un tiempo (aleatorio y determinista); si sale bien,
// el objetivo cambia de dueño pero conserva sus stats (original_player, architecture.md §3).
class ConvertAction final : public IAction {
public:
    explicit ConvertAction(core::EntityHandle target) : target_(target) {}
    ActionKind kind() const override { return ActionKind::Convert; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override { return "Convirtiendo"; }
    core::EntityHandle target() const { return target_; }
    float progress() const { return needed_ == 0 ? 0.f : static_cast<float>(done_) / static_cast<float>(needed_); }

private:
    core::EntityHandle target_;
    std::optional<MovePlan> plan_;
    int repath_ = 0;
    uint32_t needed_ = 0;
    uint32_t done_ = 0;
};

class HealAction final : public IAction {
public:
    explicit HealAction(core::EntityHandle target) : target_(target) {}
    ActionKind kind() const override { return ActionKind::Heal; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override { return "Curando"; }

private:
    core::EntityHandle target_;
    std::optional<MovePlan> plan_;
    int repath_ = 0;
    core::Fixed pending_;  // curación acumulada (fracciones de punto de vida)
};

// Entrar en un edificio: al llegar, la unidad desaparece dentro (GameState::garrison).
class GarrisonAction final : public IAction {
public:
    explicit GarrisonAction(core::EntityHandle building) : building_(building) {}
    ActionKind kind() const override { return ActionKind::Garrison; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override { return "Entrando al edificio"; }

private:
    core::EntityHandle building_;
    std::optional<MovePlan> plan_;
    int stuck_ = 0;
};

class BuildAction final : public IAction {
public:
    explicit BuildAction(core::EntityHandle site) : site_(site) {}
    ActionKind kind() const override { return ActionKind::Build; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override { return "Construyendo"; }
    core::EntityHandle site() const { return site_; }

private:
    core::EntityHandle site_;
    std::optional<MovePlan> plan_;
    int stuck_ = 0;
};

}  // namespace sim
