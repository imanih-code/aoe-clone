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
enum class ActionKind { Move, Gather, Build };

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

class GatherAction final : public IAction {
public:
    GatherAction(core::EntityHandle target, data::ResourceTag resource) : target_(target), resource_(resource) {}
    ActionKind kind() const override { return ActionKind::Gather; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override;
    data::ResourceTag resource() const { return resource_; }

private:
    enum class Phase { ToResource, Gathering, ToDropoff };
    ActionStatus to_resource(SpawnableEntity& self, GameState& game);
    ActionStatus gathering(SpawnableEntity& self, GameState& game);
    ActionStatus to_dropoff(SpawnableEntity& self, GameState& game);
    bool retarget(SpawnableEntity& self, GameState& game);

    core::EntityHandle target_;
    data::ResourceTag resource_;
    Phase phase_ = Phase::ToResource;
    core::EntityHandle dropoff_;
    core::Vec2F last_spot_;
    bool has_spot_ = false;
    std::optional<MovePlan> plan_;
    int stuck_ = 0;
};

class BuildAction final : public IAction {
public:
    explicit BuildAction(core::EntityHandle site) : site_(site) {}
    ActionKind kind() const override { return ActionKind::Build; }
    ActionStatus tick(SpawnableEntity& self, GameState& game) override;
    const char* label() const override { return "Construyendo"; }

private:
    core::EntityHandle site_;
    std::optional<MovePlan> plan_;
    int stuck_ = 0;
};

}  // namespace sim
