# Roadmap — orden de construcción

> El diseño de `architecture.md` y `mutability.md` es el destino. Este documento dice **en qué orden llegar**: cada fase termina en algo que se ejecuta y se puede probar. Nada de la fase N+1 empieza hasta que la N cumple su "Hecho cuando".

Principio: **vertical slice primero**. Un aldeano que corta madera de punta a punta (datos → simulación → render → input) vale más que el sistema de triggers completo sin nada en pantalla.

---

## Fase 0 — Esqueleto C++/SFML

- `CMakeLists.txt` + `CMakePresets.json` (Windows/Linux, debug/release) con SFML 3.1.0, nlohmann/json y doctest vía `FetchContent` (ver README).
- Targets `sim` (biblioteca, sin SFML Graphics/Window), `aoe_clone` (ejecutable) y `sim_tests`.
- Ventana SFML 3 con el bucle de tick fijo de `architecture.md` §9.1 (20 Hz de simulación, render libre).
- `core/`: `EntityHandle`, `SlotMap`, `Fixed`, `Rng` con tests.
- Mover el prototipo Python a una rama o borrarlo.

**Hecho cuando**: `cmake --preset ... && cmake --build ...` compila en Windows desde cero, abre una ventana y `ctest` pasa los tests de `SlotMap` (handle muerto → `get()` devuelve `nullptr`) y `Fixed`.

> **Estado (2026-11-14)**: hecha, salvo retirar el prototipo Python. Compilado con GCC 16.2 (WinLibs) + CMake 4.4 + Ninja; `ctest` en verde.

## Fase 1 — Mapa y cámara

- `TileMap` + generador simple (hierba, agua, bosque) con semilla.
- Render isométrico por chunks con `sf::VertexArray`, cámara con `sf::View` (bordes, teclado, zoom).
- Conversión pantalla ↔ tile y resaltado del tile bajo el ratón.

**Hecho cuando**: mapa de 128×128 a 60 FPS, el tile bajo el cursor es el correcto en todos los zooms.

> **Estado (2026-11-14)**: hecha con mapa de 96×96 (`--size 128` disponible). Generador con lagos, orillas, tierra alrededor de las bases, bosques y minas. Pendiente comprobar los 60 FPS a 128×128.

## Fase 2 — Entidades, datos y movimiento

- `DataRegistry` cargando `data/` (unidades, edificios, recursos, clases) con validación de errores legible.
- `SpawnManager` + `SpatialGrid`.
- Selección (clic, caja, shift), `Command` MOVE, `Pathfinder` A* con cola de peticiones por tick.
- Sprites desde atlas, orden por profundidad, interpolación entre ticks.

**Hecho cuando**: 200 unidades seleccionables se mueven por el mapa rodeando agua y árboles sin bajar de 60 FPS, y añadir una unidad nueva es solo crear un JSON.

> **Estado (2026-11-14)**: casi hecha. Entidades por JSON con figuras etiquetadas, selección (clic, caja, shift, doble clic), mover con A* + suavizado + formación, interpolación entre ticks. Pendiente: la prueba de 200 unidades y la cola de pathfinding con presupuesto por tick (hoy se calcula todo al recibir la orden). El movimiento es todavía una `MoveOrder` dentro de la entidad; pasa a `IActionBehavior` en la Fase 3.

## Fase 3 — Economía

- Recursos naturales (`resource_node`), `GATHER` con ciclo ir → recolectar → depositar.
- `ResourcePool`, HUD de recursos y población.
- Producción en edificios (cola, rally point), población y casas.
- Colocación de edificios (footprint, validación, obra en construcción, varios constructores).

**Hecho cuando**: partida en solitario donde se pueden crear aldeanos, recolectar los cuatro recursos, construir casas y un cuartel y entrenar una unidad militar.

## Fase 4 — Combate y modificadores

- Modelo de daño por clase de armadura (`architecture.md` §4.5), daño acumulado por tick, muertes diferidas.
- `ATTACK` cuerpo a cuerpo y a distancia (proyectiles como entidades), auto-ataque de unidades ociosas, torres.
- `ModifierResolver` + `PlayerState::apply_modifier`, tecnologías en edificios, subir de edad como tecnología.

**Hecho cuando**: dos ejércitos pelean con resultados que cuadran con una hoja de cálculo de daño, y una tecnología de ataque cambia el resultado de forma reproducible (test con semilla fija).

## Fase 5 — Niebla, IA y victoria

- `VisibilityMap` por jugador, niebla en render, minimapa.
- IA por guion que solo emite `Command`s (economía básica + ataque).
- Condición de victoria por conquista, pantalla de fin.
- **Replays**: grabar `GameConfig` + `Command`s y reproducir; test de hash de estado por tick.

**Hecho cuando**: partida completa contra la IA de principio a fin, y su replay reproduce exactamente el mismo resultado.

## Fase 6 — Mutabilidad

Todo `mutability.md`: `CapabilitySet` dinámico, `ActionConfig` como `std::variant`, `ObjectModifier`, conversión (monjes), garrison, morph, triggers. Los 4 casos de uso como tests de integración.

**Hecho cuando**: los 4 casos de `mutability.md` funcionan en partida y sus tests pasan sin tocar código del motor para añadir un quinto caso del mismo tipo (solo datos).

## Fase 7 — Opcional

- Guardar/cargar partida.
- Multijugador lockstep por red (posible gracias al determinismo de la Fase 0).
- Editor de mapas, más civilizaciones, sonido posicional, formaciones, flow fields.

---

## Riesgos conocidos

| Riesgo | Mitigación |
|---|---|
| Pathfinding de muchas unidades se come el frame | Cola con presupuesto por tick; un path por grupo + formación; flow fields en Fase 7 |
| Punto fijo incómodo de depurar | `Fixed` con `to_float()` solo para logs/render y tests de sus operaciones en Fase 0 |
| Arte: sin sprites no hay juego | Empezar con formas geométricas y un tileset libre (CC0); el render lee `sprite_set` del JSON, cambiar el arte no toca código |
| Sobrediseño (triggers/morph antes de tener juego) | Este roadmap: la mutabilidad es la Fase 6 |
