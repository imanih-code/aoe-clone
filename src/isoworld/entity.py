from __future__ import annotations

from collections.abc import Iterator
from enum import Enum, auto
from typing import TYPE_CHECKING, Any

from .pathfinding import PathMode

if TYPE_CHECKING:
    from .controller import DamageResolver
    from .grid import WorldGrid


class GameEntityType(Enum):
    RESOURCE = auto()
    BUILDING = auto()
    UNIT = auto()


class GameEntity:
    _next_id = 1

    def __init__(
        self,
        kind: GameEntityType,
        *,
        col: int = 0,
        row: int = 0,
        footprint: tuple[int, int] = (1, 1),
        max_hp: float = 1.0,
        has_hp: bool = True,
        invulnerable: bool = False,
        blocks_movement: bool = True,
        data: dict[str, Any] | None = None,
    ) -> None:
        if footprint[0] <= 0 or footprint[1] <= 0:
            raise ValueError("footprint must be at least 1x1")
        self.id = GameEntity._next_id
        GameEntity._next_id += 1
        self.kind = kind
        self.col = col
        self.row = row
        self.footprint = (int(footprint[0]), int(footprint[1]))
        self.has_hp = has_hp
        self.invulnerable = invulnerable
        self.blocks_movement = blocks_movement
        self.max_hp = max_hp if has_hp else 0.0
        self.hp = self.max_hp
        self.alive = True
        self.data = data or {}
        self._pending_damage = 0.0
        self._pending_heal = 0.0
        self._pending_repair = 0.0

    @property
    def is_resource(self) -> bool:
        return self.kind is GameEntityType.RESOURCE

    @property
    def is_building(self) -> bool:
        return self.kind is GameEntityType.BUILDING

    @property
    def is_unit(self) -> bool:
        return self.kind is GameEntityType.UNIT

    @property
    def hp_ratio(self) -> float:
        if self.max_hp <= 0:
            return 0.0
        return max(0.0, min(1.0, self.hp / self.max_hp))

    @property
    def can_be_healed(self) -> bool:
        return self.kind is GameEntityType.UNIT

    @property
    def can_be_repaired(self) -> bool:
        return self.kind is GameEntityType.BUILDING

    def occupied_cells(self) -> Iterator[tuple[int, int]]:
        for dc in range(self.footprint[0]):
            for dr in range(self.footprint[1]):
                yield self.col + dc, self.row + dr

    def occupies(self, col: int, row: int) -> bool:
        return (
            self.col <= col < self.col + self.footprint[0]
            and self.row <= row < self.row + self.footprint[1]
        )

    def take_damage(self, amount: float) -> None:
        self._pending_damage += max(0.0, amount)

    def heal(self, amount: float) -> None:
        self._pending_heal += max(0.0, amount)

    def repair(self, amount: float) -> None:
        self._pending_repair += max(0.0, amount)

    def resolve(self, controller: "DamageResolver") -> "DamageResult":
        return controller.resolve(self)

    def reset_pending(self) -> None:
        self._pending_damage = 0.0
        self._pending_heal = 0.0
        self._pending_repair = 0.0

    def get_path(
        self,
        grid: "WorldGrid",
        goal: tuple[int, int],
        mode: PathMode = PathMode.PATHFAST,
    ) -> list[tuple[int, int]]:
        return grid.find_path((self.col, self.row), goal, mode)
