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
| Roadmap (`ROADMAP.md`) | Phases 0–7 defined |
| C++ code | **Not started** — next step is Phase 0 |
| `src/*.py`, `main.py`, `requirements.txt` | Legacy Python/pygame prototype (iso renderer, A*, damage resolver). Kept only as reference; to be removed once Phase 1 is ported |

## Documentation

| File | What it covers |
|---|---|
| [`architecture.md`](architecture.md) | Layers, entity handles, modifiers, damage model, data layer, world, game loop, determinism, SFML rendering, missing systems (fog, economy, AI, replays) |
| [`mutability.md`](mutability.md) | Capabilities, action configs, action behaviors and triggers (what a unit *can do*, changed at runtime) |
| [`ROADMAP.md`](ROADMAP.md) | Build order: what to implement first and what "done" means for each phase |

Diagrams are PlantUML; paste them into [PlantUML Online](https://www.plantuml.com/plantuml/uml/) to render.

## Stack

- **C++20** (MSVC 19.3x / VS 2022, GCC 12+, or Clang 15+)
- **SFML 3.1.0** — graphics, window, audio
- **CMake 3.24+** — dependencies fetched with `FetchContent`, nothing to install by hand
- **nlohmann/json 3.11.3** — data files
- **doctest** — unit tests for the simulation

## Build (once Phase 0 lands)

### Windows

1. Install **Visual Studio 2022** (or Build Tools) with the *Desktop development with C++* workload, which includes CMake and Ninja. Alternatively: `winget install Kitware.CMake`.
2. From a *Developer PowerShell for VS 2022*:

```bash
cmake --preset windows-release
```

```bash
cmake --build --preset windows-release
```

```bash
./build/windows-release/aoe_clone.exe
```

### Linux

```bash
sudo apt install build-essential cmake ninja-build libxrandr-dev libxcursor-dev libxi-dev libudev-dev libfreetype-dev libflac-dev libvorbis-dev libgl1-mesa-dev libegl1-mesa-dev
```

```bash
cmake --preset linux-release && cmake --build --preset linux-release
```

### Tests

```bash
ctest --preset windows-release
```

### How dependencies are wired

```cmake
cmake_minimum_required(VERSION 3.24)
project(aoe_clone LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.1.0
    GIT_SHALLOW ON
    SYSTEM)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(SFML json)

# Simulation: no SFML Graphics/Window, testable headless
add_library(sim STATIC ...)
target_link_libraries(sim PUBLIC nlohmann_json::nlohmann_json)

# Game executable: render + input + app
add_executable(aoe_clone src/app/main.cpp ...)
target_link_libraries(aoe_clone PRIVATE sim SFML::Graphics SFML::Audio)
```

On Windows, copy `data/` and `assets/` next to the executable with a post-build step (or set the working directory in the preset).

## Controls (planned)

- Left click / drag: select · double click: select all of that type on screen
- Right click: context order (move, gather, attack, build, garrison)
- Screen edges, arrow keys or middle-drag: pan · mouse wheel: zoom
- `Ctrl+1..9`: control groups · `.`: next idle villager · `Esc`: menu
