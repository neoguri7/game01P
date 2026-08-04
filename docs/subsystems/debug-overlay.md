<!-- doc-verify subsystem=debug-overlay commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# debug-overlay

> `src/debug/` — the ImGui debug overlay, the demo-scene bootstrap, and the tactical-combat telemetry panel that issues UI commands.

## As-built

### DebugOverlay

`RenderDebugOverlay` (`src/debug/DebugOverlay.h:10`, defined at `src/debug/DebugOverlay.cpp:136`) is installed as the engine's overlay renderer from `src/main.cpp:17` and invoked every frame at `src/core/Engine.cpp:181`. It draws three ImGui windows:

| Window | Drawn at | Contents |
| --- | --- | --- |
| **Engine Stats** | `src/debug/DebugOverlay.cpp:90` | frame/system info |
| **Entity Inspector** | `src/debug/DebugOverlay.cpp:102` | per-entity component tree; **Add Demo Entity** button (`:122-123` → `CreateDebugDemoEntity`), **Clear All Entities** (`:127`) |
| **Tactical Combat** | via `RenderTacticalCombatTelemetryPanel` (`src/debug/DebugOverlay.cpp:139`) | command buttons + telemetry, see below |

### TacticalCombatTelemetryPanel

`RenderTacticalCombatTelemetryPanel` (`src/debug/TacticalCombatTelemetryPanel.cpp:130`) calls `ImGui::Begin("Tactical Combat")` (`:134`) and:
- reads the active unit (`registry.ctx().find<FActiveTacticalUnit>` … `FActiveTacticalUnit` helper) and telemetry `FTacticalCombatTelemetry` (`:131`);
- draws command buttons **Attack/Move/Dash/Dodge/End Turn** (`src/debug/TacticalCombatTelemetryPanel.cpp:84-106`) that build `FTacticalCommandRequestedEvent`s and push them into `registry.ctx().get<FTacticalCommandInputBridge>().requests` (`EnqueueUiCommand`, `:28-33`);
- renders the log scrollback `FTacticalCombatLog` (`:149`).

The panel reads combat tags directly — including `FCombatStateAwaitingCommand` (`src/debug/TacticalCombatTelemetryPanel.cpp:6`) — see [tactical-combat](tactical-combat.md).

### DemoBootstrap

`BootstrapDemoScene` (`src/debug/DemoBootstrap.cpp:9-12`) is called from `src/main.cpp:16`. It initializes combat config and creates the combat setup state:
- `FTacticalCombatConfigLoader::Initialize(registry)` — `src/debug/DemoBootstrap.cpp:10`
- `FTacticalCombatStateFactory::createSetupState(registry)` — `src/debug/DemoBootstrap.cpp:11`

`CreateDebugDemoEntity` (`src/debug/DemoBootstrap.cpp:14-16`) delegates to `FDemoEntityFactory::create` (used by the Entity Inspector button).

## Intended / In-progress

- [UNVERIFIED — src/debug/DemoBootstrap.cpp:5] `src/debug/DemoBootstrap.cpp` includes `gameplay/tactical_d20/FTacticalCombatConfigLoader.h` (`src/debug/DemoBootstrap.cpp:5`), which does **not** exist on disk — the combat-config loader was in the removed `gameplay/` tree. `BootstrapDemoScene` will not compile until [RECOVERY.md](../RECOVERY.md) restores it or the call is stubbed.
- [UNVERIFIED — src/debug/TacticalCombatTelemetryPanel.cpp:13] The telemetry panel is missing its seven `gameplay/tactical_d20/*` headers, included at `src/debug/TacticalCombatTelemetryPanel.cpp:13-19` (`FTacticalCommandInputBridge`, `FTacticalCombatConfig`, `FTacticalCombatLog`, `FTacticalCombatQueries`, `FTacticalCombatTelemetry`, `events/FTacticalCombatEvents`, `logging/FTacticalCombatLogUtils`). None exist on disk; the panel cannot compile today.

## Public API surface

| Symbol | Signature | Anchor |
| --- | --- | --- |
| `RenderDebugOverlay` | `void(registry&, const Time&, const SystemManager&)` | `src/debug/DebugOverlay.h:10` |
| `RenderTacticalCombatTelemetryPanel` | same shape | `src/debug/TacticalCombatTelemetryPanel.cpp:130` |
| `BootstrapDemoScene` | `void(registry&)` | `src/debug/DemoBootstrap.cpp:9` |
| `CreateDebugDemoEntity` | `entt::entity(registry&)` | `src/debug/DemoBootstrap.cpp:14` |

## How to extend

**Add a debug window:** in `RenderDebugOverlay` (`src/debug/DebugOverlay.cpp:136`), open an `ImGui::Begin("My Window")` … `ImGui::End()` block, or call a helper like `RenderTacticalCombatTelemetryPanel` (`:139`). Mirror the existing windows' use of `registry` / `systemManager` / `Time`.

**Add a command button:** in `src/debug/TacticalCombatTelemetryPanel.cpp:84-106`, add an `ImGui::Button` that builds the right `FTacticalCommandRequestedEvent` and calls `EnqueueUiCommand` (`:28`) — then ensure the matching `TacticalD20<Action>ActionResolutionSystem` handles it (see [tactical-combat](tactical-combat.md)).

## Cross-references

- [factories](factories.md) — demo-entity factory.
- [tactical-combat](tactical-combat.md) — the combat state/events the panel drives.
- [events](events.md) — the UI events / input bridge.
- [RECOVERY.md](../RECOVERY.md) — the missing `gameplay/` headers.
