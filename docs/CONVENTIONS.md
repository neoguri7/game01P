<!-- doc-verify subsystem=conventions commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# Conventions

> The enforced coding conventions of `game01P`, consolidated from the `clangd`/`clang-tidy` config and the recovered engineering rules. Enforced naming comes from `.clangd`; behavior rules are team convention.

## Naming

Configured via `readability-identifier-naming` in `.clangd` (`src`-clangd config):

| Construct | Prefix/Case | Clangd option |
| --- | --- | --- |
| Namespace | `lower_case` | `.clangd:21` |
| Struct prefix | `F` | `.clangd:22` |
| Class prefix | `F` | `.clangd:23` |
| Enum prefix | `E` | `.clangd:24` |
| Function | `lowerCamelCase` | `.clangd:25` |

Known inconsistency: `FPosition` is declared in the **global** namespace (`src/ecs/components/FPosition.h:3`), unlike every other component which lives in `namespace game` (e.g. `src/ecs/components/FVelocity.h:3-4`). Treat `FPosition` as a prefixed struct; do not replicate the global-namespace placement.

## ECS rules

- **Components are pure public data structs** with no behavior and no methods (see [subsystems/ecs.md](subsystems/ecs.md)). The only exceptions are tiny helpers like `TacticalCommandActionName` (`src/ecs/components/FQueuedTacticalCommand.h:15`).
- **Systems are stateless functors** satisfying `SystemConcept` (`src/ecs/systems/ISystem.h:14`). They hold no per-entity state; all state lives in components.
- **Entity creation goes through factories** in `src/core/factories/`, never raw `registry.create()` for gameplay entities (see [subsystems/factories.md](subsystems/factories.md)).
- **Tag components for state**: phases/states are encoded as empty tag components (e.g. `FCombatStateSetup` at `src/ecs/components/FCombatStateSetup.h:5`).

## Services and communication

- **Shared services** are injected via `registry.ctx()` and fetched with `registry.ctx().find<T>()`. Services are owned by the `Engine` lifecycle (see [subsystems/core-engine.md](subsystems/core-engine.md)).
- **Systems communicate via `FEventBus`, never direct calls.** Use the typed helpers in `src/core/events/FEventPublishing.h` (`SubscribeEvent`, `PublishEvent`, `QueueFrameEvent`). Prefer these over the legacy macros in `src/core/events/FEventBus.h:97-109`.

## State machines

- **App-flow** (title/menu/scene) uses the polymorphic `GameStateMachine` in `src/states/`. Scope is explicitly limited — see `src/states/GameStateMachine.h:10-19`.
- **Gameplay state is forbidden in the app-flow state machine.** It is modeled with ECS tag components plus `FEventBus`-driven transition systems, each keeping a **documented transition table** beside the driving system. This doctrine is stated at `src/states/FBaseState.h:3-5`.
- See [subsystems/tactical-combat.md](subsystems/tactical-combat.md) for the combat transition table and [subsystems/app-flow-states.md](subsystems/app-flow-states.md) for the app-flow machine.

## Dependency direction

- `core/` ← `ecs/` ← `gameplay/`. `core/` must not include `ecs/` or `gameplay/`; `ecs/` must not include `gameplay/`. `gameplay/` is the only layer permitted to own the game rules. Rationale and diagram in [ARCHITECTURE.md](ARCHITECTURE.md).

## Data-driven values

- Gameplay tuning values (combat config, unit stats, board layout) are intended to load from data under `assets/data/`, with in-code fallback defaults.
- **Current state:** `assets/data/` does not exist; the intended config loader (`FTacticalCombatConfigLoader`) was planned in the removed `gameplay/` layer. [UNVERIFIED — out/build/macos-clang-debug/CMakeFiles/game01P.dir/src/gameplay/tactical_d20/config/] The loader lives under `gameplay/tactical_d20/config/`. Until restored, defaults are the fallback. See [RECOVERY.md](RECOVERY.md).

## Tooling conventions

- Formatting: `.clang-format` (run `clang-format -i` on changed files).
- Static analysis: `.clang-tidy` (~150 lines) is wired into `clangd` diagnostics (`.clangd:18-26`).
- Editor: `.editorconfig` for base whitespace settings.
- See [BUILD_AND_TOOLING.md](BUILD_AND_TOOLING.md) for setup.

## Cross-references

- [ARCHITECTURE.md](ARCHITECTURE.md)
- [subsystems/ecs.md](subsystems/ecs.md)
- [subsystems/factories.md](subsystems/factories.md)
- [subsystems/events.md](subsystems/events.md)
- [BUILD_AND_TOOLING.md](BUILD_AND_TOOLING.md)
