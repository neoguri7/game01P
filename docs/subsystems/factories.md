<!-- doc-verify subsystem=factories commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# factories

> `src/core/factories/` — static helpers that construct entities with a predefined component set. Gameplay entities MUST be created through a factory, never via raw `registry.create()`.

## As-built

Five factories exist. Each is a stateless struct with static `create` methods.

### FTacticalCombatStateFactory

Creates the combat "state entity" — one entity that carries the combat's global tags and turn-order bookkeeping (`src/core/factories/FTacticalCombatStateFactory.h:7-9`).

`createSetupState` (`src/core/factories/FTacticalCombatStateFactory.cpp:9-15`) stamps the entity with:
- `FCombatStateSetup` (tag; the initial combat state)
- `FTacticalTurnOrder` (turn-order bookkeeping, `currentIndex=-1`, `round=0`)
- `FTag("tactical_combat_state")`

### FTacticalUnitFactory

Creates a tactical combatant (`src/core/factories/FTacticalUnitFactory.h:32-33`) from an `FTacticalUnitSpawn` spec (`src/core/factories/FTacticalUnitFactory.h:10-33`, with `playerControlled` etc.). `create` (`src/core/factories/FTacticalUnitFactory.cpp:11-33`) stamps 12 components:

`FTacticalUnit`, `FGridPosition`, `FHitPoints`, `FArmorClass`, `FSpeed`, `FInitiativeBonus`, `FAbilityScores`, `FTacticalAttack`, `FTurnBudget`, `FTurnResources`, `FTag`, and either `FPlayerControlledTacticalUnit` or `FAiControlledTacticalUnit` depending on `spawn.playerControlled`.

### FTacticalBoardTileFactory

Creates a board tile entity (`create` at `src/core/factories/FTacticalBoardTileFactory.cpp:9-15`): stamps `FTacticalBoardTile`, `FGridPosition`, and `FTag` (`"tactical_wall"` if wall else `"tactical_floor"`).

### FDemoEntityFactory

Creates a debug demo entity (`src/core/factories/FDemoEntityFactory.cpp:9-15`): stamps `FPosition(200,200)`, `FVelocity(50,-30)`, `FTag("demo")`. Used by the Entity Inspector "Add Demo Entity" button via `CreateDebugDemoEntity` (see [debug-overlay](debug-overlay.md)).

### FDemoPlayerFactory

Player demo factory; mirrors the demo-entity pattern (position/velocity/tag), present as a factory for player-like demo entities.

## Intended / In-progress

- [UNVERIFIED — out/build/macos-clang-debug/CMakeFiles/game01P.dir/src/core/factories/] Stale build artifacts indicate an older, richer factory set under `src/core/factories/` (e.g. `FTacticalD20UnitFactory`, `FTacticalD20BoardTileFactory`, `FTacticalD20CombatStateFactory`, `FTacticalD20CommandTokenFactory`) that was renamed/consolidated into the five above by the `b048ebd` refactor. The DF20-derived board placement (`FTacticalD20BoardPlacement`) is also a stale artifact. These are historical; do not rely on them — use the five current factories.

## How to extend

**Add a factory for a new gameplay entity type:**
1. Create a header + `.cpp` under `src/core/factories/` (picked up by `GLOB_RECURSE` `CMakeLists.txt:15-19`).
2. Give it a static `create(entt::registry&, params...)` returning the new entity.
3. Stamp a data component for identity (e.g. `FTag`), the required data/tag components, and the player/AI marker where relevant — mirroring `FTacticalUnitFactory::create` (`src/core/factories/FTacticalUnitFactory.cpp:11-33`).
4. Callers use `registry.ctx()`-free static calls; the entity lands in the shared `registry`.

Prefer a factory over `registry.create()` + manual `emplace` so the component set is discoverable in one place.

## Cross-references

- [ecs](ecs.md) — the components factories stamp.
- [tactical-combat](tactical-combat.md) — the combat state + unit entities.
- [debug-overlay](debug-overlay.md) — demo-entity usage.
- [CONVENTIONS.md](../CONVENTIONS.md)
