from __future__ import annotations

from collections.abc import Iterator
from typing import TYPE_CHECKING, Any

from .entity import GameEntityType
from .pathfinding import PathFinder, PathMode

if TYPE_CHECKING:
    from ..engine.iso import IsoRenderer
    from .entity import GameEntity

_DIRS_4 = ((1, 0), (-1, 0), (0, 1), (0, -1))
_DIRS_8 = _DIRS_4 + ((1, 1), (1, -1), (-1, 1), (-1, -1))

_ENTITY_COLORS: dict[GameEntityType, tuple[int, int, int]] = {
    GameEntityType.BUILDING: (150, 155, 165),
    GameEntityType.RESOURCE: (172, 138, 66),
    GameEntityType.UNIT: (92, 132, 205),
}


class WorldGrid:
    def __init__(self, cols: int, rows: int, renderer: "IsoRenderer | None" = None) -> None:
        if cols <= 0 or rows <= 0:
            raise ValueError("grid must have positive dimensions")
        self.cols = cols
        self.rows = rows
        self._entities: dict[int, "GameEntity"] = {}
        self._occupancy: list[set[int]] = [set() for _ in range(cols * rows)]
        self._blocked: list[bool] = [False] * (cols * rows)
        self._pathfinder: PathFinder | None = None
        self.renderer = renderer

    def set_renderer(self, renderer: "IsoRenderer") -> None:
        self.renderer = renderer

    def render(
        self,
        surface: Any,
        origin: tuple[int, int] = (0, 0),
        cell_color: tuple[int, int, int] | None = None,
        entity_colors: dict[GameEntityType, tuple[int, int, int]] | None = None,
    ) -> None:
        if self.renderer is None:
            return
        renderer = self.renderer
        ox, oy = origin
        for row in range(self.rows):
            for col in range(self.cols):
                renderer.draw_tile(surface, col, row, origin, cell_color)
        colors = entity_colors if entity_colors is not None else _ENTITY_COLORS
        for entity in self._entities.values():
            color = colors.get(entity.kind)
            if color is None:
                continue
            cx, cy = renderer.project(entity.col, entity.row)
            half_w = renderer.half_w * entity.footprint[0]
            half_h = renderer.half_h * entity.footprint[1]
            renderer.draw_diamond(surface, cx + ox, cy + oy, half_w, half_h, color)

    @property
    def size(self) -> int:
        return self.cols * self.rows

    @property
    def entity_count(self) -> int:
        return len(self._entities)

    def _index(self, col: int, row: int) -> int:
        return row * self.cols + col

    def in_bounds(self, col: int, row: int) -> bool:
        return 0 <= col < self.cols and 0 <= row < self.rows

    def in_bounds_rect(self, col: int, row: int, width: int, height: int) -> bool:
        return (
            col >= 0
            and row >= 0
            and col + width <= self.cols
            and row + height <= self.rows
        )

    def neighbors(self, col: int, row: int, diagonal: bool = False) -> Iterator[tuple[int, int]]:
        for dc, dr in (_DIRS_8 if diagonal else _DIRS_4):
            nc, nr = col + dc, row + dr
            if self.in_bounds(nc, nr):
                yield nc, nr

    def entity_by_id(self, entity_id: int) -> "GameEntity | None":
        return self._entities.get(entity_id)

    def find_path(
        self,
        start: tuple[int, int],
        goal: tuple[int, int],
        mode: PathMode = PathMode.PATHFAST,
    ) -> list[tuple[int, int]]:
        if self._pathfinder is None:
            self._pathfinder = PathFinder(self)
        return self._pathfinder.find(start, goal, mode)

    def entities(self) -> tuple["GameEntity", ...]:
        return tuple(self._entities.values())

    def entity_ids_at(self, col: int, row: int) -> tuple[int, ...]:
        if not self.in_bounds(col, row):
            return ()
        return tuple(self._occupancy[self._index(col, row)])

    def entities_at(self, col: int, row: int) -> tuple["GameEntity", ...]:
        if not self.in_bounds(col, row):
            return ()
        return tuple(
            self._entities[eid]
            for eid in self._occupancy[self._index(col, row)]
        )

    def entities_in_area(self, col: int, row: int, width: int, height: int) -> tuple["GameEntity", ...]:
        found: list["GameEntity"] = []
        seen: set[int] = set()
        c0 = max(0, col)
        r0 = max(0, row)
        c1 = min(self.cols, col + width)
        r1 = min(self.rows, row + height)
        for r in range(r0, r1):
            for c in range(c0, c1):
                for eid in self._occupancy[self._index(c, r)]:
                    if eid not in seen:
                        seen.add(eid)
                        found.append(self._entities[eid])
        return tuple(found)

    def entities_in_range(self, col: int, row: int, radius: int) -> tuple["GameEntity", ...]:
        if radius < 0:
            return ()
        d = radius
        return self.entities_in_area(col - d, row - d, 2 * d + 1, 2 * d + 1)

    def is_cell_blocked(self, col: int, row: int) -> bool:
        if not self.in_bounds(col, row):
            return True
        return self._blocked[self._index(col, row)]

    def is_rect_free(self, col: int, row: int, width: int, height: int) -> bool:
        if not self.in_bounds_rect(col, row, width, height):
            return False
        for dc in range(width):
            for dr in range(height):
                if self._blocked[self._index(col + dc, row + dr)]:
                    return False
        return True

    def is_passable(self, col: int, row: int, ignore: "GameEntity | None" = None) -> bool:
        if not self.in_bounds(col, row):
            return False
        if not self._blocked[self._index(col, row)]:
            return True
        if ignore is not None:
            for eid in self._occupancy[self._index(col, row)]:
                if self._entities[eid] is ignore:
                    return True
        return False

    def add_entity(self, entity: "GameEntity") -> None:
        if entity.id in self._entities:
            raise ValueError(f"entity {entity.id} already in grid")
        if not self.in_bounds_rect(entity.col, entity.row, *entity.footprint):
            raise ValueError("entity footprint is out of bounds")
        if entity.blocks_movement and not self.is_rect_free(entity.col, entity.row, *entity.footprint):
            raise ValueError("entity footprint overlaps blocked cells")
        self._entities[entity.id] = entity
        for col, row in entity.occupied_cells():
            idx = self._index(col, row)
            self._occupancy[idx].add(entity.id)
            if entity.blocks_movement:
                self._blocked[idx] = True

    def remove_entity(self, entity: "GameEntity") -> bool:
        removed = self._entities.pop(entity.id, None)
        if removed is None:
            return False
        for col, row in removed.occupied_cells():
            idx = self._index(col, row)
            self._occupancy[idx].discard(removed.id)
            self._recompute_blocked(col, row)
        return True

    def move_entity(self, entity: "GameEntity", col: int, row: int) -> None:
        if entity.id not in self._entities:
            raise ValueError("entity is not in grid")
        if not self.in_bounds_rect(col, row, *entity.footprint):
            raise ValueError("target footprint is out of bounds")
        old_cells = set(entity.occupied_cells())
        new_cells = set()
        for dc in range(entity.footprint[0]):
            for dr in range(entity.footprint[1]):
                new_cells.add((col + dc, row + dr))
        if entity.blocks_movement:
            for c, r in new_cells - old_cells:
                if self._blocked[self._index(c, r)]:
                    raise ValueError("target overlaps blocked cells")
        for c, r in old_cells - new_cells:
            self._occupancy[self._index(c, r)].discard(entity.id)
            self._recompute_blocked(c, r)
        for c, r in new_cells - old_cells:
            self._occupancy[self._index(c, r)].add(entity.id)
            self._recompute_blocked(c, r)
        entity.col = col
        entity.row = row

    def _recompute_blocked(self, col: int, row: int) -> None:
        idx = self._index(col, row)
        self._blocked[idx] = any(
            self._entities[eid].blocks_movement
            for eid in self._occupancy[idx]
        )
