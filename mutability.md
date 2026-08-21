# Sistema de Mutabilidad de Unidades — PlantUML
> Diagrama complementario a `architecture.md`. Solo cubre el sistema de Capabilities + Actions + Triggers.
> La idea central: `ClassTag` describe lo que una unidad **ES**. `CapabilityTag` describe lo que una unidad **PUEDE HACER**. Son ortogonales y los segundos son totalmente dinámicos.

---

## Los 4 Casos de Uso como Guía de Diseño

| # | Caso | Mecanismo |
|---|---|---|
| 1 | Unidad no-aldeano que construye torres | Modifier otorga `CAP_BUILD` con `BuildActionConfig { WATCH_TOWER }` |
| 2 | Caballeros que convierten infantería enemiga | Tech otorga `CAP_CONVERT` con `ConvertActionConfig { target: INFANTRY }` |
| 3 | Unidad del castillo se sacrifica → edificio | Action `CAP_MORPH` con `MorphActionConfig { consume: true, form: TOWER }` |
| 4 | Caballero muere → jinete se levanta como infantería | Trigger `ON_DEATH` con `SpawnTriggerEffect { spawn: MAN_AT_ARMS }` |

---

## Diagrama 1: CapabilityTag — Lo que una unidad puede hacer

```plantuml
@startuml Capabilities
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea
skinparam enumBackgroundColor #0f3460

enum CapabilityTag {
  ' ── Movimiento ──────────────────────────────────────────
  CAP_MOVE
  ' Mover por tierra. Las torres no lo tienen.
  CAP_MOVE_NAVAL
  ' Mover por agua.

  ' ── Combate ──────────────────────────────────────────────
  CAP_ATTACK_MELEE
  CAP_ATTACK_RANGED
  CAP_ATTACK_SIEGE
  ' Una unidad puede tener varios tipos de ataque.

  ' ── Recolección ──────────────────────────────────────────
  CAP_GATHER_FOOD
  CAP_GATHER_WOOD
  CAP_GATHER_GOLD
  CAP_GATHER_STONE
  CAP_FISH

  ' ── Construcción / Reparación ────────────────────────────
  CAP_BUILD
  ' Qué puede construir lo define BuildActionConfig.
  CAP_REPAIR

  ' ── Conversión / Curación ────────────────────────────────
  CAP_CONVERT
  ' Qué puede convertir lo define ConvertActionConfig.
  CAP_HEAL_UNITS
  ' Solo monjes/sanadores. Qué puede sanar: HealActionConfig.

  ' ── Transformación ───────────────────────────────────────
  CAP_MORPH
  ' En qué se transforma: MorphActionConfig.
  ' Puede consumir la unidad original (sacrificio).

  ' ── Interacción con el mundo ─────────────────────────────
  CAP_PICK_RELIC
  CAP_TRADE
  CAP_BOARD_SHIP
  ' Puede subirse a un barco de transporte.

  ' ── Garrisón ─────────────────────────────────────────────
  CAP_GARRISON_ENTER
  ' Puede entrar en una estructura.
  CAP_GARRISON_ACCEPT
  ' Puede recibir unidades dentro (edificios/barcos).

  ' ── Especiales ───────────────────────────────────────────
  CAP_DETONATE
  ' Petardo: se destruye causando daño en área.
  CAP_PACK_UNPACK
  ' Trebuchet: alterna entre modo pack y deploy.
}

note top of CapabilityTag
  Estas capabilities son DATOS, no código.
  Una unidad MEDIEVAL_BUILDER puede tener:
    { CAP_MOVE, CAP_ATTACK_MELEE, CAP_BUILD }
  con BuildActionConfig restringido a { WATCH_TOWER }.

  Un caballero con mejora puede tener:
    { CAP_MOVE, CAP_ATTACK_MELEE, CAP_CONVERT }
  con ConvertActionConfig restringido a { ClassTag::MILITARY_UNIT }.
end note

@enduml
```

---

## Diagrama 2: ActionConfig — La configuración de cada capability

```plantuml
@startuml ActionConfigs
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "ActionConfig Hierarchy" {

  abstract class ActionConfig {
    + CapabilityTag capability
    + {abstract} ActionConfig* merge(const ActionConfig* other) const
    ' merge() combina dos configs del mismo tipo
    ' (ej: dos BuildActionConfigs se unen sus listas)
  }

  note top of ActionConfig
    Cada CapabilityTag tiene su propia subclase de ActionConfig.
    Se almacenan en un mapa: CapabilityTag → ActionConfig*
    Las subclases definen QUÉ puede hacer la unidad con esa capability.
  end note

  ' ─── Construcción ───────────────────────────────────────
  class BuildActionConfig {
    + std::vector<ObjTag> buildable_tags
    ' Qué edificios puede construir esta unidad.
    ' Vacío = puede construir todo lo del jugador.
    ' { WATCH_TOWER } = solo torres de guardia.
    + ActionConfig* merge(const ActionConfig* other) const
    ' merge: une los dos buildable_tags (union de conjuntos)
  }

  note right of BuildActionConfig
    Caso 1 — "unidad que construye torres":
    Modifier otorga CAP_BUILD con:
      buildable_tags = { WATCH_TOWER }
    Un aldeano tiene:
      buildable_tags = {} (vacío = todo)
  end note

  ' ─── Conversión ─────────────────────────────────────────
  class ConvertActionConfig {
    + std::vector<ClassTag>  targetable_classes
    ' Qué clases puede convertir (vacío = todas).
    + std::vector<ObjTag>    targetable_tags
    ' ObjTags específicos convertibles (vacío = todos de la clase).
    + bool                   can_convert_buildings
    + float                  convert_speed_modifier
    + ActionConfig* merge(const ActionConfig* other) const
    ' merge: une targetable_classes y targetable_tags
  }

  note right of ConvertActionConfig
    Caso 2 — "caballeros que convierten infantería":
    Tech otorga CAP_CONVERT con:
      targetable_classes = { MILITARY_UNIT }
      can_convert_buildings = false
    Solo infantería militar. No edificios.

    Monje base tiene:
      targetable_classes = {} (vacío = todo)
      can_convert_buildings = true
  end note

  ' ─── Transformación / Morfosis ──────────────────────────
  class MorphActionConfig {
    + ObjTag  target_form
    ' En qué se transforma la unidad.
    + bool    consume_original
    ' true  = la unidad original desaparece (sacrificio).
    ' false = la unidad solo cambia de stats (reversible).
    + bool    inherits_hp_percent
    ' Si el nuevo form hereda el % de HP actual.
    + float   cooldown_sec
    + ActionConfig* merge(const ActionConfig* other) const
    ' merge: el último target_form gana (override)
  }

  note right of MorphActionConfig
    Caso 3 — "unidad se sacrifica → edificio":
    Skill activa ejecuta morph:
      target_form      = TOWER
      consume_original = true
      inherits_hp_percent = false
    La unidad desaparece, aparece una torre en su lugar.

    Trebuchet (pack/unpack):
      target_form      = TREBUCHET_PACKED
      consume_original = false  (reversible)
  end note

  ' ─── Curación ───────────────────────────────────────────
  class HealActionConfig {
    + std::vector<ClassTag>  healable_classes
    + float                  heal_range
    + float                  heal_rate_override
    ' 0 = usa HEAL_RATE del stat
    + ActionConfig* merge(const ActionConfig* other) const
  }

  ' ─── Recolección ────────────────────────────────────────
  class GatherActionConfig {
    + std::vector<ResourceTag> gatherable_resources
    ' Vacío = todos. Pescador solo tiene FOOD.
    + float gather_rate_override
    ' 0 = usa GATHER_RATE del stat
    + ActionConfig* merge(const ActionConfig* other) const
  }

  ' ─── Garrisón (aceptar unidades) ────────────────────────
  class GarrisonAcceptConfig {
    + std::vector<ClassTag>  accepted_classes
    + uint32_t               capacity_override
    ' 0 = usa max_sheltered_units del EntityStats
    + float                  arrow_per_unit
    ' Flechas que dispara el edificio por unidad guarecida
    + ActionConfig* merge(const ActionConfig* other) const
  }

  ActionConfig <|-- BuildActionConfig
  ActionConfig <|-- ConvertActionConfig
  ActionConfig <|-- MorphActionConfig
  ActionConfig <|-- HealActionConfig
  ActionConfig <|-- GatherActionConfig
  ActionConfig <|-- GarrisonAcceptConfig
}
@enduml
```

---

## Diagrama 3: IActionBehavior — Strategy para acciones ordenadas

```plantuml
@startuml ActionBehaviors
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Action Behavior System (Strategy)" {

  note as Context
    IActionBehavior = acción que el JUGADOR ordena ("build here", "attack that", "convert").
    ISkillBehavior  = habilidad con cooldown que se ACTIVA.
    Son distintas: un monje usa IActionBehavior para convertir (la ordenas)
    pero usa ISkillBehavior si tiene una habilidad especial de área.
  end note

  abstract class IActionBehavior {
    + CapabilityTag required_capability
    + {abstract} bool can_execute(\n    const SpawnableEntity* actor,\n    const WorldState& world) const
    + {abstract} void on_start(\n    SpawnableEntity* actor,\n    WorldState& world,\n    sf::Vector2f target_pos,\n    SpawnableEntity* target_entity)
    + {abstract} void on_tick(\n    SpawnableEntity* actor,\n    WorldState& world,\n    float dt)
    + {abstract} void on_interrupt(\n    SpawnableEntity* actor)
    + {abstract} bool is_done() const
  }

  class AttackActionBehavior {
    ' Requiere: CAP_ATTACK_MELEE o CAP_ATTACK_RANGED
    - SpawnableEntity* target
    - float            attack_cooldown_remaining
    + bool can_execute(...) const
    + void on_start(...)
    + void on_tick(...)
    + void on_interrupt(...)
    + bool is_done() const
  }

  class BuildActionBehavior {
    ' Requiere: CAP_BUILD
    ' Respeta BuildActionConfig::buildable_tags
    - ObjTag         building_to_place
    - sf::Vector2f   build_target_pos
    - float          build_progress
    - SpawnableEntity* construction_site
    + bool can_execute(...) const
    ' Verifica que building_to_place esté en BuildActionConfig
    + void on_start(...)
    + void on_tick(...)
    ' Avanza build_progress hasta completion_time del edificio
    + void on_interrupt(...)
    + bool is_done() const
  }

  class ConvertActionBehavior {
    ' Requiere: CAP_CONVERT
    ' Respeta ConvertActionConfig::targetable_classes y targetable_tags
    - SpawnableEntity* target
    - float            convert_elapsed
    - bool             interrupted
    + bool can_execute(...) const
    ' Verifica que el target cumpla ConvertActionConfig
    + void on_start(...)
    + void on_tick(...)
    ' Se interrumpe si el monje/caballero recibe daño
    + void on_interrupt(...)
    + bool is_done() const
  }

  class MorphActionBehavior {
    ' Requiere: CAP_MORPH
    ' Lee MorphActionConfig del actor
    - bool done
    + bool can_execute(...) const
    + void on_start(...)
    ' Ejecuta el morph:
    '   Si consume_original: despawn actor, spawn target_form
    '   Si no: cambia base_stats_ref al nuevo ObjSheet
    + void on_tick(...)
    + void on_interrupt(...)
    + bool is_done() const
  }

  note right of MorphActionBehavior
    Caso 3 — unidad castillo → torre:
    actor->get_action_config(CAP_MORPH)
      → MorphActionConfig { TOWER, consume_original=true }
    on_start:
      nueva_torre = world.spawn_manager.spawn(TOWER,
                     actor->original_player,
                     actor_pos)
      world.spawn_manager.despawn(actor)
    La unidad desaparece, aparece la torre.
  end note

  class GatherActionBehavior {
    ' Requiere: CAP_GATHER_*
    - ResourceFont* resource_target
    - float         carry_current
    + bool can_execute(...) const
    + void on_start(...)
    + void on_tick(...)
    + void on_interrupt(...)
    + bool is_done() const
  }

  class HealActionBehavior {
    ' Requiere: CAP_HEAL_UNITS
    - SpawnableEntity* target
    - float            heal_cooldown
    + bool can_execute(...) const
    + void on_start(...)
    + void on_tick(...)
    + void on_interrupt(...)
    + bool is_done() const
  }

  class MoveActionBehavior {
    ' Requiere: CAP_MOVE o CAP_MOVE_NAVAL
    - std::vector<sf::Vector2i> path
    - int                       path_index
    + bool can_execute(...) const
    + void on_start(...)
    ' Solicita path al PathfindingGrid
    + void on_tick(...)
    + void on_interrupt(...)
    + bool is_done() const
  }

  class ActionFactory {
    + {static} std::unique_ptr<IActionBehavior> create(\n    CapabilityTag cap)
  }

  IActionBehavior <|-- AttackActionBehavior
  IActionBehavior <|-- BuildActionBehavior
  IActionBehavior <|-- ConvertActionBehavior
  IActionBehavior <|-- MorphActionBehavior
  IActionBehavior <|-- GatherActionBehavior
  IActionBehavior <|-- HealActionBehavior
  IActionBehavior <|-- MoveActionBehavior
  ActionFactory ..> IActionBehavior : crea
}
@enduml
```

---

## Diagrama 4: TriggerSystem — Reacciones pasivas a eventos

```plantuml
@startuml TriggerSystem
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Trigger System" {

  enum TriggerEvent {
    ON_DEATH
    ' La unidad llega a 0 HP.
    ON_KILL
    ' La unidad mata a otra.
    ON_HIT
    ' La unidad recibe daño.
    ON_CONVERT_SUCCESS
    ' La unidad completa una conversión.
    ON_SPAWN
    ' La unidad acaba de ser creada.
    ON_IDLE
    ' La unidad no tiene órdenes.
    ON_GARRISON_ENTER
    ' La unidad entra en una estructura.
    ON_GARRISON_EXIT
    ' La unidad sale de una estructura.
    ON_AGE_UP
    ' El jugador dueño sube de edad.
  }

  ' ─── Efectos que puede producir un trigger ───────────────

  struct SpawnTriggerEffect {
    + ObjTag  spawn_tag
    ' Qué unidad/edificio spawnear.
    + bool    inherit_original_player
    ' true = el spawneado tiene el mismo original_player
    '        que la unidad que murió.
    + bool    inherit_current_player
    ' true = el spawneado pertenece al current_player
    '        de la unidad que disparó el trigger.
    + bool    at_same_position
    + float   hp_fraction_inherited
    ' 0.0 = HP máximo. >0 = fracción del HP actual.
  }

  note right of SpawnTriggerEffect
    Caso 4 — caballero muere → jinete como infantería:
    SpawnTriggerEffect {
      spawn_tag               = MAN_AT_ARMS
      inherit_original_player = true
      inherit_current_player  = true
      at_same_position        = true
      hp_fraction_inherited   = 0.5f
    }
    El Man-at-Arms hereda ambos punteros
    del caballero → si el caballero era azteca
    convertido, el soldado también lo será.
  end note

  struct ApplyModifierTriggerEffect {
    + Modifier   modifier_to_apply
    + bool       apply_to_self
    + bool       apply_to_killer
    + bool       apply_to_player_wide
    ' Ej: "al morir, el asesino recibe -10% velocidad"
  }

  struct ActivateSkillTriggerEffect {
    + std::string  skill_id
    ' Activa una skill del catálogo como efecto del trigger.
    ' Ej: "al recibir daño, activa escudo temporal"
  }

  struct ResourceTriggerEffect {
    + ResourceTag  resource
    + int32_t      amount
    ' Positivo = otorga. Negativo = consume.
    + bool         to_original_player
    + bool         to_current_player
    ' Ej: "al morir la unidad, devuelve 50% de su costo en oro"
  }

  ' ─── La plantilla de un trigger (en DataRegistry) ────────

  class TriggerTemplate {
    + std::string    trigger_id
    + TriggerEvent   on_event
    ' Cuándo dispara.
    + ObjTag         filter_killed_tag
    ' Solo para ON_KILL: solo dispara si mató a este ObjTag.
    + ClassTag       filter_killed_class
    ' Solo para ON_KILL: solo dispara si mató a esta clase.
    + float          probability
    ' 1.0 = siempre. 0.5 = 50% de chance.
    ' ─── Efectos al disparar ──────────────────────────────
    + std::vector<SpawnTriggerEffect>          spawn_effects
    + std::vector<ApplyModifierTriggerEffect>  modifier_effects
    + std::vector<ActivateSkillTriggerEffect>  skill_effects
    + std::vector<ResourceTriggerEffect>       resource_effects
  }

  ' ─── Instancia viva del trigger en una unidad ────────────

  struct TriggerInstance {
    + const TriggerTemplate*  tmpl
    ' Puntero a la plantilla (read-only).
    + bool    is_permanent
    ' false = desaparece tras dispararse una vez.
    + int     charges_remaining
    ' -1 = infinito. >0 = se agota.
  }

  ' ─── El despachador de triggers ─────────────────────────

  class TriggerDispatcher {
    + {static} void dispatch(\n    TriggerEvent event,\n    SpawnableEntity* source,\n    SpawnableEntity* related,\n    WorldState& world,\n    GameState& game)
    ' Itera TriggerInstance del source,
    ' ejecuta los efectos de los que coincidan con event.
    ' related = quién lo mató (para ON_DEATH),
    '           quién mató (para ON_KILL), etc.
  }

  note bottom of TriggerDispatcher
    Caso 4 — flujo completo:
    1. knight->hp = 0
    2. WorldState::process_death(knight, killer)
    3. TriggerDispatcher::dispatch(ON_DEATH, knight, killer, ...)
    4. Encuentra TriggerInstance { "knight_death_rise" }
    5. Ejecuta SpawnTriggerEffect:
         new_unit = spawn_manager.spawn(MAN_AT_ARMS, pos)
         new_unit->original_player = knight->original_player
         new_unit->current_player  = knight->current_player
         new_unit->hp_current = knight->hp_max * 0.5f
    6. despawn(knight)
  end note

  TriggerTemplate "1" *-- "many" SpawnTriggerEffect
  TriggerTemplate "1" *-- "many" ApplyModifierTriggerEffect
  TriggerTemplate "1" *-- "many" ActivateSkillTriggerEffect
  TriggerTemplate "1" *-- "many" ResourceTriggerEffect
  TriggerInstance "many" o-- "1" TriggerTemplate
  TriggerDispatcher ..> TriggerInstance : evalúa
  TriggerDispatcher ..> TriggerTemplate : lee efectos
}
@enduml
```

---

## Diagrama 5: ObjectModifier actualizado — Otorgar y quitar capabilities y triggers

```plantuml
@startuml ObjectModifierUpdated
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "ObjectModifier (v2)" {

  note as Context
    ObjectModifier es el payload de un Modifier que
    afecta las CAPACIDADES y TRIGGERS de una unidad,
    en lugar de sus stats numéricos (eso lo hace StatModifier).
    Un solo Modifier puede contener múltiples ObjectModifiers.
  end note

  struct ObjectModifier {
    ' ── Capability grants / removals ──────────────────────
    + CapabilityTag  grant_capability
    ' CAP_NONE = no otorga. Cualquier otro = otorga esa cap.
    + CapabilityTag  remove_capability
    ' CAP_NONE = no quita. Cualquier otro = quita esa cap.

    ' ── ActionConfig asociada a la capability otorgada ────
    + std::unique_ptr<ActionConfig>  action_config
    ' Si grant_capability != CAP_NONE y action_config != null,
    ' esta config se merge con la que ya tenga la unidad
    ' para esa capability.
    ' Ej: otorgar CAP_BUILD + BuildActionConfig{WATCH_TOWER}

    ' ── Trigger grants / removals ─────────────────────────
    + std::string  grant_trigger_id
    ' ID de TriggerTemplate en DataRegistry. "" = no otorga.
    + std::string  remove_trigger_id
    ' ID del trigger a eliminar de la unidad. "" = no quita.

    ' ── Skill grants / removals ───────────────────────────
    + std::string  grant_skill_id
    ' ID de SkillTemplate en DataRegistry. "" = no otorga.
    + std::string  remove_skill_id
    ' ID de la skill a quitar de la unidad.
  }

  note right of ObjectModifier
    Caso 1 — tech "Ingeniería de Campo":
    Modifier {
      target_class_tag = MILITARY_UNIT
      object_mods = [{
        grant_capability = CAP_BUILD
        action_config = BuildActionConfig {
          buildable_tags = { WATCH_TOWER, PALISADE_WALL }
        }
      }]
    }

    Caso 2 — tech "Caballería Mística":
    Modifier {
      target_obj_tag = KNIGHT
      object_mods = [{
        grant_capability = CAP_CONVERT
        action_config = ConvertActionConfig {
          targetable_classes = { MILITARY_UNIT }
          can_convert_buildings = false
        }
      }]
    }

    Caso 4 — innato del DEATH_KNIGHT (en ObjSheet):
    ObjectModifier dentro del ObjSheet base:
      grant_trigger_id = "death_knight_rise_as_infantry"
    El trigger está en DataRegistry con:
      on_event = ON_DEATH
      spawn_effects = [{ MAN_AT_ARMS, inherit_players=true }]
  end note

  ' ─── Modifier completo (recordatorio) ───────────────────
  class Modifier {
    + std::string        modifier_id
    + ModifierSource     source
    + ModifierScope      scope
    + ObjTag             target_obj_tag
    + ClassTag           target_class_tag
    + bool               is_permanent
    + float              duration_seconds
    ' ─── Payloads ────────────────────────────────────────
    + std::vector<StatModifier>    stat_mods
    + std::vector<AttackBonus>     attack_bonuses
    + std::vector<ObjectModifier>  object_mods
    ' Un Modifier puede tener stat_mods Y object_mods a la vez.
    ' Ej: una tech puede +2 ataque Y otorgar CAP_BUILD.
  }

  Modifier "1" *-- "many" ObjectModifier
  ObjectModifier "1" o-- "0..1" ActionConfig
}
@enduml
```

---

## Diagrama 6: EntityStats y ObjSheet actualizados — Capabilities base

```plantuml
@startuml EntityStatsUpdated
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Data Layer — Actualizado" {

  class EntityStats {
    + ClassTag  class_tag
    + float     max_hp
    + float     creation_time_sec
    + AttackTag attack_type
    + float     attack_dmg_base
    + float     attack_rate_base
    + std::vector<AttackBonus>   base_attack_bonuses
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
    ' ─── NUEVO: capabilities base ────────────────────────
    + std::unordered_set<CapabilityTag>  base_capabilities
    ' Lo que la unidad puede hacer por defecto.
    ' Ej: KNIGHT = { CAP_MOVE, CAP_ATTACK_MELEE, CAP_GARRISON_ENTER }
    '     VILLAGER = { CAP_MOVE, CAP_GATHER_*, CAP_BUILD, CAP_REPAIR }
    '     TOWER = { CAP_ATTACK_RANGED, CAP_GARRISON_ACCEPT }
    ' ─── NUEVO: action configs base ──────────────────────
    + std::unordered_map<CapabilityTag,\n  std::unique_ptr<ActionConfig>>  base_action_configs
    ' Config por defecto para cada capability.
    ' Ej: VILLAGER → BuildActionConfig{ buildable_tags={} (vacío=todo) }
    ' ─── NUEVO: triggers innatos ─────────────────────────
    + std::vector<std::string>  base_trigger_ids
    ' IDs de TriggerTemplates que la unidad tiene desde que nace.
    ' Ej: DEATH_KNIGHT → { "death_knight_rise_as_infantry" }
    ' ─── NUEVO: skills innatas ───────────────────────────
    + std::vector<std::string>  base_skill_ids
    ' IDs de SkillTemplates disponibles por defecto.
  }

  note right of EntityStats
    Ejemplos de base_capabilities:

    VILLAGER:
      { CAP_MOVE, CAP_ATTACK_MELEE,
        CAP_GATHER_FOOD, CAP_GATHER_WOOD,
        CAP_GATHER_GOLD, CAP_GATHER_STONE,
        CAP_BUILD, CAP_REPAIR, CAP_GARRISON_ENTER }

    KNIGHT:
      { CAP_MOVE, CAP_ATTACK_MELEE, CAP_GARRISON_ENTER }

    MONK:
      { CAP_MOVE, CAP_CONVERT, CAP_HEAL_UNITS,
        CAP_PICK_RELIC, CAP_GARRISON_ENTER }

    TREBUCHET:
      { CAP_MOVE, CAP_ATTACK_SIEGE, CAP_PACK_UNPACK }

    WATCH_TOWER:
      { CAP_ATTACK_RANGED, CAP_GARRISON_ACCEPT }
      (¡sin CAP_MOVE!)

    DEATH_KNIGHT (único de civi):
      { CAP_MOVE, CAP_ATTACK_MELEE, CAP_GARRISON_ENTER }
      base_trigger_ids = { "death_knight_rise_as_infantry" }
  end note

  class ObjSheet {
    + ObjTag       tag
    + EntityStats  base_stats
    + ProductionRecipe recipe
    + std::vector<std::string>  available_skill_ids
    + std::vector<std::string>  available_tech_ids
    + std::vector<ObjTag>       can_produce_tags
    ' (sin cambios estructurales: las capabilities
    '  ya viven dentro de EntityStats)
  }

  ObjSheet "1" *-- "1" EntityStats
}
@enduml
```

---

## Diagrama 7: SpawnableEntity actualizado — Capabilities dinámicas

```plantuml
@startuml SpawnableEntityUpdated
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "Entity Layer — Actualizado" {

  struct InstanceModifierStack {
    + std::vector<Modifier>  modifiers
    + bool                   is_dirty
  }

  class SpawnableEntity {
    + ObjTag     obj_tag
    + ClassTag   class_tag
    + float      hp_current
    + PlayerState* original_player
    + PlayerState* current_player

    + const EntityStats*  base_stats_ref

    + InstanceModifierStack  instance_modifiers

    ' ─── NUEVO: capabilities computadas ──────────────────
    + std::unordered_set<CapabilityTag>  current_capabilities
    ' = base_capabilities
    '   + caps otorgadas por original_player->active_modifiers
    '   + caps otorgadas por instance_modifiers
    '   - caps removidas por cualquiera de los anteriores
    ' Se recalcula cuando algún modifier cambia (is_dirty).

    ' ─── NUEVO: action configs mergeadas ─────────────────
    + std::unordered_map<CapabilityTag,\n  std::unique_ptr<ActionConfig>>  current_action_configs
    ' = base_action_configs mergeadas con las de los modifiers.

    ' ─── NUEVO: triggers activos ──────────────────────────
    + std::vector<TriggerInstance>  active_triggers
    ' = base triggers + triggers otorgados por modifiers
    '   - triggers removidos por modifiers.

    ' ─── Skills (igual que antes) ─────────────────────────
    + std::vector<ActiveSkillState>  skill_states

    ' ─── Garrisón y producción (igual que antes) ──────────
    + std::vector<SpawnableEntity*>  sheltered_units
    + std::queue<ProductionJob>      production_queue

    ' ─── Acción en curso (la que el jugador ordenó) ───────
    + std::unique_ptr<IActionBehavior>  current_action
    ' Solo una acción ordenada a la vez (move, attack, build...).
    ' Las skills van en skill_states separadas.

    ' ─── Métodos de capabilities ──────────────────────────
    + bool has_capability(CapabilityTag cap) const
    + const ActionConfig* get_action_config(CapabilityTag cap) const
    + void rebuild_capabilities()
    ' Recalcula current_capabilities y current_action_configs
    ' desde base + player mods + instance mods.
    ' Llamado cuando is_dirty en algún modifier stack.

    ' ─── Métodos de stats (sin cambios) ──────────────────
    + float get_stat(StatTag stat) const
    + std::vector<AttackBonus> get_attack_bonuses() const

    ' ─── Acciones básicas ─────────────────────────────────
    + void order_action(\n    std::unique_ptr<IActionBehavior> action)
    + void take_damage(float raw_dmg, AttackTag type)
    + void heal(float amount)
    + void convert_to(PlayerState* new_owner)
    + void garrison(SpawnableEntity* unit)
    + void eject_all()
    + bool is_converted() const
    + void tick(float dt, WorldState& world, GameState& game)
    ' tick(): avanza current_action, skills, y verifica triggers
  }

  note right of SpawnableEntity
    rebuild_capabilities() hace:
    1. Copia base_stats_ref->base_capabilities
    2. Para cada Modifier en original_player->active_modifiers:
         - Filtra si aplica a (obj_tag, class_tag)
         - Procesa cada ObjectModifier:
             grant_capability → añade a current_capabilities
             remove_capability → quita de current_capabilities
             action_config → merge en current_action_configs
             grant_trigger_id → añade TriggerInstance
             remove_trigger_id → quita TriggerInstance
    3. Igual con instance_modifiers (encima)

    has_capability() es O(1) → solo lookup en current_capabilities.

    order_action() hace:
      if current_action: current_action->on_interrupt(this)
      current_action = std::move(action)
      current_action->on_start(this, world, ...)

    tick() hace:
      current_action->on_tick(this, world, dt)
      if current_action->is_done(): current_action = nullptr
      for skill in skill_states: skill.tick(dt)
  end note

  SpawnableEntity "1" *-- "1"    InstanceModifierStack
  SpawnableEntity "1" *-- "many" TriggerInstance
  SpawnableEntity "1" *-- "many" ActiveSkillState
  SpawnableEntity "1" o-- "0..1" IActionBehavior : current_action
  SpawnableEntity ..>  PlayerState : original_player
  SpawnableEntity ..>  PlayerState : current_player
  SpawnableEntity ..>  TriggerDispatcher : cuando take_damage / muere
}
@enduml
```

---

## Diagrama 8: Los 4 casos de uso en acción

```plantuml
@startuml UseCases
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea
skinparam noteBackgroundColor #0f3460

package "Caso 1 — Unidad que construye torres (no aldeano)" #0f2040 {
  note as C1
    **Tech: "Ingeniería de Campo"**
    Researched at: BARRACKS (feudal age)
    grants Modifier {
      target_class_tag = MILITARY_UNIT
      object_mods = [ObjectModifier {
        grant_capability  = CAP_BUILD
        action_config     = BuildActionConfig {
                              buildable_tags = { WATCH_TOWER }
                            }
      }]
    }

    Resultado:
    ► Toda unidad militar del jugador:
        current_capabilities.contains(CAP_BUILD) == true
        get_action_config(CAP_BUILD)
          → BuildActionConfig { buildable_tags = {WATCH_TOWER} }

    ► Jugador selecciona infantería → icono "Construir"
        Si el jugador tiene un aldeano Y un guerrero seleccionados
        y ordena construir una torre:
          - aldeano: usa BuildActionBehavior,
                     can_execute() → buildable_tags vacío = todo ✓
          - guerrero: usa BuildActionBehavior,
                      can_execute() → WATCH_TOWER en su config ✓
          - guerrero no puede construir BARRACKS:
                      can_execute() → BARRACKS no en buildable_tags ✗
  end note
}

package "Caso 2 — Caballeros que convierten infantería" #1a0f40 {
  note as C2
    **Tech: "Caballería Mística"** (unique tech del Castillo)
    grants Modifier {
      target_obj_tag = KNIGHT  (solo caballeros)
      object_mods = [ObjectModifier {
        grant_capability  = CAP_CONVERT
        action_config     = ConvertActionConfig {
                              targetable_classes = { MILITARY_UNIT }
                              can_convert_buildings = false
                              convert_speed_modifier = 0.5f
                            }
      }]
    }

    Resultado:
    ► Solo los KNIGHT del jugador:
        current_capabilities.contains(CAP_CONVERT) == true
        get_action_config(CAP_CONVERT)
          → ConvertActionConfig { MILITARY_UNIT, no buildings, 0.5x speed }

    ► MONK del mismo jugador sigue con su ConvertActionConfig base:
        targetable_classes = {} (vacío = todo)
        can_convert_buildings = true
        (los modifiers solo aplican a KNIGHT por target_obj_tag)

    ► Knight intenta convertir un TREBUCHET (siege weapon):
        ConvertActionBehavior::can_execute():
          target->class_tag == SIEGE_WEAPON
          SIEGE_WEAPON no está en targetable_classes → ✗ cancelado
  end note
}

package "Caso 3 — Unidad se sacrifica → edificio" #401a0f {
  note as C3
    **ObjSheet: RITUAL_WARRIOR** (unique unit del Castillo)
    EntityStats::base_capabilities = {
      CAP_MOVE, CAP_ATTACK_MELEE, CAP_MORPH
    }
    base_action_configs[CAP_MORPH] = MorphActionConfig {
      target_form      = RITUAL_TOWER
      consume_original = true
      inherits_hp_percent = false
    }

    También tiene skill activa "Ritual del Sacrificio":
    → Cuando el jugador la activa, ejecuta:
    SkillBehavior (MorphSkillBehavior):
      1. Verifica actor->has_capability(CAP_MORPH) → true
      2. Lee actor->get_action_config(CAP_MORPH)
             → MorphActionConfig { RITUAL_TOWER, consume=true }
      3. pos = world.spatial.get_position(actor)
      4. nueva_torre = spawn_manager.spawn(RITUAL_TOWER,
                         actor->original_player, pos)
      5. spawn_manager.despawn(actor)

    ► Sin la tech "Ingeniería de Campo" un guerrero normal
      NO tiene CAP_MORPH, así que la skill no aplica a él.
    ► Solo el RITUAL_WARRIOR tiene esta capability en su base.
  end note
}

package "Caso 4 — Caballero muere → jinete se levanta" #0f400f {
  note as C4
    **ObjSheet: DEATH_KNIGHT** (unique unit)
    EntityStats::base_capabilities = {
      CAP_MOVE, CAP_ATTACK_MELEE, CAP_GARRISON_ENTER
    }
    base_trigger_ids = { "death_knight_rise_as_infantry" }

    **TriggerTemplate en DataRegistry:**
    TriggerTemplate {
      trigger_id    = "death_knight_rise_as_infantry"
      on_event      = ON_DEATH
      probability   = 1.0f
      spawn_effects = [SpawnTriggerEffect {
        spawn_tag               = MAN_AT_ARMS
        inherit_original_player = true
        inherit_current_player  = true
        at_same_position        = true
        hp_fraction_inherited   = 0.5f
      }]
    }

    **Flujo en GameState::tick():**
    1. dk->hp_current -= net_damage → llega a 0
    2. WorldState::process_death(dk, killer)
    3. TriggerDispatcher::dispatch(ON_DEATH, dk, killer, world, game)
    4. Itera dk->active_triggers:
         TriggerInstance { "death_knight_rise_as_infantry", charges=-1 }
         on_event == ON_DEATH → dispara
    5. Ejecuta SpawnTriggerEffect:
         new_unit = spawn_manager.spawn(MAN_AT_ARMS, dk_pos)
         new_unit->original_player = dk->original_player
         new_unit->current_player  = dk->current_player
         new_unit->hp_current = new_unit->base_stats->max_hp * 0.5f
         new_unit->rebuild_capabilities()
    6. spawn_manager.despawn(dk)

    ► Si el Death Knight había sido convertido:
         original_player = &azteca, current_player = &espanol
         → El nuevo Man-at-Arms también es azteca-convertido-espanol
         → Sus stats son del azteca, su allegiance es española
  end note
}
@enduml
```

---

## Diagrama 9: Vista completa del sistema de mutabilidad

```plantuml
@startuml MutabilityOverview
skinparam backgroundColor #1a1a2e
skinparam classBackgroundColor #16213e
skinparam classBorderColor #0f3460
skinparam classArrowColor #e94560
skinparam classFontColor #eaeaea

package "DATA (plantillas estáticas)" #0f2040 {
  class EntityStats {
    + base_capabilities : Set<CapabilityTag>
    + base_action_configs : Map<Cap, ActionConfig>
    + base_trigger_ids : List<string>
    + base_skill_ids : List<string>
  }
  class TriggerTemplate
  class SkillTemplate
  class TechTemplate
}

package "MODIFIERS (vectores de cambio)" #1a0f40 {
  class Modifier
  struct StatModifier
  struct ObjectModifier {
    + grant_capability
    + remove_capability
    + action_config
    + grant_trigger_id
    + remove_trigger_id
    + grant_skill_id
    + remove_skill_id
  }
}

package "PLAYER STATE (mods activos)" #0f400f {
  class PlayerState {
    + active_modifiers : List<Modifier>
  }
}

package "ENTITY (instancia viva)" #401a0f {
  class SpawnableEntity {
    + current_capabilities : Set<CapabilityTag>
    + current_action_configs : Map<Cap, ActionConfig>
    + active_triggers : List<TriggerInstance>
    + skill_states : List<ActiveSkillState>
    + current_action : IActionBehavior*
    + rebuild_capabilities()
  }
}

package "BEHAVIORS (ejecución)" #1a3040 {
  interface IActionBehavior
  interface ISkillBehavior
  class TriggerDispatcher
}

EntityStats ..> SpawnableEntity : base
TechTemplate ..> Modifier : grants
PlayerState "1" o-- "many" Modifier
Modifier "1" *-- "many" ObjectModifier
ObjectModifier ..> SpawnableEntity : altera capabilities\nvía rebuild_capabilities()
SpawnableEntity "1" o-- "0..1" IActionBehavior
SpawnableEntity "1" o-- "many" ISkillBehavior
SpawnableEntity ..> TriggerDispatcher : dispatch on events
TriggerTemplate ..> TriggerDispatcher : define efectos
@enduml
```
