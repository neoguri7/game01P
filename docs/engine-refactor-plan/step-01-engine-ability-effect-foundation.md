# Step 01: Engine Ability, Effect, Tag, And Cue Foundation

## Implementation Note

- Implementation documentation: `docs/engine-refactor-plan/step-01-implementation.md`.
- Runtime foundation files: `src/core/EngineAbility.h`, `src/core/EngineState.h`,
  `src/core/events/EngineEvents.h`, and `src/core/EngineAbilityPipeline.*`.
- The implementation keeps the Step 01 guardrail: no generic engine ability
  dispatcher and no gameplay concepts inside `core/`.

## Orchestrator Route

- Task type: cross-cutting core engine refactor plan.
- Original planning workflow: context map -> semantic contract -> plan critique -> test oracle -> mechanical checklist.
- Implementation status: Step 01 runtime foundation is implemented; the original
  planning-only stop condition is superseded by the implementation note above.

## Context Map

### Target

- Apply the GAS-shaped pipeline to low-level engine concerns with Engine-prefixed terms:
  `EngineAction/EngineAbility -> EngineCondition/EngineTag -> EngineEffect -> EngineAttribute/EngineState -> EngineEvent/EngineCue`.
- Scope is window, input, and render lifecycle. Gameplay concepts must not enter `core/`.

### Relevant Files And Symbols

| File | Symbol/Area | Why It Matters | Evidence |
|---|---|---|---|
| `src/core/Engine.h` | `Engine` | Owns SDL window/renderer, registry, frame time, system manager, and private `processInput/update/render` phases. | Lines 15-58 |
| `src/core/Engine.cpp` | `initialize`, `run`, `processInput`, `render`, `shutdown` | Current engine flow is direct procedural SDL/ImGui/state mutation. | Lines 24-214 |
| `src/core/InputState.h` | `FInputState` | Input state, SDL event translation, UI capture guard, and action mapping live in one ctx service. | Lines 20-104 |
| `src/core/SystemManager.h` | `updateAll`, `renderAll` | Update/render phase ordering already exists and should be preserved. | Lines 44-55 |
| `src/ecs/systems/ISystem.h` | `ISystem::update/render` | ECS systems already expose separate update and render hooks. | Lines 20-30 |
| `src/core/events/FEventBus.h` | `FEventBus` | Existing event bus can carry EngineCue/EngineEvent notifications. | Lines 23-87 |
| `src/core/events/FEventPublishing.h` | typed helpers | Preferred event helper layer for new code. | Lines 11-46 |
| `src/ecs/systems/SpriteRenderSystem.h` | renderer ctx usage | Rendering systems currently pull `SDL_Renderer*` from registry ctx. | Lines 21-58 |
| `src/main.cpp` | startup order | Core ECS systems are registered after `Engine::initialize`; no gameplay scene is bootstrapped by default. | Lines 9-20 |

### Current Behavior

- `Engine::initialize` creates SDL window/renderer, initializes ImGui, logger, ctx services, then sets `running = true`.
- `Engine::run` updates `Time`, clears frame events, then calls `processInput`, `update`, and `render` in order.
- `Engine::processInput` directly calls ImGui SDL processing, mutates `FInputState`, and sets `running = false` on SDL quit.
- `Engine::render` directly begins ImGui, clears the renderer, calls render systems, runs the overlay callback, renders ImGui, and presents.
- There is no explicit engine-level request/condition/effect/cue vocabulary yet.

### Tests And Checks

- Existing checks: preferred focused build is `cmake --build out/build/mac-debug`.
- Missing checks: no `tests/` directory was present during planning; manual launch/quit/render checks will be needed after implementation.
- Current risk: `src/gameplay` is empty by design. Confirm local generated/untracked files or expected build state before using build failures as refactor evidence.

### Constraints

- Dependency direction remains `core/ <- ecs/ <- gameplay/`.
- Core engine terms may use GAS-shaped names only with an `Engine` prefix.
- Do not wrap every SDL call, draw call, or key event. Use the pattern only for meaningful state transitions.
- No new dependency.
- Preserve current public behavior before adding new engine behavior.

## Semantic Contract

### Intent

- Make low-level engine lifecycle changes explicit, inspectable, and event-visible without turning `Engine` into a gameplay system or a giant abstraction.

### Terminology Contract

- `EngineAbility`: an engine capability that can be requested and executed, such as poll input, apply window event, begin render frame, or present frame.
- `EngineAction`: a request to perform an `EngineAbility` in the current phase.
- `EngineCondition`: a guard checked before an ability/effect, such as renderer ready, window alive, input not UI-captured, or frame active.
- `EngineTag`: a durable or frame-bound capability/state label, such as `Engine.Window.Open`, `Engine.Renderer.Ready`, `Engine.Input.UiCaptured`, or `Engine.Window.Minimized`.
- `EngineEffect`: a single engine mutation or low-level operation, such as update input state, set running false, clear backbuffer, update window size, or present frame.
- `EngineAttribute` / `EngineState`: engine-owned state facts in `Engine` or `registry.ctx()`, such as window size, frame index, capture flags, renderer readiness, and running state.
- `EngineCue` / `EngineEvent`: event bus notification emitted after meaningful effects, such as quit requested, window resized, input frame begun, render frame presented, or render frame skipped.

### Behavior Contract

- Every meaningful engine transition follows: request -> condition/tag check -> effect application -> state update -> cue/event emission.
- The pattern must remain phase-local: input pipeline owns input effects, window pipeline owns window effects, render pipeline owns render effects.
- SDL and ImGui calls remain allowed inside the relevant EngineEffect implementation boundary.
- Existing ECS `SystemManager` update/render order must remain compatible.
- Engine events must be typed C++ events, not stringly runtime scripting.

### Data / State / Ownership

- `Engine` continues to own SDL window/renderer handles during the first refactor wave.
- `registry.ctx()` remains the service boundary for input state, event bus, resource manager, and future engine state services.
- Engine state services may be added only when they replace repeated implicit state checks or provide event-visible state.
- Gameplay systems may consume engine cues but core must not include gameplay headers.

### Edge Cases

- Missing `FEventBus` -> engine still runs with logging or silent no-op event publication.
- Missing `FInputState` -> input frame effects skip game input mutation but SDL quit still works.
- Renderer unavailable -> render ability rejects/skips without calling render systems.
- Minimized or hidden window -> render frame may skip present if implementation defines that policy.

### Risks

- Overbuilding a full GAS clone for engine internals.
- Introducing a central `EngineAbilitySystem` that becomes a new god object.
- Emitting too many events and making normal frame processing noisy.
- Reordering ImGui/input/render calls and causing subtle UI or input regressions.

## Plan Critique

- Verdict: SPLIT.
- Start with small engine pipelines for input, window/lifecycle, and render frame.
- Do not include resource/audio in the first wave; they are valid later candidates after the core vocabulary proves useful.
- Rollback path: each pipeline extraction should be removable without changing ECS gameplay systems.

## Test Oracle

| Contract Item | Evidence Type | Check/Test | Expected Result |
|---|---|---|---|
| Build compatibility | build | `cmake --build out/build/mac-debug` after implementation | No compile regressions from engine refactor. |
| Startup/shutdown | manual/build | Launch, close via window quit, call shutdown path | App exits cleanly; no double shutdown crash. |
| Input compatibility | manual/debug | WASD/confirm/cancel/debug toggle where existing consumers use `FInputState` | Existing input behavior remains intact. |
| UI capture | manual/debug | Interact with ImGui and game input in same frame | UI capture still suppresses gameplay actions. |
| Render order | manual/visual | Start app with sprites/debug overlay | Clear -> world render -> overlay -> ImGui -> present order remains. |
| Event visibility | unit/manual | Subscribe/debug-log selected EngineCue events | Only meaningful state transitions emit cues. |

## Mechanical Checklist

### Scope

- Files to edit later: `src/core/Engine.*`, `src/core/InputState.h`, new engine event/state/helper files under `src/core/`, possibly `src/core/events/`.
- Files to read only first: `src/main.cpp`, `src/core/SystemManager.h`, `src/ecs/systems/ISystem.h`, `src/ecs/systems/SpriteRenderSystem.h`, debug overlay files.
- Do not change: gameplay behavior, ECS component rules, external dependencies, full renderer backend, or ImGui backend files.

### Steps

1. Define the Engine-prefixed vocabulary and keep it documented near the new core event/state types.
2. Introduce typed EngineCue event structs for only meaningful transitions.
3. Refactor input into an engine input pipeline while preserving `FInputState` consumers.
4. Refactor window/lifecycle events into explicit EngineEffects and EngineCues.
5. Refactor render frame order into named EngineEffects with unchanged order.
6. Re-run focused checks and manually verify startup/input/render/quit.

### Worker Slices

| Slice | Owner Agent | Write Scope | Dependencies |
|---|---|---|---|
| Engine terminology/events | `engine-architect` / `game-programmer` | `src/core/events/`, docs | Approved Step 01 contract. |
| Input pipeline | `game-programmer` | `Engine.cpp`, `InputState.h`, new core helpers | Step 02 checklist. |
| Window lifecycle pipeline | `game-programmer` | `Engine.cpp`, new core events/state | Step 03 checklist. |
| Render frame pipeline | `game-programmer` | `Engine.cpp`, optional render state/events | Step 04 checklist. |

### Stop Conditions

- Ask user before renaming public types such as `FInputState` or changing renderer ownership.
- Return to semantic planning if an implementation requires a central generic ability dispatcher.
- Return to mechanical planning if build failures come from unrelated missing gameplay files.

### Completion Criteria

- A future implementer can refactor engine input/window/render flow using Engine-prefixed GAS terms without importing gameplay concepts into `core/`.
