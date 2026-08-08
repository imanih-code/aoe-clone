from __future__ import annotations

import pygame


class AudioManager:
    def __init__(self) -> None:
        pygame.mixer.init()
        self._ambient: dict[str, pygame.mixer.Sound] = {}
        self._focused: dict[str, pygame.mixer.Sound] = {}
        self.ambient_volume = 0.7
        self.focused_volume = 0.9

    def load_ambient(self, name: str, path: str) -> None:
        self._ambient[name] = pygame.mixer.Sound(path)

    def load_focused(self, name: str, path: str) -> None:
        self._focused[name] = pygame.mixer.Sound(path)

    def play_ambient(self, name: str, loops: int = -1) -> None:
        sound = self._ambient.get(name)
        if sound is not None:
            sound.set_volume(self.ambient_volume)
            sound.play(loops)

    def stop_ambient(self, name: str | None = None) -> None:
        if name is not None:
            sound = self._ambient.get(name)
            if sound is not None:
                sound.stop()
            return
        for sound in self._ambient.values():
            sound.stop()

    def play_focused(self, name: str, volume: float | None = None) -> None:
        sound = self._focused.get(name)
        if sound is not None:
            sound.set_volume(self.focused_volume if volume is None else volume)
            sound.play()

    def stop_all(self) -> None:
        self.stop_ambient()
        for sound in self._focused.values():
            sound.stop()
