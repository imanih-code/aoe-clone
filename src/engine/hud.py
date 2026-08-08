from __future__ import annotations

import pygame

WHITE = (255, 255, 255)


class TextLabel:
    def __init__(
        self,
        font: pygame.font.Font,
        text: str = "",
        color: tuple[int, int, int] = WHITE,
    ) -> None:
        self.font = font
        self.color = color
        self._text = ""
        self.image = font.render("", True, color)
        self.set_text(text)

    def set_text(self, text: str) -> None:
        if text != self._text:
            self._text = text
            self.image = self.font.render(text, True, self.color)

    @property
    def text(self) -> str:
        return self._text

    def draw(self, surface: pygame.Surface, pos: tuple[int, int]) -> None:
        surface.blit(self.image, pos)


class ProgressBar:
    def __init__(
        self,
        rect: pygame.Rect,
        fill_color: tuple[int, int, int],
        back_color: tuple[int, int, int],
        border_color: tuple[int, int, int] | None = None,
    ) -> None:
        self.rect = pygame.Rect(rect)
        self.fill_color = fill_color
        self.back_color = back_color
        self.border_color = border_color

    def draw(self, surface: pygame.Surface, ratio: float) -> None:
        ratio = max(0.0, min(1.0, ratio))
        surface.fill(self.back_color, self.rect)
        if ratio > 0.0:
            fill = self.rect.copy()
            fill.width = int(self.rect.width * ratio)
            surface.fill(self.fill_color, fill)
        if self.border_color:
            pygame.draw.rect(surface, self.border_color, self.rect, 1)


class Panel:
    def __init__(
        self,
        rect: pygame.Rect,
        color: tuple[int, int, int],
        border_color: tuple[int, int, int] | None = None,
    ) -> None:
        self.rect = pygame.Rect(rect)
        self.color = color
        self.border_color = border_color

    def draw(self, surface: pygame.Surface) -> None:
        surface.fill(self.color, self.rect)
        if self.border_color:
            pygame.draw.rect(surface, self.border_color, self.rect, 1)


class HUD:
    def __init__(self) -> None:
        self._elements: list = []

    def add(self, element) -> None:
        self._elements.append(element)

    def draw(self, surface: pygame.Surface) -> None:
        for element in self._elements:
            element.draw(surface)
