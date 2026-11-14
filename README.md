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
| Phase 3+ | See [`ROADMAP.md`](ROADMAP.md) |
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
| `--demo` | Selects your units and sends them walking at start |
| `--screenshot file.png [--frame N]` | Saves a capture at frame N (default 90) and exits |

## Controls

- Left click: select · drag: box select your units · `Shift`: add/remove · double click: all of that type on screen
- Right click: move (units spread in a formation around the point) · `S`: stop · `Space`: center on selection
- Arrow keys, screen edges or middle-drag: pan · mouse wheel: zoom
- `L`: label mode (smart / all / none) · `G`: grid · `H`: help · `Esc`: clear selection

Labels use `assets/fonts/ui.ttf` if present, otherwise a system font (Segoe UI / Arial / DejaVu Sans).
