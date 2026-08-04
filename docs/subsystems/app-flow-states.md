<!-- doc-verify subsystem=app-flow-states commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# app-flow-states

> `src/states/` — the high-level app-flow state machine (title / menu / scene flow). It is currently **dead/unwired** and is not used by the engine.

## As-built

### Scope doctrine

The state machine is explicitly for **app flow only**. Gameplay state (alive/dead/attacking/combat phases) must **not** live here — it belongs in ECS tag components plus `FEventBus`-driven transition systems with documented transition tables. This doctrine is stated twice:
- `src/states/FBaseState.h:3-5`
- `src/states/GameStateMachine.h:10-19`

### EStateTransition

`enum EStateTransition` (`src/states/FBaseState.h:6-16`): `None, ToTitle, ToHub, ToDungeon, ToBossFloor, ToCombat, ToGameOver, Quit`. It is the only legal output of a state's per-frame `Update`.

### FBaseState

Abstract base for app-flow states (`src/states/FBaseState.h:21-33`): `Update(float) -> EStateTransition`, plus `OnEnter`/`OnExit` hooks.

### GameStateMachine

`game::GameStateMachine` (`src/states/GameStateMachine.h:21`) is a lightweight polymorphic stack:
- `pushState` (`:23-29`) — push on top, exit previous, enter new.
- `switchState` (`:31-38`) — pop current, push new.
- `update` (declared `:40`) drives the top state and applies transitions.

Implementations: `FTitleState` and `FHubState` (in `src/states/FTitleState.{h,cpp}`, `FHubState.{h,cpp}`).

### Dead/unwired status (verified)

There are **zero references** to `GameStateMachine`, `FBaseState`, `EStateTransition`, `FTitleState`, or `FHubState` anywhere outside `src/states/` (confirmed by grep over `src/`). The engine's only mention is a comment: `// TODO: optional state machine update` at `src/core/Engine.cpp:160`. `Engine` never instantiates or calls the machine.

## Intended / In-progress

- [UNVERIFIED — src/core/Engine.cpp:160] The `GameStateMachine` is intended to be driven from `Engine::update` at `src/core/Engine.cpp:160` (the `// TODO`), enabling title → hub → dungeon/combat scene flow. Not implemented.

## Public API surface

| Symbol | Signature | Anchor |
| --- | --- | --- |
| `EStateTransition` | enum | `src/states/FBaseState.h:6-16` |
| `FBaseState::Update` | `EStateTransition(float)` | `src/states/FBaseState.h:27` |
| `FBaseState::OnEnter` / `OnExit` | `void()` | `src/states/FBaseState.h:30`,`:33` |
| `GameStateMachine::pushState` | `void(unique_ptr, registry&)` | `src/states/GameStateMachine.h:23` |
| `GameStateMachine::switchState` | `void(unique_ptr, registry&)` | `src/states/GameStateMachine.h:31` |

## How to extend

**Add an app-flow state:**
1. Derive from `FBaseState` (`src/states/FBaseState.h:21`), implement `Update` returning `EStateTransition`.
2. Implement `OnEnter`/`OnExit` for any transitions (e.g. load a scene's entities).
3. Wire the machine into `Engine::update` at the `// TODO` (`src/core/Engine.cpp:160`), or into a new app-flow system.
4. **Do not** put combat/gameplay state here — use ECS tags + `FEventBus` (see [tactical-combat](tactical-combat.md)).

## Cross-references

- [ARCHITECTURE.md](../ARCHITECTURE.md) — where the machine would hook in.
- [tactical-combat](tactical-combat.md) — where gameplay state belongs instead.
- [CONVENTIONS.md](../CONVENTIONS.md)
- [RECOVERY.md](../RECOVERY.md)
