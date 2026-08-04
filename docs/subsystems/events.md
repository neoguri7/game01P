<!-- doc-verify subsystem=events commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# events

> `src/core/events/` — the `FEventBus`, a type-erased, `registry.ctx()`-hosted decoupling bus. Systems signal each other here, never via direct calls.

## As-built

### FEventBus

`game::FEventBus` (`src/core/events/FEventBus.h:39`) lives in `registry.ctx()` (created by `InitializeEventBus`, `src/core/events/FEventBus.h:89`; called from `Engine` at `src/core/Engine.cpp:78`). State: `callbacks_` (type → subscriber list) and `frameEvents_` (type → frame queue).

Dispatch rules (authoritative doc comment at `src/core/events/FEventBus.h:23-38`):

| Operation | Timing | Anchor |
| --- | --- | --- |
| `subscribe<T>(fn)` | register a callback for type `T` | `src/core/events/FEventBus.h:40-44` |
| `publish<T>(event)` | **immediate** dispatch in subscription order | `src/core/events/FEventBus.h:46-55` |
| `queueFrame<T>(event)` | store a frame-bound event | `src/core/events/FEventBus.h:57-65` |
| `frameEvents<T>()` | read this frame's queued events of type `T` | `src/core/events/FEventBus.h:67-75` |
| `beginFrame()` | clear all frame queues (once per frame) | `src/core/events/FEventBus.h:77` |
| `clear()` | shutdown: drop subscriptions + queues | `src/core/events/FEventBus.h:79-82` |

`beginFrame()` runs at the top of every engine frame (`src/core/Engine.cpp:115`). Queued events survive the frame (available through update/render) but are **never dispatched implicitly** — consumers must read them with `frameEvents<T>()`.

### Typed helpers (preferred)

`src/core/events/FEventPublishing.h` wraps the bus for idioms:

| Helper | Effect | Anchor |
| --- | --- | --- |
| `PublishEvent(reg, event)` | immediate publish | `src/core/events/FEventPublishing.h:11-18` |
| `QueueFrameEvent(reg, event)` | queue frame-bound | `src/core/events/FEventPublishing.h:20-27` |
| `PublishAndQueueFrameEvent(reg, event)` | both | `src/core/events/FEventPublishing.h:29-37` |
| `SubscribeEvent(reg, fn)` | subscribe | `src/core/events/FEventPublishing.h:39-46` |

### Legacy macros

`SUBSCRIBE`/`PUBLISH`/`QUEUE_FRAME_EVENT` (`src/core/events/FEventBus.h:97-109`) are legacy convenience macros. Prefer the typed helpers above for new code (per [CONVENTIONS.md](../CONVENTIONS.md)). They resolve the bus via `registry.ctx()` and call the matching method.

### Known events

- `FCollisionEvent` (`src/core/events/FCollisionEvent.h:9`) — emitted (frame-queued) by `CollisionSystem` (`src/ecs/systems/CollisionSystem.h:15`), payload holds the two colliding entities.
- Command events from the debug UI (`FTacticalCommandRequestedEvent`) are `[UNVERIFIED]` — see [tactical-combat](tactical-combat.md) and [debug-overlay](debug-overlay.md).

## Intended / In-progress

- [UNVERIFIED — src/debug/TacticalCombatTelemetryPanel.cpp:13] Tactical-combat event types (e.g. `FTacticalCommandRequestedEvent`) are declared in the removed `gameplay/tactical_d20/events/` headers (`src/debug/TacticalCombatTelemetryPanel.cpp:18`). They are referenced by the debug panel but not on disk — see [debug-overlay](debug-overlay.md) and [RECOVERY.md](../RECOVERY.md).

## How to extend

**Add an event:**
1. Define a plain data struct for the event (with an `F`/`E` prefix per [CONVENTIONS.md](../CONVENTIONS.md)).
2. Producer: `PublishEvent(reg, evt)` for immediate, or `QueueFrameEvent(reg, evt)` for frame-bound (`src/core/events/FEventPublishing.h:11`,`:20`).
3. Consumer: in your system's `onRegister` (`src/ecs/systems/ISystem.h:26`), `SubscribeEvent(reg, [&](const YourEvent& e){...})` for immediate; for frame-bound, read `registry.ctx().get<FEventBus>().frameEvents<YourEvent>()` in `update`.

**Register an event system's wiring:** subscribe in `onRegister` so the callback is live before the first `update`.

## Cross-references

- [ARCHITECTURE.md](../ARCHITECTURE.md) — where the bus is created/advanced.
- [ecs](ecs.md) — systems subscribing/emitting.
- [debug-overlay](debug-overlay.md) — the UI events that drive combat.
- [tactical-combat](tactical-combat.md)
- [glossary](../glossary.md)
