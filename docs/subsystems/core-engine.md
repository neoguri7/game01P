<!-- doc-verify subsystem=core-engine commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# core-engine

> `src/core/` — the `Engine`, the registry context services it owns, and the per-frame lifecycle. This is the spine everything else plugs into.

## As-built

### The Engine class

`game::Engine` (`src/core/Engine.h:22`) owns the SDL window/renderer, the single `entt::registry`, the `SystemManager`, a variable-step `Time`, and an optional ImGui overlay callback. Non-copyable (`src/core/Engine.h:26-28`). Public accessors expose the registry, renderer, and system manager (`src/core/Engine.h:35-39`).

Lifecycle: `initialize()` → `run()` → `shutdown()` (`src/core/Engine.h:30-32`). `initialize()` (`src/core/Engine.cpp:24`) inits SDL, creates window + renderer, sets up ImGui, calls `FLogger::Initialize` (`src/core/Engine.cpp:56`), then `initializeContextServices()` (`src/core/Engine.cpp:62`). `shutdown()` (`src/core/Engine.cpp:190`) clears the registry, tears down services (`shutdownContextServices`), ImGui, renderer, window, and calls `SDL_Quit`.

`setOverlayRenderer` (`src/core/Engine.h:33`) installs the debug overlay callback rendered at `src/core/Engine.cpp:181`.

### Per-frame loop

`Engine::run()` (`src/core/Engine.cpp:108`):

1. `frameTime.updateDeltaTime()` — variable-step dt — `src/core/Engine.cpp:112`
2. `bus->beginFrame()` — clears frame-bound events — `src/core/Engine.cpp:115`
3. `processInput()` — SDL events → `FInputState`, ImGui events — `src/core/Engine.cpp:126`
4. `update(dt)` — `systemMgr.updateAll` + audio GC — `src/core/Engine.cpp:150-165`
5. `render()` — ImGui frames, clear, `systemMgr.renderAll`, overlay, present — `src/core/Engine.cpp:166-188`

`processInput` resets per-frame input state using ImGui's capture flags and feeds SDL events (`src/core/Engine.cpp:127-148`). `update` runs systems then `audio->gcOneShots()` (`src/core/Engine.cpp:154-157`). `render` draws game systems after clear and before the overlay (`src/core/Engine.cpp:177-181`).

### Registry context services

All created in `initializeContextServices()` (`src/core/Engine.cpp:72`), in dependency order, and torn down in reverse in `shutdownContextServices()` (`src/core/Engine.cpp:88`).

| Service | Type | Init anchor | Notes |
| --- | --- | --- | --- |
| `FLogger` | sph spdlog + shared str | `src/core/Logger.h:19` | `struct FLogger` (`src/core/Logger.h:16`); expose via ctx, file log optional. |
| `FInputState` | frame input | `src/core/InputState.h:85` | `struct FInputState` (`src/core/InputState.h:25`), `enum EInputAction` (`src/core/InputState.h:10`); `beginFrame`/`processEvent`. |
| `FAudioManager` | audio | `src/core/AudioManager.h:17` | `struct FAudioManager` (`src/core/AudioManager.h:16`); `Shutdown` (`:32`), `gcOneShots` (`:81`). |
| `FEventBus` | event bus | `src/core/events/FEventBus.h:89` | See [events](events.md). |
| `FAssetManager` | asset identity | `src/core/AssetManager.h:10` | `struct FAssetManager` (`:9`); placeholder registry marker. |
| `SDL_Renderer*` | raw pointer | `src/core/Engine.cpp:81` | Direct renderer access from ctx. |
| `FResourceManager` | texture cache | `src/core/ResourceManager.h:8` | `init` (`:14`), `tryLoadTexture` (`:17`), `clear` (`:21`); renderer-backed. |

Teardown order mirrors dependencies: `FResourceManager` cleared first, then renderer ctx removed, then remaining services (`src/core/Engine.cpp:88-106`). `FLogger` is shut down separately in `Engine::shutdown` (`src/core/Engine.cpp:199`).

### Time

`game::Time` (`src/core/Time.h:14`) is an Engine member (not a ctx service). `frameTime.updateDeltaTime()` returns a variable-step dt with the frame-time clamping done inside (`src/core/Engine.cpp:112`). It is passed to systems and to the overlay renderer.

### RAII SDL pointers

`FWindowPtr`/`FRendererPtr` (`src/core/SDLDeleter.h:6`,`:11`) and `FTextureDeleter` (`src/core/SDLDeleter.h:16`) use custom deleters so SDL objects self-release.

## Intended / In-progress

- [UNVERIFIED — src/core/Engine.cpp:160] `Engine::update` contains `// TODO: optional state machine update` (`src/core/Engine.cpp:160`) — the app-flow `GameStateMachine` (see [app-flow-states](app-flow-states.md)) is intended to run here but is not invoked today.

## Public API surface (Engine)

| Symbol | Signature | Anchor |
| --- | --- | --- |
| `Engine::initialize` | `bool(title, w, h)` | `src/core/Engine.h:30` |
| `Engine::run` | `void()` | `src/core/Engine.h:31` |
| `Engine::shutdown` | `void()` | `src/core/Engine.h:32` |
| `Engine::setOverlayRenderer` | `void(fn)` | `src/core/Engine.h:33` |
| `Engine::getRegistry` | `entt::registry&` | `src/core/Engine.h:36` |
| `Engine::getRenderer` | `SDL_Renderer*` | `src/core/Engine.h:38` |
| `Engine::getSystemManager` | `SystemManager&` | `src/core/Engine.h:39` |

## How to extend

**Add a new context service:**
1. In `initializeContextServices()` (`src/core/Engine.cpp:72`), emplace it after its dependencies (renderer-backed services go after the `SDL_Renderer*` at `src/core/Engine.cpp:81`).
2. In `shutdownContextServices()` (`src/core/Engine.cpp:88`), erase it in reverse order (features it uses go first).
3. Fetch it in systems with `registry.ctx().find<T>()` — see [ecs](ecs.md) and [glossary](glossary.md).

**Register a new system:** see [ecs](ecs.md) (this is the owner of the `SystemManager`).

## Cross-references

- [ARCHITECTURE.md](../ARCHITECTURE.md) — engine lifecycle + main loop diagram.
- [events](events.md) — the `FEventBus` service.
- [ecs](ecs.md) — the `SystemManager` owner.
- [glossary](../glossary.md)
