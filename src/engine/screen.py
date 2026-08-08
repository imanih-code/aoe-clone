from __future__ import annotations


class Screen:
    def __init__(
        self,
        width: int,
        height: int,
        scroll_speed: float = 480.0,
        edge_margin: int = 48,
    ) -> None:
        self.width = width
        self.height = height
        self.scroll_speed = scroll_speed
        self.edge_margin = edge_margin
        self.origin_x = 0.0
        self.origin_y = 0.0
        self._bounds: tuple[int, int, int, int] | None = None

    def set_bounds(self, min_x: int, min_y: int, max_x: int, max_y: int) -> None:
        self._bounds = (min_x, min_y, max_x, max_y)
        self._clamp()

    def center_on(self, x: float, y: float) -> None:
        self.origin_x = x
        self.origin_y = y
        self._clamp()

    def center_view(self) -> None:
        if self._bounds is None:
            return
        min_x, min_y, max_x, max_y = self._bounds
        self.origin_x = self.width / 2.0 - (min_x + max_x) / 2.0
        self.origin_y = self.height / 2.0 - (min_y + max_y) / 2.0
        self._clamp()

    def offset(self) -> tuple[int, int]:
        return (int(round(self.origin_x)), int(round(self.origin_y)))

    def update(self, dt: float, mouse_pos: tuple[int, int]) -> None:
        mx, my = mouse_pos
        vx = 0.0
        vy = 0.0
        if mx <= self.edge_margin:
            vx = 1.0
        elif mx >= self.width - self.edge_margin:
            vx = -1.0
        if my <= self.edge_margin:
            vy = 1.0
        elif my >= self.height - self.edge_margin:
            vy = -1.0
        if vx != 0.0 or vy != 0.0:
            self.origin_x += vx * self.scroll_speed * dt
            self.origin_y += vy * self.scroll_speed * dt
            self._clamp()

    def _clamp(self) -> None:
        if self._bounds is None:
            return
        min_x, min_y, max_x, max_y = self._bounds
        if max_x - min_x <= self.width:
            self.origin_x = self.width / 2.0 - (min_x + max_x) / 2.0
        else:
            self.origin_x = max(-(max_x - self.width), min(self.origin_x, -min_x))
        if max_y - min_y <= self.height:
            self.origin_y = self.height / 2.0 - (min_y + max_y) / 2.0
        else:
            self.origin_y = max(-(max_y - self.height), min(self.origin_y, -min_y))
