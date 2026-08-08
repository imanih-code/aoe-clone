from __future__ import annotations

import pygame

from src.engine import IsoRenderer, Screen
from src.isoworld import WorldGrid
from src.isoworld.generation import WorldGenerator

WINDOW_W = 1024
WINDOW_H = 700
FPS = 60
BG_COLOR = (18, 22, 18)
HUD_COLOR = (12, 16, 12)


def world_bounds(cols: int, rows: int, renderer: IsoRenderer) -> tuple[int, int, int, int]:
    half_w, half_h = renderer.half_w, renderer.half_h
    min_x = -(rows - 1) * half_w - half_w
    min_y = -half_h
    max_x = (cols - 1) * half_w + half_w
    max_y = (cols + rows - 2) * half_h + half_h
    return (min_x, min_y, max_x, max_y)


def build_match(
    cols: int = 40,
    rows: int = 40,
    tile_w: int = 64,
    seed: int | None = None,
) -> tuple[WorldGrid, Screen, IsoRenderer]:
    renderer = IsoRenderer(tile_w, max(16, tile_w // 2))
    world = WorldGrid(cols, rows, renderer)
    WorldGenerator(seed).generate(world)
    screen = Screen(WINDOW_W, WINDOW_H)
    screen.set_bounds(*world_bounds(cols, rows, renderer))
    screen.center_view()
    return world, screen, renderer


def _draw_hud(surface: pygame.Surface, font: pygame.font.Font, world: WorldGrid, screen: Screen, seed: int | None) -> None:
    pygame.draw.rect(surface, HUD_COLOR, (0, 0, WINDOW_W, 26))
    cx, cy = screen.offset()
    info = (
        f"seed={seed}  |  entities={world.entity_count}  |  "
        f"camera=({cx},{cy})  |  ESC: quit  |  mouse at edge: pan camera"
    )
    surface.blit(font.render(info, True, (200, 210, 200)), (10, 6))


def run_match(
    cols: int = 40,
    rows: int = 40,
    tile_w: int = 64,
    seed: int | None = None,
    frames: int | None = None,
    screenshot_path: str | None = None,
) -> None:
    print(f"Starting provisional match: map {cols}x{rows}, tile {tile_w}px, seed={seed}")
    pygame.init()
    display = pygame.display.set_mode((WINDOW_W, WINDOW_H))
    pygame.display.set_caption("AoE Clone - provisional match (CLI)")
    clock = pygame.time.Clock()
    font = pygame.font.Font(None, 20)

    world, screen, renderer = build_match(cols, rows, tile_w, seed)

    if screenshot_path is not None:
        display.fill(BG_COLOR)
        world.render(display, origin=screen.offset())
        _draw_hud(display, font, world, screen, seed)
        pygame.image.save(display, screenshot_path)
        print(f"Screenshot saved to {screenshot_path}")
        pygame.quit()
        return

    print(f"Window opened ({WINDOW_W}x{WINDOW_H}). Move the mouse to the screen edges to pan the camera. Press ESC to quit.")

    running = True
    counter = frames if frames is not None else -1
    while running:
        dt = clock.tick(FPS) / 1000.0

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            if event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                running = False

        screen.update(dt, pygame.mouse.get_pos())

        display.fill(BG_COLOR)
        world.render(display, origin=screen.offset())
        _draw_hud(display, font, world, screen, seed)
        pygame.display.flip()

        if counter > 0:
            counter -= 1
        elif counter == 0:
            running = False

    pygame.quit()
