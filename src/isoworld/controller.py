from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass, field
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .entity import GameEntity


@dataclass
class DamageResult:
    entity_id: int
    damage: float = 0.0
    healed: float = 0.0
    repaired: float = 0.0
    died: bool = False
    ignored: bool = False


class DamageResolver:
    def __init__(self, grid: "WorldGrid | None" = None) -> None:
        self.grid = grid

    def resolve(self, entity: "GameEntity") -> DamageResult:
        result = DamageResult(entity_id=entity.id)

        if not entity.alive or entity.invulnerable or not entity.has_hp:
            result.ignored = True
            entity.reset_pending()
            return result

        if entity._pending_damage > 0.0:
            amount = entity._pending_damage
            entity._pending_damage = 0.0
            result.damage = min(amount, entity.hp)
            entity.hp -= amount
            if entity.hp <= 0.0:
                entity.hp = 0.0
                entity.alive = False
                result.died = True
                if self.grid is not None:
                    self.grid.remove_entity(entity)
                entity.reset_pending()
                return result

        if entity._pending_heal > 0.0:
            if entity.can_be_healed:
                amount = min(entity._pending_heal, entity.max_hp - entity.hp)
                entity.hp += amount
                result.healed = amount
            entity._pending_heal = 0.0

        if entity._pending_repair > 0.0:
            if entity.can_be_repaired:
                amount = min(entity._pending_repair, entity.max_hp - entity.hp)
                entity.hp += amount
                result.repaired = amount
            entity._pending_repair = 0.0

        return result

    def damage_area(self, grid: "WorldGrid", col: int, row: int, radius: int, amount: float) -> list[DamageResult]:
        targets = grid.entities_in_range(col, row, radius)
        for entity in targets:
            entity.take_damage(amount)
        return [self.resolve(entity) for entity in targets]
