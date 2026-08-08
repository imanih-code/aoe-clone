from __future__ import annotations

import pygame


class Animation:
    def __init__(
        self,
        frames: list[pygame.Surface],
        fps: float = 10.0,
        loop: bool = True,
    ) -> None:
        if not frames:
            raise ValueError("an animation needs at least one frame")
        self.frames = frames
        self.fps = fps
        self.loop = loop
        self.frame_duration = 1.0 / fps
        self.index = 0
        self.elapsed = 0.0
        self.done = False

    def update(self, dt: float) -> None:
        if self.done:
            return
        self.elapsed += dt
        while self.elapsed >= self.frame_duration:
            self.elapsed -= self.frame_duration
            self.index += 1
            if self.index >= len(self.frames):
                if self.loop:
                    self.index = 0
                else:
                    self.index = len(self.frames) - 1
                    self.done = True
                    return

    def current_frame(self) -> pygame.Surface:
        return self.frames[self.index]

    def reset(self) -> None:
        self.index = 0
        self.elapsed = 0.0
        self.done = False


class SpriteSheet:
    def __init__(self, image: pygame.Surface, cols: int, rows: int) -> None:
        w = image.get_width() // cols
        h = image.get_height() // rows
        self.frame_size = (w, h)
        self._frames = [
            image.subsurface((c * w, r * h, w, h))
            for r in range(rows)
            for c in range(cols)
        ]

    def frames(self, start: int = 0, end: int | None = None, step: int = 1) -> list[pygame.Surface]:
        return self._frames[start:end:step]


class AnimationController:
    def __init__(self, animations: dict[str, Animation] | None = None) -> None:
        self._animations = animations or {}
        self.current = ""
        if self._animations:
            self.current = next(iter(self._animations))

    def add(self, name: str, animation: Animation) -> None:
        self._animations[name] = animation
        if not self.current:
            self.current = name

    def switch(self, name: str, reset: bool = True) -> bool:
        if name not in self._animations or name == self.current:
            return False
        self.current = name
        if reset:
            self._animations[name].reset()
        return True

    def update(self, dt: float) -> None:
        if self.current:
            self._animations[self.current].update(dt)

    def current_frame(self) -> pygame.Surface | None:
        anim = self._animations.get(self.current)
        return anim.current_frame() if anim else None

    def current_animation(self) -> Animation | None:
        return self._animations.get(self.current)
