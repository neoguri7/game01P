<!-- doc-verify subsystem=glossary commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# Glossary

> The shared vocabulary of `game01P`. Conceptual definitions first, each with a pointer to where it lives in the code.

## Core concepts

- **Entity** — an opaque handle (`entt::entity`) into the central `entt::registry`. Entities carry components; they have no behavior of their own.
- **Component** — a plain, public, data-only struct attached to an entity (e.g. position, sprite, hit points). Per convention, gameplay components are named with an `F` prefix (see [CONVENTIONS.md](CONVENTIONS.md)).
- **Tag component** — an empty component struct used purely as a marker, e.g. `FCombatStateSetup` (`src/ecs/components/FCombatStateSetup.h:5`). Presence of a tag on an entity means "this entity is in this state / has this property." No fields.
- **System** — a stateless functor that operates on a subset of entities each frame. Must satisfy `SystemConcept` (`src/ecs/systems/ISystem.h:14`): it provides `update(registry, dt)` and `name()`. Systems are the only place behavior lives.
- **Registry** — the single `entt::registry` owned by `Engine` (`src/core/Engine.h:36`). It is the database of entities + components and the host for shared **context services**.
- **Context service (ctx())** — a shared, engine-lifetime object stored in `registry.ctx()` and retrieved with `registry.ctx().find<T>()` / `get<T>()`. Examples: `FInputState`, `FAudioManager`, `FEventBus`, `FResourceManager`, raw `SDL_Renderer*`. See [subsystems/core-engine.md](subsystems/core-engine.md).
- **Factory** — a static helper that constructs entities with a predefined component set. Gameplay entities must be created through factories in `src/core/factories/`, never via raw `registry.create()`. See [subsystems/factories.md](subsystems/factories.md).

## Event bus

- **Event** — a small data struct describing "something happened" (e.g. a collision). Events are published or queued on the shared `FEventBus`.
- **publish** — immediate dispatch to subscribers, in subscription order (`src/core/events/FEventBus.h:46-55`). Use for actions that must be handled right away.
- **queueFrame** — stores an event for consumers later in the same frame; consumers read it with `frameEvents<T>()` (`src/core/events/FEventBus.h:57-75`). Frame-bound events are cleared at `beginFrame()`.
- **subscribe** — register a callback for an event type (`src/core/events/FEventBus.h:40-44`).
- **Frame-bound event** — an event delivered via `queueFrame`, available only within the current frame. See [subsystems/events.md](subsystems/events.md).

## State machine concepts

- **App-flow state machine** — the high-level title/menu/scene-flow stack in `src/states/`. **Must not** hold gameplay state. See [subsystems/app-flow-states.md](subsystems/app-flow-states.md).
- **Gameplay state** — modeled with ECS tag components + `FEventBus`-driven transition systems, not the polymorphic state stack (per the doctrine at `src/states/GameStateMachine.h:10-19`).
- **Transition table** — an explicit table, kept beside the system that drives a state machine, stating every `current state → event → next state` edge. Documented for the combat machine in [subsystems/tactical-combat.md](subsystems/tactical-combat.md).

## Layering

- **Dependency direction** — `core/` may be used by `ecs/`, and both may be used by `gameplay/`; the reverse is forbidden. `game` gameplay code must not import from `core`-ward layering. See [ARCHITECTURE.md](ARCHITECTURE.md).
- **Intended vs As-built** — As-built = what exists and compiles in the current tree. Intended/In-progress = recovered or planned, tagged `[UNVERIFIED]`. See [DOC_RULES.md](DOC_RULES.md#r2--as-built-vs-intended-split).
