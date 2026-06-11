# Step 01 Implementation: Engine Ability/Effect Foundation

## Status

Implemented as a small core foundation for engine-owned lifecycle, input, window,
and render transitions. The implementation deliberately avoids a generic ability
dispatcher; each pipeline stays phase-local and uses typed C++ state/events.

## Files

| File | Purpose |
|---|---|
| `src/core/EngineAbility.h` | Engine-prefixed vocabulary for actions, abilities, conditions, effects, checks, and effect results. |
| `src/core/EngineState.h` | Engine tags plus ctx state for runtime readiness, frame activity, and window facts. |
| `src/core/events/EngineEvents.h` | Typed engine cue/event structs for meaningful transitions. |
| `src/core/EngineAbilityPipeline.h/.cpp` | Phase-local helpers that apply named engine effects and publish cues. |
| `src/core/Engine.cpp` | Wires the foundation into initialize, run, input, render, and shutdown without changing the public engine API. |

## Vocabulary Mapping

| Step 01 term | Implemented as | Notes |
|---|---|---|
| `EngineAction` | `FEngineAction` / `FEngineAbilityRequest` alias | Per-frame request to activate an engine ability. |
| `EngineAbility` | `EEngineAbility` | Engine capabilities such as process input, apply window event, begin render frame. |
| `EngineCondition` | `EEngineCondition`, `FEngineAbilityCheck` | Guard facts used by phase helpers before effects run. |
| `EngineTag` | `EEngineTag`, `FEngineTagSet` | Durable/frame-bound state labels in `FEngineRuntimeState`. |
| `EngineEffect` | `EEngineEffect`, `FEngineEffectResult` | Named mutation or low-level operation result. |
| `EngineState` | `FEngineRuntimeState`, `FEngineFrameState`, `FEngineWindowState` | Stored in `registry.ctx()` and owned by `Engine`. |
| `EngineCue` / `EngineEvent` | `FEngine*Event` structs | Typed events published/queued through `FEventBus`. |

## Runtime Flow

1. `Engine::initialize` creates SDL/ImGui/services, registers engine ctx state,
   and marks `EEngineTag::Running` when the run loop is ready.
2. Each loop starts with `BeginEngineFrameAbility`, which advances
   `FEngineFrameState::frameIndex` and sets `FrameActive`.
3. `processInput` uses `BeginEngineInputAbility`, forwards SDL events to ImGui,
   keeps `FInputState` compatibility, and routes quit/window events through the
   engine event/state path.
4. `render` checks render conditions, applies named render effects in the
   existing order, and emits coarse render cues.
5. `shutdown` clears runtime tags/state before context services and SDL handles
   are destroyed.

## Extension Rules

- Add new engine abilities/effects only for meaningful engine transitions, not
  for every SDL call, key event, or draw call.
- Keep gameplay concepts out of `core/`; gameplay may subscribe to engine cues,
  but core must not include gameplay headers.
- Prefer typed `FEngine*Event` cues through `FEventBus`; do not add stringly
  event names or a scripting-style dispatcher.
- Keep state in `registry.ctx()` only when it replaces repeated implicit checks
  or gives other systems an observable engine fact.
- Preserve existing order: ImGui input first, update systems before render,
  world render before overlay/ImGui/present.

## Verification

- Build: `cmake --build out/build/mac-debug` per project context, or the local
  configured build directory/preset when working on Windows.
- Manual checks after build: launch, close via window quit, press mapped input,
  interact with ImGui capture, confirm sprites/overlay still render.
