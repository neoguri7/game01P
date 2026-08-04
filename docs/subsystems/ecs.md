<!-- doc-verify subsystem=ecs commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# ecs

> `src/ecs/` — the EnTT ECS: the `SystemConcept`/`ISystem`/`SystemWrapper` contract, the `SystemManager` owner, the present base systems, and the component catalog.

## As-built

### System contract

Any concrete system must satisfy `SystemConcept` (`src/ecs/systems/ISystem.h:14`) — it provides `update(reg, dt)` and `name()`. It is stored as `ISystem` (`src/ecs/systems/ISystem.h:20`), which adds optional `onRegister()` and `render()` hooks (`src/ecs/systems/ISystem.h:26`,`:29`). `SystemWrapper<T>` (`src/ecs/systems/ISystem.h:33`) adapts a concrete stateless functor to `ISystem`.

Systems are **stateless** functors: state lives in components, never in the system instance.

### SystemManager

`game::SystemManager` (`src/core/SystemManager.h:19`) owns `unique_ptr<ISystem>` wrappers. Update/render order equals registration order (`src/core/SystemManager.h:17`).

| Method | Purpose | Anchor |
| --- | --- | --- |
| `addSystem<T>()` | register a concrete system (wrapped) | `src/core/SystemManager.h:24-29` |
| `addSystem(unique_ptr<ISystem>)` | register a prebuilt system | `src/core/SystemManager.h:31-36` |
| `onAllSystemsRegistered` | call `onRegister` on all | `src/core/SystemManager.h:38-42` |
| `updateAll` | run `update` on all | `src/core/SystemManager.h:44-49` |
| `renderAll` | run `render` on all | `src/core/SystemManager.h:51-56` |
| `getRegisteredCount` / `getSystemNames` | introspection | `src/core/SystemManager.h:58-67` |
| `clear` | drop all systems | `src/core/SystemManager.h:69-71` |

The `Engine` owns the `SystemManager` (`src/core/Engine.h:55`); `update`/`render` call `updateAll`/`renderAll` (`src/core/Engine.cpp:154`,`:177`).

### Present base systems

Four header-only systems exist and are ready to register (registered only by the missing `RegisterDefaultSystems` — see [RECOVERY.md](../RECOVERY.md)):

| System | Phase | Behavior | Anchor |
| --- | --- | --- | --- |
| `SpriteRenderSystem` | render | draws `FPosition`+`FSprite` entities sorted by `FLayer::depth` (default 0), via ctx `FResourceManager::tryLoadTexture` | `src/ecs/systems/SpriteRenderSystem.h:18` (sort `:40-41`, draw `:50`) |
| `CollisionSystem` | update | O(n²) AABB overlap over `FCollider`+`FPosition`, `queueFrame<FCollisionEvent>` | `src/ecs/systems/CollisionSystem.h:15` (view `:22`) |
| `AnimationSystem` | update | advances `FAnimation` frames and writes the linked `FSprite` | `src/ecs/systems/AnimationSystem.h:13` (view `:16`) |
| `MoveSystem` | update | `FPosition += FVelocity * dt` | `src/ecs/systems/MoveSystem.h:12` (view `:16`) |

### Component catalog

Components live in `src/ecs/components/` (49 files; one `.cpp`: `src/ecs/components/FQueuedTacticalCommand.cpp`). Data components are plain public structs in `namespace game` — with the single exception `FPosition`, which is in the **global** namespace (`src/ecs/components/FPosition.h:3`).

**Transform / render:** `FPosition` (global), `FVelocity`, `FSprite` (`texturePath`), `FLayer` (`depth`), `FSprFrame` (atlas rect + duration), `FAnimation` (`sheetPath`, `frames`, `timer`, `loop`), `FCamera`, `FText`, `FCollider` (`EColliderType` AABB/Circle).

**Tactical (data):** `FTacticalUnit` (`id`/`team`/`displayName`/`spawnOrder`), `FGridPosition` (int tile), `FHitPoints` (`current`/`max`), `FArmorClass` (`value`), `FSpeed` (`feet`), `FInitiativeBonus`, `FAbilityScores` (Str/Dex/Con/Int/Wis/Cha), `FTacticalAttack` (range, bonus, damage dice), `FTacticalBoardTile` (`tileX/Y`, `tileFeet`, `isWall`, `isCover`), `FTacticalTurnOrder` (`units`, `currentIndex=-1`, `round`), `FInitiativeRoll` (`naturalRoll`/`initiativeBonus`/`total`), `FTurnBudget` (`baseMovementFeet`/`remainingMovementFeet`), `FTurnResources` (hasAction/hasBonusAction/hasReaction), `FCommandToken`, `FQueuedTacticalCommand` (`ETacticalCommandAction`). See [tactical-combat](tactical-combat.md).

**Conditions (data):** `FConditionBurning` (`remainingRounds=2`), `FConditionPoisoned` (`remainingRounds=-1`), `FConditionStunned` (`remainingTurns=1`). See [tactical-combat](tactical-combat.md).

**Tag components (empty, 20 total):** the 11 `FCombatState{Setup,InitiativeRolling,RoundStart,TurnStart,AwaitingCommand,EnemyThinking,ResolvingAction,TurnEndCheck,NextTurn,Victory,Defeat}*` (each `src/ecs/components/FCombatState*.h:5`), the 4 action-economy tags `FActionEconomy{HasActionOnly,HasMoveAndAction,HasMoveOnly,TurnComplete}`, plus `FActiveTacticalUnit`, `FPlayerControlledTacticalUnit`, `FAiControlledTacticalUnit`, `FUnitStateDefeated`, `FConditionDodge`.

## Intended / In-progress

- [UNVERIFIED — out/build/macos-clang-debug/CMakeFiles/game01P.dir/src/gameplay/tactical_d20/] The combat `gameplay/` systems (driving the `FCombatState*` tags) are unbuilt — see [tactical-combat](tactical-combat.md). Today no system on disk advances those tags.
- [UNVERIFIED — src/core/Engine.cpp:160] `Engine::update` has no gameplay-state hook besides the `// TODO` at `src/core/Engine.cpp:160`.

## How to extend

**Add a system:**
1. Create a header-only functor struct satisfying `SystemConcept` (`src/ecs/systems/ISystem.h:14`) — e.g. copy `src/ecs/systems/MoveSystem.h:12` as a starting shape.
2. (Optional) implement `onRegister`/`render` hooks (`src/ecs/systems/ISystem.h:26-29`). `render` is used for draw-phase systems.
3. Register it in `RegisterDefaultSystems` — [UNVERIFIED] the missing `gameplay/SystemRegistration.h` (see [RECOVERY.md](../RECOVERY.md)); the `Engine` then runs it every frame in registration order.

**Add a component:**
1. Create a data struct (or empty tag) in `namespace game` under `src/ecs/components/` with an `F`/`E` prefix per [CONVENTIONS.md](../CONVENTIONS.md).
2. It is picked up by the `GLOB_RECURSE` build automatically (`CMakeLists.txt:15-19`).
3. Attach it via a factory (preferred) or `registry.emplace<T>(entity, ...)`.

## Cross-references

- [ARCHITECTURE.md](../ARCHITECTURE.md) — layering + registration.
- [events](events.md) — how systems signal each other.
- [factories](factories.md) — where components get attached.
- [tactical-combat](tactical-combat.md) — the unbuilt combat systems + transition table.
- [CONVENTIONS.md](../CONVENTIONS.md)
