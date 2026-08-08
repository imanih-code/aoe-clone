# AoE Clone

An Age of Empires clone: a generic RTS engine built as a real-time strategy
wrapper **without hardcoding mechanics or game variables**.

## Concept

Instead of fixing rules in code (costs, health, production, attributes...),
the engine exposes a set of primitives configurable at runtime:

- **Entities**: units and buildings defined by data (JSON/schema), not by
  hardcoded classes
- **Mechanics**: selection, movement, gathering, combat, construction,
  production — implemented as decoupled systems
- **Configuration**: balance (costs, timings, damage, HP) declared in data and
  editable without touching code
- **RTS wrapper**: a reusable architecture for any RTS, not just this clone

The goal is that changing any game variable (how much health does a villager
have? how much does a knight cost?) is only editing data, never rewriting logic.

## Current state

Provisional isometric renderer with a generated world, edge-scroll camera and a
CLI to start a match. Modular structure:

```
src/
  engine/    pygame wrappers: input/keybindings, animation, audio, HUD,
             iso renderer, camera (Screen)
  isoworld/  world mechanics: grid, entities, damage resolver, pathfinding,
             world generation
  game.py    provisional match loop
  cli.py     command line interface
```

## Run

```bash
pip install -r requirements.txt
python -m src.cli play --cols 40 --rows 40 --tile 64 --seed 7
```

Save a screenshot of the generated world without a window:

```bash
python -m src.cli play --seed 7 --screenshot /tmp/world.png
```

## Controls

- Mouse at screen edge: pan the camera
- `ESC`: quit

## Stack

- Python 3
- pygame-ce (2D rendering)
