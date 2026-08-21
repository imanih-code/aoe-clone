# Arquitectura de Clases — AoE2 Clone (C++ / SFML)
> **v2** — Elimina `ObjId`, sistema de conversión dual-pointer, kill counter por unidad.
> Diagramas PlantUML renderizables en [PlantUML Online](https://www.plantuml.com/plantuml/uml/).

---

## 1. Visión General del Sistema

El juego se divide en **cinco capas ortogonales**. Cada capa solo conoce las capas inferiores a ella:

```
┌──────────────────────────────────────────┐
│  5. RENDER / UI  (SFML, HUD, selección)  │
├──────────────────────────────────────────┤
│  4. GAME LOOP    (GameState, input, dt)  │
├──────────────────────────────────────────┤
│  3. WORLD STATE  (QuadTree, pathfinding, │
│                   colisiones, spawns)    │
├──────────────────────────────────────────┤
│  2. PLAYER STATE (recursos, tecnologías, │
│                   stats efectivos, civis)│
├──────────────────────────────────────────┤
│  1. DATA LAYER   (plantillas estáticas,  │
│                   stats base, recetas)   │
└──────────────────────────────────────────┘
```

La separación fundamental:
- **WorldState** → sabe *dónde* están las cosas (posición, colisión, pathfinding). La búsqueda es **100% espacial**.
- **PlayerState** → sabe *qué tan poderosas* son las cosas (stats, modificadores, recursos). Vive **toda la partida**.
- **SpawnableEntity** → instancia viva que une ambos mundos. Su identidad **es su puntero**, no un ID numérico.

---

## 2. Por Qué ObjId No Tiene Sentido

En un RTS la búsqueda de entidades siempre parte de una posición o de un área:
- _"¿Qué enemigos tengo en rango de ataque?"_ → query espacial por radio
- _"¿Qué hay debajo del cursor?"_ → query espacial por punto
- _"¿Qué unidades de este jugador están vivas?"_ → iterar `PlayerState::units`

Un `ObjId` solo sería útil si necesitaras buscar **por nombre/número** independientemente del espacio, lo cual no ocurre en un RTS. Por eso:

- El `QuadTree` almacena `SpawnableEntity*` directamente.
- `PlayerState` usa `std::unordered_set<SpawnableEntity*>` para sus colecciones.
- La identidad de una entidad **es su dirección de memoria** durante su vida útil.

> **Nota sobre memoria**: Esto requiere que las entidades vivan en memoria estable (no en `std::vector` que se reasigna). Usa `std::list<SpawnableEntity>` o un pool allocator para las entidades. Los punteros seguirán siendo válidos mientras la entidad exista.

---

## 3. El Sistema de Conversión: Dual-Pointer

### 3.1 El Problema

Cuando un Monje convierte un Guerrero Águila azteca para el español, ¿qué stats tiene esa unidad?

**Decisión de diseño**: La unidad convertida **conserva los stats de su creador original**. Un Guerrero Águila azteca trabajando para el español sigue siendo tan fuerte como lo hizo su civi original. Esto tiene sentido lore-wise y evita romper el balance de unidades únicas.

### 3.2 La Solución: Dos Punteros

```cpp
// En SpawnableEntity:
PlayerState* original_player;   // El que creó la unidad. NUNCA cambia.
PlayerState* current_player;    // El dueño actual. Cambia en conversión.
```

| Situación | `original_player` | `current_player` | Stats vienen de |
|---|---|---|---|
| Unidad no convertida | `&player_A` | `&player_A` | `player_A` (son el mismo) |
| Unidad convertida | `&player_A` | `&player_B` | `player_A` (el original) |

La regla es simple: **los stats de una entidad siempre se resuelven contra `original_player`**. El `current_player` solo determina allegiance (quién la controla, a quién aporta población, quién recibe los recursos que recolecta).

### 3.3 Por Qué los PlayerStates Viven Toda la Partida

Si el jugador A es eliminado pero tenía unidades convertidas por el jugador B, esas unidades siguen necesitando el `original_player` de A para resolver sus stats. Por eso los `PlayerState` se destruyen solo al cerrar la partida, no cuando el jugador pierde.

```cpp
// Esto siempre es seguro durante la partida:
float atk = entity->get_stat(ATTACK_DMG);
// Internamente: usa entity->original_player->get_effective_stats(entity->obj_tag)
// Aunque player A esté "derrotado", su PlayerState sigue en memoria con is_defeated=true
```

### 3.4 El Kill Counter

Cada `PlayerState` lleva un registro de cuántas unidades de cada tipo han matado **sus** unidades. Esto sirve para estadísticas, achievements y potencialmente para triggers de misión.

```cpp
// En PlayerState:
std::unordered_map<ObjTag, uint32_t> kills_by_unit_type;
// Ej: kills_by_unit_type[EAGLE_WARRIOR] = 12
// → "mis Guerreros Águila mataron 12 unidades"
```

Si quieres también llevar cuántas **bajas propias** tuvo cada tipo, agregas:
```cpp
std::unordered_map<ObjTag, uint32_t> losses_by_unit_type;
```

---

## 4. El Sistema de Modificadores

### 4.1 Por Qué NO mutar `EntityStats` directamente

Mutar el stat base hace imposible deshacerlo (necesario para hechizos temporales, conversión, etc.) y pierdes la procedencia del cambio.

### 4.2 Base + Lista de Modificadores → Stat Efectivo

```
stat_efectivo = f(stat_base, [mod1, mod2, mod3, ...])
```

El `ModifierResolver` es una **función pura**: recibe el valor base + lista de mods → devuelve el valor final. Nunca muta nada.

### 4.3 Orden de Aplicación (CRÍTICO)

```
1. base + Σ(ADD_FLAT)
2. paso_1 × (1 + Σ(ADD_PERCENT) / 100)
3. paso_2 × Π(MULTIPLY_FLAT)
4. Si existe SET_VALUE → reemplaza todo
```

Este orden garantiza que no importa el orden en que se investiguen las tecnologías, el resultado es el mismo.

### 4.4 Lookup de Stats en una Entidad Convertida

```
entity->get_stat(ATTACK_DMG)
    │
    └─→ usa entity->original_player->get_effective_stats(entity->obj_tag)
              │
              └─→ ModifierResolver::resolve(base_stat, original_player->active_modifiers, obj_tag)
```

Los mods de instancia (hechizos temporales sobre esta unidad específica) se aplican encima:

```
stat_final = resolve(resolve(base, player_mods), instance_mods)
```

---

## 5. Diagramas PlantUML

### Diagrama 1: Enumeraciones y Tags (Data Layer)

```plantuml
@startuml DataLayer
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Tags & Enumeraciones" {

  enum ObjTag {
    EMPTY = 0
    VILLAGER
    MILITIA
    MAN_AT_ARMS
    LONG_SWORDSMAN
    SPEARMAN
    ARCHER
    CROSSBOWMAN
    KNIGHT
    EAGLE_WARRIOR
    FISHING_SHIP
    TRANSPORT_SHIP
    GALLEY
    TOWN_CENTER
    BARRACKS
    ARCHERY_RANGE
    STABLE
    BLACKSMITH
    MONASTERY
    CASTLE
    GOLD_MINE
    STONE_MINE
    TREE
    BERRY_BUSH
    DEER
    RELIC
  }

  enum ClassTag {
    BUILDING
    MILITARY_UNIT
    VILLAGER_UNIT
    NATURAL_WONDER
    RESOURCE_FONT
  }

  enum StatTag {
    MAX_HP
    ATTACK_DMG
    ATTACK_RATE
    ARMOR_MELEE
    ARMOR_PIERCE
    MOVE_SPEED
    SIGHT_RANGE
    CREATION_TIME
    CARRY_CAPACITY
    GATHER_RATE
    HEAL_RATE
    CONVERSION_RATE
    POPULATION_VALUE
  }

  enum ResourceTag {
    GOLD
    STONE
    WOOD
    FOOD
  }

  enum ArmorTag {
    MELEE
    PIERCE
    BUILDING
    CAVALRY
    SIEGE
    SHIP
  }

  enum AttackTag {
    MELEE
    PIERCE
    SIEGE
    MAGIC
  }

  enum AgeTag {
    DARK_AGE = 0
    FEUDAL_AGE = 1
    CASTLE_AGE = 2
    IMPERIAL_AGE = 3
  }

  enum ModifierOp {
    ADD_FLAT
    ADD_PERCENT
    MULTIPLY_FLAT
    SET_VALUE
    GRANT_TAG
    REMOVE_TAG
  }

  enum ModifierSource {
    CIVILIZATION_BONUS
    TECHNOLOGY
    AGE_UP
    PASSIVE_SKILL
    ACTIVE_SKILL
    RELIC
    GARRISONED
    WONDER
  }

  enum ModifierScope {
    PLAYER_WIDE
    UNIT_INSTANCE
    BUILDING_INSTANCE
    TEAM_WIDE
  }
}
@enduml
```

---

### Diagrama 2: Sistema de Modificadores

```plantuml
@startuml ModifierSystem
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Modifier System" {

  struct AttackBonus {
    + AttackTag attack_type
    + ArmorTag  target_armor_class
    + float     bonus_dmg
  }

  struct StatModifier {
    + StatTag    stat_tag
    + ModifierOp operation
    + float      value
  }

  note right of StatModifier
    { MAX_HP,      ADD_PERCENT,   15.0f } → +15% vida
    { ATTACK_DMG,  ADD_FLAT,       2.0f } → +2 daño plano
    { MOVE_SPEED,  MULTIPLY_FLAT, 0.85f } → ×0.85 velocidad
    { ARMOR_MELEE, SET_VALUE,      5.0f } → fija armor en 5
  end note

  struct ObjectModifier {
    + ObjTag       grants_unit_tag
    + bool         removes_tag
    + std::string  grants_skill_id
  }

  class Modifier {
    + std::string        modifier_id
    + ModifierSource     source
    + ModifierScope      scope
    + ObjTag             target_obj_tag
    + ClassTag           target_class_tag
    + bool               is_permanent
    + float              duration_seconds
    + float              elapsed_time
    + std::vector<StatModifier>   stat_mods
    + std::vector<AttackBonus>    attack_bonuses
    + std::vector<ObjectModifier> object_mods
  }

  note right of Modifier
    Un Modifier agrupa todo lo que hace
    una tecnología o bonus de civi.
    "Forja" (Blacksmith):
      target_class_tag = MILITARY_UNIT
      stat_mods = [
        { ATTACK_DMG, ADD_FLAT, 1.0f },
        { ARMOR_MELEE, ADD_FLAT, 1.0f }
      ]
  end note

  class ModifierResolver {
    + {static} float resolve_stat(\n    StatTag stat,\n    float base_value,\n    const std::vector<Modifier>& mods,\n    ObjTag unit_tag,\n    ClassTag class_tag)
    + {static} std::vector<AttackBonus> resolve_attack_bonuses(\n    const std::vector<Modifier>& mods,\n    ObjTag unit_tag)
  }

  note bottom of ModifierResolver
    1. Filtra mods que aplican a (unit_tag, class_tag)
    2. base += Σ ADD_FLAT
    3. resultado *= (1 + Σ ADD_PERCENT / 100)
    4. resultado *= Π MULTIPLY_FLAT
    5. Si existe SET_VALUE → reemplaza
  end note

  Modifier "1" *-- "many" StatModifier
  Modifier "1" *-- "many" AttackBonus
  Modifier "1" *-- "many" ObjectModifier
  ModifierResolver ..> Modifier : usa
}
@enduml
```

---

### Diagrama 3: Data Layer — Plantillas Estáticas

```plantuml
@startuml DataTemplates
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Data Layer (Read-Only)" {

  class EntityStats {
    + ClassTag  class_tag
    + float     max_hp
    + float     creation_time_sec
    + AttackTag attack_type
    + float     attack_dmg_base
    + float     attack_rate_base
    + std::vector<AttackBonus> base_attack_bonuses
    + ArmorTag  armor_type
    + float     armor_melee_base
    + float     armor_pierce_base
    + float     move_speed_base
    + uint32_t  sight_range_quads
    + uint32_t  max_sheltered_units
    + float     size_factor
    + float     collision_w
    + float     collision_h
    + bool      is_convertible
    + bool      has_cooldown_bar
    + float     gather_rate_base
    + float     carry_capacity_base
    + uint32_t  population_cost
  }

  class ProductionRecipe {
    + ObjTag   produces_tag
    + uint32_t cost_gold
    + uint32_t cost_stone
    + uint32_t cost_wood
    + uint32_t cost_food
    + AgeTag   min_age_required
    + std::vector<ObjTag>        requires_buildings
    + std::vector<std::string>   requires_techs
  }

  class TechTemplate {
    + std::string  tech_id
    + std::string  name
    + AgeTag       min_age_required
    + ObjTag       researched_at_building
    + uint32_t     cost_gold
    + uint32_t     cost_food
    + float        research_time_sec
    + std::vector<std::string>  requires_techs
    + std::vector<Modifier>     grants_modifiers
  }

  class SkillTemplate {
    + std::string  skill_id
    + bool         is_passive
    + bool         is_active
    + float        cooldown_sec
    + float        duration_sec
    + float        area_of_effect
    + std::vector<Modifier>  effect_modifiers
    + ProductionRecipe       spawn_recipe
  }

  class ObjSheet {
    + ObjTag           tag
    + EntityStats      base_stats
    + ProductionRecipe recipe
    + std::vector<std::string>  available_skills
    + std::vector<std::string>  available_techs
    + std::vector<ObjTag>       can_produce_tags
  }

  class CivilizationTemplate {
    + std::string  civ_id
    + std::string  name
    + AgeTag       starting_age
    ' punteros → no copias (datos compartidos)
    + std::unordered_map<ObjTag, ObjSheet*>  base_unit_sheets
    + std::unordered_map<ObjTag, ObjSheet*>  base_building_sheets
    ' copias propias (únicos de esta civi)
    + std::unordered_map<ObjTag, ObjSheet>   unique_unit_sheets
    + std::unordered_map<ObjTag, ObjSheet>   unique_building_sheets
    + std::unordered_map<std::string, TechTemplate*> available_techs
    + std::unordered_map<std::string, TechTemplate>  unique_techs
    ' Bonuses pasivos de la civi (innatos)
    + std::vector<Modifier>  civ_modifiers
    + Modifier               team_bonus
  }

  class DataRegistry {
    - {static} DataRegistry* instance
    + std::unordered_map<ObjTag, ObjSheet>          all_obj_sheets
    + std::unordered_map<std::string, TechTemplate>  all_techs
    + std::unordered_map<std::string, SkillTemplate> all_skills
    + std::unordered_map<std::string, CivilizationTemplate> all_civs
    + {static} DataRegistry& get()
    + void load_from_files(const std::string& data_dir)
  }

  ObjSheet *-- EntityStats
  ObjSheet *-- ProductionRecipe
  CivilizationTemplate "1" o-- "many" ObjSheet
  CivilizationTemplate "1" *-- "many" TechTemplate
  CivilizationTemplate "1" *-- "many" Modifier
  DataRegistry "1" *-- "many" ObjSheet
  DataRegistry "1" *-- "many" TechTemplate
  DataRegistry "1" *-- "many" SkillTemplate
  DataRegistry "1" *-- "many" CivilizationTemplate
}
@enduml
```

---

### Diagrama 4: Player State

```plantuml
@startuml PlayerState
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Player Layer" {

  class ResourcePool {
    + uint32_t food
    + uint32_t wood
    + uint32_t gold
    + uint32_t stone
    + int32_t  population_current
    + int32_t  population_cap
    + bool can_afford(const ProductionRecipe& r) const
    + void deduct(const ProductionRecipe& r)
    + void add(ResourceTag r, uint32_t amount)
  }

  struct CachedEffectiveStats {
    + ObjTag       obj_tag
    + EntityStats  effective_stats
    + std::vector<AttackBonus> effective_bonuses
    + bool         is_dirty
  }

  class PlayerState {
    + int     player_id
    + bool    is_defeated
    ' is_defeated=true pero el objeto NO se destruye hasta fin de partida
    ' (unidades convertidas siguen referenciándolo como original_player)
    + bool    is_ally_with[8]
    + AgeTag  current_age
    + ResourcePool  resources
    + const CivilizationTemplate* civ_template
    ' ─── Modificadores activos ────────────────────────────
    + std::vector<Modifier> active_modifiers
    ' ─── Cache de stats efectivos por ObjTag ──────────────
    + std::unordered_map<ObjTag, CachedEffectiveStats> effective_stats_cache
    ' ─── Tecnologías investigadas ─────────────────────────
    + std::unordered_set<std::string> researched_techs
    ' ─── Entidades VIVAS controladas por este jugador ─────
    ' (sin ObjId: las entidades se identifican por puntero)
    + std::unordered_set<SpawnableEntity*> units
    + std::unordered_set<SpawnableEntity*> buildings
    + std::unordered_set<SpawnableEntity*> idle_villagers
    ' ─── Kill counter ─────────────────────────────────────
    + std::unordered_map<ObjTag, uint32_t> kills_by_unit_type
    + std::unordered_map<ObjTag, uint32_t> losses_by_unit_type
    ' ─── Métodos ──────────────────────────────────────────
    + void apply_modifier(const Modifier& mod)
    + void remove_modifier(const std::string& modifier_id)
    + void invalidate_cache(ObjTag tag)
    + void invalidate_all_cache()
    + const EntityStats& get_effective_stats(ObjTag tag) const
    + bool can_research(const std::string& tech_id) const
    + void research_tech(const std::string& tech_id)
    + bool can_produce(const ProductionRecipe& recipe) const
    + void record_kill(ObjTag killed_tag, ObjTag killer_tag)
    + void record_loss(ObjTag lost_tag)
  }

  PlayerState "1" *-- "1" ResourcePool
  PlayerState "1" *-- "many" CachedEffectiveStats
  PlayerState "1" *-- "many" Modifier
  PlayerState ..> CivilizationTemplate : observa (read-only)
  PlayerState ..> ModifierResolver : usa
}
@enduml
```

---

### Diagrama 5: Entidades en el Mundo

```plantuml
@startuml Entities
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Entity Layer" {

  struct ProductionJob {
    + ObjTag  tag_to_produce
    + double  time_remaining_sec
    + int     requesting_player_id
  }

  struct ActiveSkillState {
    + std::string  skill_id
    + float        cooldown_remaining
    + bool         is_active
    + float        duration_remaining
  }

  struct InstanceModifierStack {
    + std::vector<Modifier>  modifiers
    + bool                   is_dirty
  }

  class SpawnableEntity {
    + ObjTag    obj_tag
    + ClassTag  class_tag
    + float     hp_current
    ' ─── Punteros de dueño ────────────────────────────────
    + PlayerState* original_player
    ' Nunca cambia. Stats siempre se calculan contra este.
    + PlayerState* current_player
    ' Cambia en conversión. Determina allegiance y control.
    ' Si original_player == current_player → no fue convertida.
    ' Si difieren → fue convertida: stats = original_player.
    ' ─── Referencia a stats base (de la plantilla) ────────
    + const EntityStats* base_stats_ref
    ' ─── Modificadores solo de esta instancia ─────────────
    ' (hechizos temporales, bonuses de garrisón, etc.)
    + InstanceModifierStack  instance_modifiers
    ' ─── Habilidades ──────────────────────────────────────
    + std::vector<ActiveSkillState>     skill_states
    ' ─── Garrisón ─────────────────────────────────────────
    + std::vector<SpawnableEntity*>     sheltered_units
    ' ─── Cola de producción (para edificios) ──────────────
    + std::queue<ProductionJob>         production_queue
    ' ─── Cálculo de stats ─────────────────────────────────
    + float get_stat(StatTag stat) const
    + std::vector<AttackBonus> get_attack_bonuses() const
    ' ─── Acciones básicas ─────────────────────────────────
    + void take_damage(float raw_dmg, AttackTag atk_type)
    + void heal(float amount)
    + void convert_to(PlayerState* new_owner)
    + void garrison(SpawnableEntity* unit)
    + void eject_all()
    + bool is_converted() const
  }

  note right of SpawnableEntity
    is_converted():
      return original_player != current_player;

    get_stat(StatTag stat):
      1. base  ← base_stats_ref->get(stat)
      2. paso2 ← ModifierResolver::resolve_stat(
                   stat, base,
                   original_player->active_modifiers,
                   obj_tag, class_tag)
      3. return ModifierResolver::resolve_stat(
                   stat, paso2,
                   instance_modifiers.modifiers,
                   obj_tag, class_tag)

    convert_to(PlayerState* new_owner):
      current_player->units.erase(this)
      current_player = new_owner
      new_owner->units.insert(this)
      instance_modifiers.clear_temporaries()
      (original_player NO cambia)
  end note

  class ResourceFont {
    + ResourceTag  resource_type
    + uint32_t     amount_remaining
    + uint32_t     amount_max
    + bool         is_depleted() const
    + uint32_t     gather(float gather_rate, float dt)
  }

  SpawnableEntity "0..*" *-- "0..*" SpawnableEntity : sheltered
  SpawnableEntity "1"    *-- "1"    InstanceModifierStack
  SpawnableEntity "1"    *-- "many" ActiveSkillState
  SpawnableEntity "1"    *-- "many" ProductionJob
  SpawnableEntity ..>    PlayerState : original_player (stats)
  SpawnableEntity ..>    PlayerState : current_player (control)
  ResourceFont    --|>   SpawnableEntity
}
@enduml
```

---

### Diagrama 6: World State (QuadTree — búsqueda espacial pura)

```plantuml
@startuml WorldState
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "World Layer" {

  note as N1
    Sin ObjId. Las entidades se identifican
    por puntero. El QuadTree almacena
    SpawnableEntity* directamente.
    Las entidades viven en un std::list<SpawnableEntity>
    o pool allocator para garantizar
    estabilidad de punteros.
  end note

  struct WorldRect {
    + float x, y, w, h
    + bool contains(float px, float py) const
    + bool intersects(const WorldRect& o) const
  }

  class QuadTreeNode {
    - WorldRect              bounds
    - int                    depth
    - int                    max_depth
    - std::vector<SpawnableEntity*>  entities
    - QuadTreeNode*          children[4]
    + void insert(SpawnableEntity* e, const WorldRect& rect)
    + void remove(SpawnableEntity* e)
    + void update(SpawnableEntity* e,\n              const WorldRect& old_r,\n              const WorldRect& new_r)
    + std::vector<SpawnableEntity*> query_range(const WorldRect& r)
    + std::vector<SpawnableEntity*> query_radius(float cx, float cy, float r)
  }

  class SpatialIndex {
    - QuadTreeNode                             root
    - std::unordered_map<SpawnableEntity*,\n  WorldRect> entity_rects
    + void insert(SpawnableEntity* e, float x, float y)
    + void remove(SpawnableEntity* e)
    + void move(SpawnableEntity* e, float new_x, float new_y)
    + sf::Vector2f get_position(SpawnableEntity* e) const
    + std::vector<SpawnableEntity*> get_in_range(\n    float cx, float cy, float radius)
    + std::vector<SpawnableEntity*> get_enemies_in_range(\n    SpawnableEntity* observer, float radius)
    + std::vector<SpawnableEntity*> get_allies_in_range(\n    SpawnableEntity* observer, float radius)
    + bool check_collision(SpawnableEntity* a, SpawnableEntity* b)
  }

  class PathfindingGrid {
    - std::vector<std::vector<bool>> walkable
    - int grid_width
    - int grid_height
    + std::vector<sf::Vector2i> find_path(\n    sf::Vector2i start,\n    sf::Vector2i end,\n    ClassTag unit_class)
    + void set_walkable(int x, int y, bool val)
    + bool is_walkable(int x, int y) const
  }

  class SpawnManager {
    - std::list<SpawnableEntity> entity_pool
    ' std::list garantiza estabilidad de punteros
    + SpawnableEntity* spawn(\n    ObjTag tag,\n    PlayerState* owner,\n    float x, float y)
    + void despawn(SpawnableEntity* e, SpatialIndex& spatial)
  }

  class WorldState {
    + SpatialIndex    spatial
    + PathfindingGrid pathfinding
    + SpawnManager    spawn_mgr
    ' ─── Queries ─────────────────────────────────────────
    + sf::Vector2f get_position(SpawnableEntity* e) const
    + std::vector<SpawnableEntity*> get_attackable_in_range(\n    SpawnableEntity* attacker, float range)
    + std::vector<SpawnableEntity*> get_healable_in_range(\n    SpawnableEntity* healer, float range)
    ' ─── Tick ────────────────────────────────────────────
    + void process_attack(\n    SpawnableEntity* attacker,\n    SpawnableEntity* target)
    + void tick(float dt, std::vector<PlayerState>& players)
    ' process_attack registra los kills en PlayerState:
    ' attacker->original_player->record_kill(target->obj_tag,
    '                                        attacker->obj_tag)
    ' target->current_player->record_loss(target->obj_tag)
  }

  WorldState "1" *-- "1" SpatialIndex
  WorldState "1" *-- "1" PathfindingGrid
  WorldState "1" *-- "1" SpawnManager
  SpatialIndex "1" *-- "1" QuadTreeNode
  SpawnManager ..> SpawnableEntity : crea/destruye
  WorldState ..> SpawnableEntity
  WorldState ..> PlayerState : registra kills
}
@enduml
```

---

### Diagrama 7: GameState — Orquestador

```plantuml
@startuml GameState
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Game Layer" {

  class GameConfig {
    + int         num_players
    + std::string map_name
    + AgeTag      starting_age
    + uint32_t    starting_food
    + uint32_t    starting_wood
    + uint32_t    starting_gold
    + uint32_t    starting_stone
    + bool        team_mode
    + int         victory_condition
  }

  class GameState {
    + float    game_time_elapsed
    + bool     is_paused
    + GameConfig config
    + WorldState world
    ' PlayerStates viven TODA la partida (is_defeated no los destruye)
    + std::vector<PlayerState> players
    + DataRegistry* data
    ' ─── Lógica de alto nivel ────────────────────────────
    + void initialize(const GameConfig& cfg)
    + void tick(float dt)
    + void on_technology_researched(\n    PlayerState* ps,\n    const std::string& tech_id)
    + void on_age_up(\n    PlayerState* ps,\n    AgeTag new_age)
    + void on_unit_converted(\n    SpawnableEntity* entity,\n    PlayerState* new_owner)
    + void check_victory_conditions()
    + bool is_game_over() const
  }

  note right of GameState
    on_unit_converted:
      old_owner = entity->current_player
      entity->convert_to(new_owner)
      ' SpawnableEntity actualiza sus punteros
      ' old_owner->units ya no contiene entity
      ' new_owner->units ahora contiene entity
      ' original_player no cambia → stats intactos
  end note

  GameState "1" *-- "1" WorldState
  GameState "1" *-- "1..8" PlayerState
  GameState "1" *-- "1" GameConfig
  GameState ..> DataRegistry : singleton
}
@enduml
```

---

### Diagrama 8: Sistema de Habilidades (Strategy Pattern)

```plantuml
@startuml SkillSystem
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Skill System" {

  abstract class ISkillBehavior {
    + {abstract} void on_activate(\n    SpawnableEntity* caster,\n    WorldState& world)
    + {abstract} void on_tick(\n    SpawnableEntity* caster,\n    WorldState& world,\n    float dt)
    + {abstract} void on_deactivate(\n    SpawnableEntity* caster)
    + {abstract} bool is_done() const
  }

  class HealAuraBehavior {
    - float heal_per_sec
    - float radius
    - float duration_remaining
    + void on_activate(...)
    + void on_tick(...)
    + void on_deactivate(...)
    + bool is_done() const
  }

  class SpawnUnitBehavior {
    - ObjTag  unit_to_spawn
    - int     count
    - bool    done
    + void on_activate(...)
    + void on_tick(...)
    + void on_deactivate(...)
    + bool is_done() const
  }

  class StatBoostBehavior {
    ' Aplica un Modifier temporal a la instancia
    - Modifier temp_modifier
    - float   duration
    - float   elapsed
    + void on_activate(...)
    + void on_tick(...)
    + void on_deactivate(...)
    + bool is_done() const
  }

  class ConvertBehavior {
    ' El monje convierte usando original_player del target
    - float  conversion_time
    - float  elapsed
    - SpawnableEntity* target
    + void on_activate(...)
    + void on_tick(...)
    + void on_deactivate(...)
    + bool is_done() const
  }

  note right of ConvertBehavior
    on_deactivate:
      Si conversión completada:
        game_state.on_unit_converted(
          target,
          caster->current_player)
      El target conserva sus stats
      (original_player no cambia)
  end note

  class ProjectileBehavior {
    - float         speed
    - AttackTag     attack_type
    - float         damage
    - sf::Vector2f  target_pos
    + void on_activate(...)
    + void on_tick(...)
    + void on_deactivate(...)
    + bool is_done() const
  }

  class SkillFactory {
    + {static} std::unique_ptr<ISkillBehavior> create(\n    const SkillTemplate& tmpl)
  }

  ISkillBehavior <|-- HealAuraBehavior
  ISkillBehavior <|-- SpawnUnitBehavior
  ISkillBehavior <|-- StatBoostBehavior
  ISkillBehavior <|-- ConvertBehavior
  ISkillBehavior <|-- ProjectileBehavior
  SkillFactory ..> ISkillBehavior : crea
  SkillFactory ..> SkillTemplate  : lee
}
@enduml
```

---

### Diagrama 9: Vista Completa de Capas

```plantuml
@startuml FullOverview
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "DATA LAYER" #0f2040 {
  class DataRegistry
  class CivilizationTemplate
  class ObjSheet
  class TechTemplate
  class SkillTemplate
  class EntityStats
}

package "MODIFIER SYSTEM" #1a0f40 {
  class Modifier
  struct StatModifier
  struct AttackBonus
  class ModifierResolver
}

package "PLAYER LAYER" #0f400f {
  class PlayerState
  class ResourcePool
}

package "ENTITY LAYER" #401a0f {
  class SpawnableEntity
  class ResourceFont
  class InstanceModifierStack
}

package "WORLD LAYER (spatial)" #400f2a {
  class WorldState
  class SpatialIndex
  class PathfindingGrid
  class SpawnManager
}

package "GAME LAYER" #2a2a00 {
  class GameState
}

package "SKILL BEHAVIORS" #1a3040 {
  interface ISkillBehavior
}

DataRegistry "1" *-- "many" CivilizationTemplate
DataRegistry "1" *-- "many" ObjSheet
DataRegistry "1" *-- "many" TechTemplate
DataRegistry "1" *-- "many" SkillTemplate
ObjSheet "1" *-- "1" EntityStats

CivilizationTemplate ..> Modifier : define civ_modifiers
TechTemplate         ..> Modifier : grants_modifiers

PlayerState "1" o-- "many" Modifier : active_modifiers
PlayerState "1" *-- "1" ResourcePool
PlayerState ..> CivilizationTemplate : observa
PlayerState ..> ModifierResolver : usa

SpawnableEntity ..> PlayerState : original_player (stats)
SpawnableEntity ..> PlayerState : current_player  (control)
SpawnableEntity "1" *-- "1" InstanceModifierStack
SpawnableEntity ..> ModifierResolver : usa
SpawnableEntity ..> ISkillBehavior   : ejecuta

WorldState "1" *-- "1" SpatialIndex
WorldState "1" *-- "1" PathfindingGrid
WorldState "1" *-- "1" SpawnManager
SpawnManager ..> SpawnableEntity : crea (std::list)
WorldState ..> PlayerState : registra kills

GameState "1" *-- "1" WorldState
GameState "1" *-- "1..8" PlayerState
GameState ..> DataRegistry : singleton

Modifier "1" *-- "many" StatModifier
Modifier "1" *-- "many" AttackBonus
ModifierResolver ..> Modifier

ResourceFont --|> SpawnableEntity
@enduml
```

---

## 6. Flujos Clave

### 6.1 Conversión de una Unidad (Monje)

```
ConvertBehavior::on_deactivate(monje_azteca)
    │
    ├── target = guerrero_aguila_azteca (original_player = &player_azteca)
    ├── Llama GameState::on_unit_converted(target, monje->current_player)
    │       ├── old_owner = target->current_player  (= &player_azteca)
    │       ├── target->convert_to(&player_espanol)
    │       │       ├── current_player->units.erase(target)    // player_azteca lo suelta
    │       │       ├── current_player = &player_espanol
    │       │       ├── current_player->units.insert(target)   // player_espanol lo toma
    │       │       └── limpia instance_modifiers temporales
    │       └── original_player sigue siendo &player_azteca
    │
    └── Resultado: el guerrero pertenece al español
        PERO sus stats siguen calculándose con los mods del azteca
        → Si el azteca tenía "Garland Wars" (+4 ataque), el guerrero convertido
          aún tiene ese bonus aunque el español no haya investigado esa tech.
```

### 6.2 Muerte de una Unidad y Kill Counter

```
WorldState::process_attack(espada_espanol, arquero_mongol)
    │
    ├── raw_dmg = espada->get_stat(ATTACK_DMG)  // usa original_player del español
    ├── armor   = arquero->get_stat(ARMOR_MELEE) // usa original_player del mongol
    ├── net_dmg = raw_dmg - armor
    ├── arquero->take_damage(net_dmg, MELEE)
    │
    └── Si arquero->hp_current <= 0:
            // Kill se atribuye al original_player del atacante
            espada->original_player->record_kill(ARCHER, LONG_SWORDSMAN)
            // Baja se atribuye al current_player de la víctima
            arquero->current_player->record_loss(ARCHER)
            world.despawn(arquero)
```

> **Nota**: Si el `espada_espanol` era originalmente azteca (convertido), el kill se registra en el `PlayerState` azteca original. Decisión a tomar: ¿el kill debe ir al `current_player` (quien controla) o al `original_player` (quien "merece" el crédito)?

### 6.3 Investigar una Tecnología

```
GameState::on_technology_researched(&player_espanol, "tech_forja")
    │
    ├── player_espanol.can_research("tech_forja") → OK
    ├── player_espanol.resources.deduct(tech_forja.recipe)
    ├── Crea Modifier desde TechTemplate::grants_modifiers
    ├── player_espanol.apply_modifier(forja_modifier)
    │       ├── active_modifiers.push_back(forja_modifier)
    │       └── invalidate_cache(MILITIA), invalidate_cache(MAN_AT_ARMS), ...
    └── player_espanol.researched_techs.insert("tech_forja")

Próximo frame: milicia_espanola->get_stat(ATTACK_DMG)
    → resolve(base=4, player_espanol.active_modifiers)
    → 4 + 1 (Forja ADD_FLAT) = 5.0f
    (La milicia azteca convertida NO se beneficia: usa player_azteca.active_modifiers)
```

### 6.4 Subir de Edad

```
GameState::on_age_up(&player_espanol, FEUDAL_AGE)
    │
    ├── Crea Modifier AGE_UP con scope=PLAYER_WIDE
    ├── player_espanol.apply_modifier(feudal_up_modifier)
    ├── Invalida todo el cache del jugador
    └── Desbloquea nuevas entradas en production_queue de edificios
```

---

## 7. Estructura de Archivos Sugerida

```
src/
├── enums/
│   ├── tags.hpp          ← ObjTag, ClassTag, StatTag
│   └── enums.hpp         ← ResourceTag, ArmorTag, AttackTag, AgeTag,
│                            ModifierOp, ModifierSource, ModifierScope
│
├── data/
│   ├── data_registry.hpp/.cpp
│   ├── entity_stats.hpp
│   ├── obj_sheet.hpp
│   ├── tech_template.hpp
│   ├── skill_template.hpp
│   └── civ_template.hpp
│
├── modifiers/
│   ├── modifier.hpp         ← struct Modifier, StatModifier, AttackBonus, ObjectModifier
│   └── modifier_resolver.hpp/.cpp
│
├── player/
│   ├── player_state.hpp/.cpp
│   └── resource_pool.hpp
│
├── entities/
│   ├── spawnable_entity.hpp/.cpp
│   └── resource_font.hpp/.cpp
│
├── skills/
│   ├── i_skill_behavior.hpp
│   ├── skill_factory.hpp/.cpp
│   ├── heal_aura_behavior.hpp/.cpp
│   ├── stat_boost_behavior.hpp/.cpp
│   ├── spawn_unit_behavior.hpp/.cpp
│   ├── convert_behavior.hpp/.cpp
│   └── projectile_behavior.hpp/.cpp
│
├── world/
│   ├── world_state.hpp/.cpp
│   ├── spatial_index.hpp/.cpp    ← QuadTree, búsqueda espacial
│   ├── pathfinding_grid.hpp/.cpp ← A*
│   └── spawn_manager.hpp/.cpp    ← std::list<SpawnableEntity>
│
└── game/
    ├── game_state.hpp/.cpp
    └── game_config.hpp
```

---

## 8. Preguntas Abiertas

1. **Kill counter y conversión**: ¿El kill de una unidad convertida se atribuye a `original_player` (quien la entrenó) o a `current_player` (quien la controla en ese momento)?
2. **Proyectiles**: ¿Son entidades en el `SpawnManager` (tienen puntero, posición en el QuadTree) o efectos puramente visuales sin existencia en el world state?
3. **Formato de datos**: ¿JSON, XML, binario propio, o hardcodeado en C++ como `constexpr`?
4. **Niebla de guerra**: ¿Se implementa en el `WorldState` (filtra queries) o solo en el Render Layer?
5. **Multijugador**: ¿Local o red? Si es red: el `GameState` debe ser 100% determinístico (sin `float` random, sin `std::unordered_map` con orden no determinístico).
