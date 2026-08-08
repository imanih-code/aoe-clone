from __future__ import annotations

import random
from typing import Any

from .entity import GameEntity, GameEntityType
from .grid import WorldGrid

_TREE: dict[str, Any] = {
    "kind": GameEntityType.RESOURCE,
    "footprint": (1, 1),
    "max_hp": 50,
}
_ROCK: dict[str, Any] = {
    "kind": GameEntityType.RESOURCE,
    "footprint": (1, 1),
    "max_hp": 120,
}
_BASE: dict[str, Any] = {
    "kind": GameEntityType.BUILDING,
    "footprint": (3, 3),
    "max_hp": 600,
}
_HOUSE: dict[str, Any] = {
    "kind": GameEntityType.BUILDING,
    "footprint": (2, 2),
    "max_hp": 250,
}
_UNIT: dict[str, Any] = {
    "kind": GameEntityType.UNIT,
    "footprint": (1, 1),
    "max_hp": 40,
    "blocks_movement": False,
}


class WorldGenerator:
    def __init__(self, seed: int | None = None) -> None:
        self.seed = seed
        self.random = random.Random(seed)

    def _place(self, world: WorldGrid, config: dict[str, Any], col: int, row: int) -> GameEntity | None:
        if not world.is_rect_free(col, row, *config["footprint"]):
            return None
        entity = GameEntity(
            config["kind"],
            col=col,
            row=row,
            footprint=config["footprint"],
            max_hp=config["max_hp"],
            blocks_movement=config.get("blocks_movement", True),
        )
        world.add_entity(entity)
        return entity

    def generate(self, world: WorldGrid) -> None:
        rng = self.random
        cols, rows = world.cols, world.rows

        self._place(world, _BASE, 2, 2)
        if cols > 8 and rows > 8:
            self._place(world, _BASE, cols - 5, rows - 5)

        for _ in range(6):
            self._place(world, _HOUSE, rng.randrange(1, max(2, cols - 3)), rng.randrange(1, max(2, rows - 3)))

        for _ in range(int(cols * rows * 0.06)):
            self._place(world, _TREE, rng.randrange(cols), rng.randrange(rows))

        for _ in range(int(cols * rows * 0.02)):
            self._place(world, _ROCK, rng.randrange(cols), rng.randrange(rows))

        for _ in range(int(cols * rows * 0.01) + 4):
            self._place(world, _UNIT, rng.randrange(cols), rng.randrange(rows))
