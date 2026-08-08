from __future__ import annotations

from collections.abc import Iterable

import pygame


class KeyBindings:
    def __init__(self, bindings: dict[str, int | Iterable[int]] | None = None):
        self._bindings: dict[str, set[int]] = {}
        self._pressed: set[str] = set()
        self._held: set[str] = set()
        self._released: set[str] = set()
        if bindings:
            for action, keys in bindings.items():
                self.bind(action, keys)

    def bind(self, action: str, keys: int | Iterable[int]) -> None:
        if isinstance(keys, int):
            keys = [keys]
        self._bindings.setdefault(action, set()).update(keys)

    def unbind(self, action: str, key: int) -> None:
        self._bindings.get(action, set()).discard(key)

    def rebind(self, action: str, key: int) -> None:
        self._bindings[action] = {key}

    def actions(self) -> tuple[str, ...]:
        return tuple(self._bindings)

    def keys_for(self, action: str) -> frozenset[int]:
        return frozenset(self._bindings.get(action, ()))

    def begin_frame(self) -> None:
        self._pressed.clear()
        self._released.clear()

    def update(self, keys) -> None:
        for action, bound in self._bindings.items():
            down = any(keys[k] for k in bound)
            was_held = action in self._held
            if down and not was_held:
                self._pressed.add(action)
            elif was_held and not down:
                self._released.add(action)
            if down:
                self._held.add(action)
            else:
                self._held.discard(action)

    def is_pressed(self, action: str) -> bool:
        return action in self._pressed

    def is_held(self, action: str) -> bool:
        return action in self._held

    def is_released(self, action: str) -> bool:
        return action in self._released
