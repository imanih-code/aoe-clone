# AoE Clone (working title)

A classic historical real-time strategy game — villagers, resource gathering,
ages, technologies, armies, conversion — written from scratch in **C++20 with
SFML 3**, on purpose without a game engine.

The engine is a generic, **data-driven RTS core**: units, buildings, techs,
civilizations, ages and balance live in `data/*.json`, never in code. Changing
how much health a villager has or what a knight costs is editing data, not
recompiling.

The game borrows the *genre's* mechanics, not another game's content: civ,
unit and tech names, art, sound and maps must be original (or properly
licensed). See [architecture.md §11.7](architecture.md).

## Status

| Part | State |
|---|---|
| Design (`architecture.md` v3, `mutability.md`) | Reviewed and up to date |
| Phase 0 — C++/SFML skeleton | Done: CMake presets, fixed-tick loop, `EntityHandle`/`SlotMap`, `Fixed`, `Rng`, tests |
| Phase 1 — map and camera | Done: generated iso map (lakes, shores, forests, mines), chunked terrain, pan/zoom camera |
| Phase 2 — entities and movement | Mostly done: data-driven entities, labeled placeholder shapes, selection (click, box, shift, double click), right-click move with A* and formation. Pending: perf target of 200 units |
| Phase 3 — economy | Done: data inheritance (`extends`), per-sheet capabilities, gather/deposit cycle, construction sites with several builders, houses and population, training queues with cancel and rally points, command panel with grid hotkeys, contextual right click |
| Phase 4 — combat, techs, civs | Done: armor-class damage, melee and ranged combat with arrows as entities, auto-attack and retaliation, towers, modifier resolver with per-player effective stats, 3 ages, techs, upgrade lines, 3 original civilizations with bonuses and unique units |
| Phase 5+ | Fog of war, AI, victory and replays next — see [`ROADMAP.md`](ROADMAP.md) |
| `src/*.py`, `main.py`, `requirements.txt` | Legacy Python/pygame prototype, superseded by the C++ code; to be removed |

Graphics are **labeled placeholder shapes** for now. Each entity's look comes from the `visual` block of its JSON, so swapping in sprites later (original or CC0-licensed) does not touch game code.

## Documentation

| File | What it covers |
|---|---|
| [`architecture.md`](architecture.md) | Layers, entity handles, modifiers, damage model, data layer, world, game loop, determinism, SFML rendering, missing systems (fog, economy, AI, replays) |
| [`mutability.md`](mutability.md) | Capabilities, action configs, action behaviors and triggers (what a unit *can do*, changed at runtime) |
| [`ROADMAP.md`](ROADMAP.md) | Build order: what to implement first and what "done" means for each phase |

Diagrams are PlantUML; paste them into [PlantUML Online](https://www.plantuml.com/plantuml/uml/) to render.

## Stack

- **C++20** (GCC 12+, Clang 15+ or MSVC 19.3x)
- **SFML 3.1.0** — graphics and window (audio and network modules disabled for now)
- **CMake 3.25+** and **Ninja** — dependencies fetched with `FetchContent` through git, nothing to install by hand
- **nlohmann/json 3.11.3** — data files (comments allowed)
- **doctest 2.4.11** — unit tests for the simulation (`sim` builds without SFML)

## Build

### Windows (tested with GCC 16.2 from WinLibs)

```bash
winget install --source winget BrechtSanders.WinLibs.POSIX.UCRT
```

WinLibs ships GCC, CMake and Ninja in its `mingw64/bin`. Make sure that folder is on `PATH` (open a new terminal after installing), then:

```bash
cmake --preset windows-release
```

```bash
cmake --build --preset windows-release
```

```bash
./build/windows-release/aoe_clone.exe
```

Visual Studio 2022 works too: run the same commands from a *Developer PowerShell for VS 2022*. The first configure clones SFML, FreeType and HarfBuzz and takes a few minutes. If it stops with *Permission denied* on a file under `build/`, an antivirus scanning the fresh clone is the usual cause; run the configure again.

### Linux

```bash
sudo apt install build-essential cmake ninja-build libxrandr-dev libxcursor-dev libxi-dev libudev-dev libfreetype-dev libgl1-mesa-dev libegl1-mesa-dev
```

```bash
cmake --preset linux-release && cmake --build --preset linux-release
```

### Tests

```bash
ctest --preset windows-release
```

### Command-line options

| Option | Effect |
|---|---|
| `--seed N` | Map seed (same seed = same map) |
| `--players N` | 1 to 8 players (default 2) |
| `--size N` | Map size in tiles (default 96) |
| `--civ KEY` | Your civilization: `valdoria`, `tarvesh` or `ostreva` |
| `--demo` | Plays a few economy orders at start (gather, build, train) |
| `--battle` | Spawns two armies and a tower next to your base to watch combat |
| `--screenshot file.png [--frame N]` | Saves a capture at frame N (default 90) and exits |

## Controls

- Left click: select · drag: box select your units · `Shift`: add/remove · double click: all of that type on screen
- Right click with units: move (formation) · on a resource: gather · on your construction site: help build · on an enemy: attack
- Right click with a building selected: set its rally point
- Command panel (bottom center): built from the selection's data — what a villager can build, what a building trains, the production queue (click to cancel and refund). Hotkeys follow the grid: `Q W E R T` / `A S D F G` / `Z X C V B`
- Placing a building: left click to place (`Shift` to place several) · right click or `Esc` to cancel
- `.`: next idle villager · `Space`: center on selection
- Arrow keys, screen edges or middle-drag: pan · mouse wheel: zoom
- `L`: label mode (smart / all / none) · `F2`: grid · `H` or `F1`: help · `Esc`: clear selection

## Adding content

Every unit, building and resource is a JSON file under `data/entities/`. A new sheet can inherit from another with `"extends"` and only override what changes; templates marked `"abstract": true` exist only to be inherited:

```json
{
  "key": "mill",
  "extends": "_camp_base",
  "name": "Molino",
  "dropoff": ["food"],
  "visual": { "label": "Molino" }
}
```

To make a villager able to build it, add its key to the villager's `"builds"` list; to train a unit somewhere, add it to that building's `"trains"` list. The command panel picks both up automatically.

Labels use `assets/fonts/ui.ttf` if present, otherwise a system font (Segoe UI / Arial / DejaVu Sans).
