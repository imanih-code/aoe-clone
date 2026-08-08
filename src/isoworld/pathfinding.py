from __future__ import annotations

import heapq
from collections import deque
from enum import Enum, auto
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .grid import WorldGrid

SQRT2 = 2.0 ** 0.5
_MAX_SEGMENTS = 64
_MAX_GOAL_SEARCH = 4096


class PathMode(Enum):
    PATHFAST = auto()
    PATHMICRO = auto()


def _octile(a: tuple[int, int], b: tuple[int, int]) -> float:
    dc = abs(a[0] - b[0])
    dr = abs(a[1] - b[1])
    return max(dc, dr) + (SQRT2 - 1.0) * min(dc, dr)


def _line(a: tuple[int, int], b: tuple[int, int]) -> list[tuple[int, int]]:
    x0, y0 = a
    x1, y1 = b
    dx = abs(x1 - x0)
    sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0)
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    cells: list[tuple[int, int]] = []
    while True:
        cells.append((x0, y0))
        if x0 == x1 and y0 == y1:
            return cells
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


class PathFinder:
    def __init__(self, grid: "WorldGrid") -> None:
        self.grid = grid

    def find(
        self,
        start: tuple[int, int],
        goal: tuple[int, int],
        mode: PathMode = PathMode.PATHFAST,
    ) -> list[tuple[int, int]]:
        start = (int(start[0]), int(start[1]))
        goal = (int(goal[0]), int(goal[1]))
        if start == goal:
            return [start]
        if not self.grid.in_bounds(*start):
            return []
        goal = self._resolve_goal(goal)
        if goal is None:
            return []
        if mode is PathMode.PATHMICRO:
            return self._a_star(start, goal) or []
        return self._find_fast(start, goal)

    def _resolve_goal(self, goal: tuple[int, int]) -> tuple[int, int] | None:
        if self.grid.is_passable(*goal):
            return goal
        seen = {goal}
        queue = deque([goal])
        expanded = 0
        while queue:
            cell = queue.popleft()
            expanded += 1
            if expanded > _MAX_GOAL_SEARCH:
                return None
            for nb in self.grid.neighbors(cell[0], cell[1], diagonal=True):
                if nb in seen:
                    continue
                seen.add(nb)
                if self.grid.is_passable(*nb):
                    return nb
                queue.append(nb)
        return None

    def _can_move(self, a: tuple[int, int], b: tuple[int, int]) -> bool:
        if not self.grid.is_passable(*b):
            return False
        dc = b[0] - a[0]
        dr = b[1] - a[1]
        if dc and dr:
            if not self.grid.is_passable(a[0] + dc, a[1]):
                return False
            if not self.grid.is_passable(a[0], a[1] + dr):
                return False
        return True

    def _a_star(self, start: tuple[int, int], goal: tuple[int, int]) -> list[tuple[int, int]] | None:
        if not self.grid.is_passable(*goal):
            return None
        open_heap: list[tuple[float, tuple[int, int]]] = [(0.0, start)]
        g_score = {start: 0.0}
        came_from: dict[tuple[int, int], tuple[int, int]] = {}
        closed: set[tuple[int, int]] = set()
        while open_heap:
            _, node = heapq.heappop(open_heap)
            if node in closed:
                continue
            if node == goal:
                return self._reconstruct(came_from, start, goal)
            closed.add(node)
            for nb in self.grid.neighbors(node[0], node[1], diagonal=True):
                if nb in closed or not self._can_move(node, nb):
                    continue
                step = SQRT2 if (nb[0] != node[0] and nb[1] != node[1]) else 1.0
                tentative = g_score[node] + step
                if tentative < g_score.get(nb, float("inf")):
                    g_score[nb] = tentative
                    came_from[nb] = node
                    heapq.heappush(open_heap, (tentative + _octile(nb, goal), nb))
        return None

    def _reconstruct(
        self,
        came_from: dict[tuple[int, int], tuple[int, int]],
        start: tuple[int, int],
        goal: tuple[int, int],
    ) -> list[tuple[int, int]]:
        path = [goal]
        node = goal
        while node != start:
            node = came_from[node]
            path.append(node)
        path.reverse()
        return path

    def _find_fast(self, start: tuple[int, int], goal: tuple[int, int]) -> list[tuple[int, int]]:
        path: list[tuple[int, int]] = [start]
        current = start
        visited: set[tuple[int, int]] = set()
        start_distance = _octile(start, goal)

        for _ in range(_MAX_SEGMENTS):
            if self._line_clear(current, goal):
                return path[:-1] + _line(current, goal)

            line = _line(current, goal)
            blocked_idx = next(
                (i for i, cell in enumerate(line) if not self.grid.is_passable(*cell)),
                None,
            )
            if blocked_idx is None:
                return path[:-1] + line

            run_end = blocked_idx
            while run_end < len(line) and not self.grid.is_passable(*line[run_end]):
                run_end += 1
            run = set(line[blocked_idx:run_end])
            exit_on_line = line[run_end] if run_end < len(line) else None

            candidates: list[tuple[int, int]] = []
            line_set = set(line)
            for cell in line[blocked_idx:run_end]:
                for nb in self.grid.neighbors(cell[0], cell[1], diagonal=False):
                    if nb in run or nb in line_set:
                        continue
                    if self.grid.is_passable(*nb):
                        candidates.append(nb)
            if exit_on_line is not None:
                candidates.append(exit_on_line)

            best_segment: list[tuple[int, int]] | None = None
            best_cost = float("inf")
            for cand in candidates:
                segment = self._a_star(current, cand)
                if segment is None:
                    continue
                cost = len(segment) + _octile(cand, goal)
                if cost < best_cost:
                    best_cost = cost
                    best_segment = segment

            if best_segment is None or best_segment[-1] == current:
                return self._a_star(start, goal) or []
            if best_segment[-1] in visited:
                return self._a_star(start, goal) or []

            visited.add(best_segment[-1])
            path.extend(best_segment[1:])
            current = best_segment[-1]

            progress = start_distance - _octile(current, goal)
            if len(path) > max(24, int(progress * 1.75) + 16):
                return self._a_star(start, goal) or []

        return self._a_star(start, goal) or []

    def _line_clear(self, a: tuple[int, int], b: tuple[int, int]) -> bool:
        prev = a
        for cell in _line(a, b)[1:]:
            if not self._can_move(prev, cell):
                return False
            prev = cell
        return True
