from __future__ import annotations

import colorsys

import pygame

GRASS_COLOR = (102, 168, 102)


def saturate_darken(color: tuple[int, int, int], darken: float = 0.72, saturate: float = 1.35) -> tuple[int, int, int]:
    r, g, b = (v / 255.0 for v in color[:3])
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    s = min(1.0, s * saturate)
    v = max(0.0, v * darken)
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return (int(r * 255), int(g * 255), int(b * 255))


class IsoRenderer:
    def __init__(
        self,
        tile_w: int,
        tile_h: int,
        tile_color: tuple[int, int, int] = GRASS_COLOR,
        border_thickness: int | None = None,
    ) -> None:
        self.tile_w = tile_w
        self.tile_h = tile_h
        self.half_w = tile_w // 2
        self.half_h = tile_h // 2
        self.tile_color = tile_color
        self.border_color = saturate_darken(tile_color)
        self.border_thickness = border_thickness if border_thickness is not None else max(1, (tile_w + tile_h) // 24)

    def project(self, col: int, row: int) -> tuple[int, int]:
        return ((col - row) * self.half_w, (col + row) * self.half_h)

    def draw_diamond(
        self,
        surface: pygame.Surface,
        cx: int,
        cy: int,
        half_w: int,
        half_h: int,
        color: tuple[int, int, int],
        border_color: tuple[int, int, int] | None = None,
        border_thickness: int | None = None,
    ) -> None:
        outer = [(cx, cy - half_h), (cx + half_w, cy), (cx, cy + half_h), (cx - half_w, cy)]
        if border_color is None:
            pygame.draw.polygon(surface, color, outer)
            return
        t = border_thickness if border_thickness is not None else self.border_thickness
        inner = [(cx, cy - half_h + t), (cx + half_w - t, cy), (cx, cy + half_h - t), (cx - half_w + t, cy)]
        pygame.draw.polygon(surface, border_color, outer)
        pygame.draw.polygon(surface, color, inner)

    def draw_tile(
        self,
        surface: pygame.Surface,
        col: int,
        row: int,
        origin: tuple[int, int] = (0, 0),
        color: tuple[int, int, int] | None = None,
    ) -> None:
        cx, cy = self.project(col, row)
        self.draw_diamond(
            surface,
            cx + origin[0],
            cy + origin[1],
            self.half_w,
            self.half_h,
            color if color is not None else self.tile_color,
            self.border_color,
        )
