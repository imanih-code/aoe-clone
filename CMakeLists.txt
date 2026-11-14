cmake_minimum_required(VERSION 3.25...4.0)

# Algunas dependencias declaran versiones de CMake < 3.5; CMake 4 las rechaza sin esto.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(aoe_clone VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AOE_BUILD_TESTS "Compilar los tests de la simulacion" ON)

# ─── Dependencias ────────────────────────────────────────────────────────────
include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.1.0
    GIT_SHALLOW ON
    SYSTEM)

# Por git (no por URL): el curl interno de algunas distribuciones de CMake en Windows
# no reconoce los certificados del sistema, y git sí.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW ON
    SYSTEM)

# doctest es de un solo header: SOURCE_SUBDIR inexistente = solo descargar, no add_subdirectory
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW ON
    SOURCE_SUBDIR no_cmake)

FetchContent_MakeAvailable(SFML nlohmann_json)

# ─── Opciones comunes de compilación ────────────────────────────────────────
add_library(aoe_warnings INTERFACE)
if(MSVC)
    target_compile_options(aoe_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    # missing-field-initializers: los structs de datos usan valores por defecto a propósito
    target_compile_options(aoe_warnings INTERFACE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# ─── Simulación + datos (sin SFML: se puede testear sin ventana) ─────────────
add_library(sim STATIC
    src/data/data_registry.cpp
    src/sim/world/tile_map.cpp
    src/sim/world/spatial_grid.cpp
    src/sim/world/pathfinder.cpp
    src/sim/game/game_state.cpp
    src/sim/game/map_generation.cpp
    src/sim/actions/actions.cpp
    src/sim/game/game_combat.cpp
    src/sim/modifiers/modifier_resolver.cpp
)
target_include_directories(sim PUBLIC src)
target_link_libraries(sim PUBLIC nlohmann_json::nlohmann_json PRIVATE aoe_warnings)

# ─── Juego ───────────────────────────────────────────────────────────────────
add_executable(aoe_clone
    src/app/main.cpp
    src/app/game_app.cpp
    src/render/camera.cpp
    src/render/map_renderer.cpp
    src/render/entity_renderer.cpp
    src/render/hud.cpp
    src/render/text.cpp
    src/input/input_controller.cpp
    src/ui/command_panel.cpp
)
target_link_libraries(aoe_clone PRIVATE sim SFML::Graphics aoe_warnings)
target_compile_definitions(aoe_clone PRIVATE AOE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(MINGW)
    # Ejecutable autocontenido: sin DLLs de libstdc++/libgcc al lado
    target_link_options(aoe_clone PRIVATE -static)
endif()

# data/ junto al ejecutable
add_custom_command(TARGET aoe_clone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:aoe_clone>/data)

# ─── Tests ───────────────────────────────────────────────────────────────────
if(AOE_BUILD_TESTS)
    FetchContent_MakeAvailable(doctest)
    enable_testing()
    add_executable(sim_tests
        tests/test_main.cpp
        tests/test_core.cpp
        tests/test_sim.cpp
        tests/test_economy.cpp
        tests/test_combat.cpp
    )
    target_include_directories(sim_tests PRIVATE ${doctest_SOURCE_DIR}/doctest)
    target_link_libraries(sim_tests PRIVATE sim aoe_warnings)
    target_compile_definitions(sim_tests PRIVATE AOE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(MINGW)
        target_link_options(sim_tests PRIVATE -static)
    endif()
    add_test(NAME sim_tests COMMAND sim_tests)
endif()
