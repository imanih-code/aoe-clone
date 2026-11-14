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
| Phase 5 — fog, AI, victory, replays | Mostly done: per-player fog of war respected by the simulation, smooth fog and clickable minimap, scripted AI (economy, farms, buildings, age-up, attack waves, defense), conquest victory with end screen, replays verified by state hash, speed x1–x8, pause, spectator mode. Pending: a full game played to victory has not been verified yet |
| Phase 5b — living world | Done: walkable 3x3 farms worked tile by tile, gather slots per resource, wildlife (livestock, fleeing prey, hostile wolves, retaliating boars, carcasses), shore and deep-water fish with dock and fishing boat, fog-aware pathfinding that replans on discovery, Shift order queues and villagers that start working what they just built |
| Phase 6 — mutability | Done: runtime capabilities and action configs changed by techs and per-unit modifiers, monk conversion and healing, garrison in town center and towers (extra arrows), triggers on death/kill/hit/spawn, skills (morph, aura, stat boost) with cooldowns. The four cases of `mutability.md` plus a fifth added with data only are integration tests |
| Phase 7 | Optional: save/load, lockstep multiplayer, map editor — see [`ROADMAP.md`](ROADMAP.md) |
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
| `--showcase` | Phase 6 demo: monks converting, a ritual warrior using its skill, a death knight and villagers garrisoning the town center |
| `--spectate` | Every player is AI and the whole map is visible |
| `--reveal` | No fog of war |
| `--no-ai` | Opponents stay idle (for testing) |
| `--speed N` | Start at x1, x2, x4 or x8 |
| `--replay file.json` | Play back a recorded game and check it stays in sync |
| `--record file.json` | Where to save the replay (default `replays/last_replay.json`) |
| `--screenshot file.png [--frame N]` | Saves a capture at frame N (default 90) and exits |

## Controls

- Left click: select · drag: box select your units · `Shift`: add/remove · double click: all of that type on screen
- Right click with units: move (formation) · on a resource: gather · on an animal: hunt (villagers) or attack (troops) · on your construction site: help build · on an enemy: attack
- `Shift` + any order: add it to each unit's queue; the queued route is drawn with yellow dots. `Shift` while placing buildings: several foundations, built one after another
- `Ctrl` + right click: attack-move (walk there fighting anything on the way)
- Monks: right click an enemy to convert it, a wounded ally to heal it
- `Alt` + right click on your town center or tower: garrison (troops garrison with a plain right click); the building's **Ungarrison** button lets them out
- Unit skills (sacrifice, prayer, battle cry) show up on the `Z X C V` row of the command panel with their cooldown
- Minimap (bottom right): click or drag to move the camera
- Right click with a building selected: set its rally point
- Command panel (bottom center): built from the selection's data — what a villager can build, what a building trains, the production queue (click to cancel and refund). Hotkeys follow the grid: `Q W E R T` / `A S D F G` / `Z X C V B`
- Placing a building: left click to place (`Shift` to place several) · right click or `Esc` to cancel
- `.`: next idle villager · `Space`: center on selection
- Arrow keys, screen edges or middle-drag: pan · mouse wheel: zoom
- `L`: label mode (smart / all / none) · `F2`: grid · `H` or `F1`: help · `Esc`: clear selection
- `+` / `-`: game speed · `P`: pause · `Esc` or `Enter` on the end screen: quit

Every game is recorded to `replays/last_replay.json` when it ends or the window closes.

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
