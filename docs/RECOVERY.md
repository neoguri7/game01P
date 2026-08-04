<!-- doc-verify subsystem=recovery commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# Recovery (broken-build state)

> The current tree does **not** compile. This documents exactly what broke, where the recoverable source lives, and the two paths to a compiling tree — so a contributor (or a small AI agent) can restore it precisely.

## What broke

Commit `b048ebd` ("Refactor Tactical D20 Systems and Components") removed the `src/gameplay/` layer (74 files, including the combat `tactical_d20/` systems) and moved tactical factories/components into `src/core/factories/` and `src/ecs/components/`. The callers were **not** fully updated, so the tree no longer builds:

- `src/main.cpp:4` includes `gameplay/SystemRegistration.h` — [UNVERIFIED] missing on disk.
- `src/debug/DemoBootstrap.cpp:5` includes `gameplay/tactical_d20/FTacticalCombatConfigLoader.h` — [UNVERIFIED] missing.
- `src/debug/TacticalCombatTelemetryPanel.cpp:13-19` includes seven `gameplay/tactical_d20/*` headers — [UNVERIFIED] missing: `FTacticalCommandInputBridge`, `FTacticalCombatConfig`, `FTacticalCombatLog`, `FTacticalCombatQueries`, `FTacticalCombatTelemetry`, `events/FTacticalCombatEvents`, `logging/FTacticalCombatLogUtils`.

Because CMake globs `src/` (`CMakeLists.txt:15-19`), these non-existent includes fail at compile time as soon as the source is scanned.

## Recoverable source

The full deleted layer is recoverable from the parent commit:

- `git show b048ebd~1:src/gameplay/` — [UNVERIFIED] the complete tree (74 files): `SystemRegistration.{h,cpp}` and `tactical_d20/` with its `config/`, `events/`, `rules/`, `actions/`, `systems/{flow,ai,actions,input,presentation}/` subfolders.
- `out/build/macos-clang-debug/CMakeFiles/game01P.dir/src/gameplay/tactical_d20/` — [UNVERIFIED] stale `.o` files confirming the intended translation units (see [tactical-combat](tactical-combat.md) for the recovered system list).

**Naming mismatch (the crux):** the recovered `b048ebd~1` gameplay layer uses `FTacticalD20*` names (e.g. `FTacticalD20ConfigLoader`), but the current callers reference **no-D20** names (`FTacticalCombatConfigLoader`, `FTacticalCommandInputBridge`, `FTacticalCombatTelemetry`, `FTacticalCombatLog`, …). A plain `git checkout b048ebd~1 -- src/gameplay` will restore the files but will **not** satisfy the current includes — the renames must be reconciled by hand or the callers updated. (This is the user's work-in-progress; do not guess intent.)

## Path A — Restore + rename-reconcile (complete combat, more effort)

1. Restore the layer: `git checkout b048ebd~1 -- src/gameplay`
2. Reconcile the rename: update `tactical_d20/` symbols to the no-D20 names the callers expect (`FTacticalCombatConfigLoader`, `FTacticalCommandInputBridge`, `FTacticalCombatTelemetry`, `FTacticalCombatLog`, `FTacticalCombatQueries`, and the `events/`/`logging/` headers in `src/debug/TacticalCombatTelemetryPanel.cpp:13-19`), or revert those callers to the old D20 names.
3. Re-register: confirm `gameplay/SystemRegistration` calls the restored `tactical_d20` registration (recovered name `TacticalD20SystemRegistration`) and that `src/main.cpp:15` links.
4. Restore the data contract: (see Path A note 4 below).

**Tradeoff:** faithful, complete combat, fully restores the transition driving documented in [tactical-combat](tactical-combat.md). Most code churn + rename risk.

## Path B — Stub minimal gameplay (fastest compile; defers combat)

1. Create `src/gameplay/SystemRegistration.h` + `.cpp` [UNVERIFIED] declaring `game::RegisterDefaultSystems(systemManager, registry)` that registers **only the 4 present base systems** ([ecs](ecs.md): `SpriteRenderSystem`, `CollisionSystem`, `AnimationSystem`, `MoveSystem`) and calls `systemMgr.onAllSystemsRegistered(registry)`. This satisfies `src/main.cpp:4` / `:15` with no `tactical_d20/` dependency.
2. Neutralize the debug-panel references: guard `BootstrapDemoScene` and the `TacticalCombatTelemetryPanel` `#include`/use of the seven missing headers — e.g. `#if` them out or drop the tactical panel until combat is restored. The rest of [debug-overlay](debug-overlay.md) (Engine Stats + Entity Inspector) keeps working.
3. Do **not** touch combat state tags yet — they stay dormant (see [tactical-combat](tactical-combat.md)), which is fine because no system advances them.
4. `assets/data/` (the data-driven config dir) is also empty/missing — either create defaults or leave it; the config loader expects it (Path A restores the loader that reads it).

**Tradeoff:** compiles fast, unblocks [ONBOARDING.md](ONBOARDING.md) run-through, but combat and the telemetry panel stay `[UNVERIFIED]`/disabled until [tactical-combat](tactical-combat.md) is built out.

## After either path

- Re-verify every `[UNVERIFIED]` claim in [tactical-combat](tactical-combat.md) and [debug-overlay](debug-overlay.md) against the now-real `gameplay/` code and **promote** them to As-built (remove the `[UNVERIFIED]` tag, add real anchors).
- Confirm a clean `cmake --preset <host>-debug` + build + run sees the window and the ImGui overlay.
- If the graph/systems materialized differently than the transition table in [tactical-combat](tactical-combat.md), update that doc (R2/R9).

## Also missing

- `DEV_WORKFLOW.md` — [UNVERIFIED] linked from `README.md:7`; describes the macOS-authoring / Windows-verify flow summarized in [BUILD_AND_TOOLING.md](BUILD_AND_TOOLING.md). Create it during recovery.
- `assets/data/` — the intended data-driven config dir never landed (see [CONVENTIONS.md](CONVENTIONS.md) data-driven section).

## Cross-references

- [ARCHITECTURE.md](../ARCHITECTURE.md) — registration + layering.
- [tactical-combat](tactical-combat.md) — the intended combat design to verify/reconcile.
- [debug-overlay](debug-overlay.md) — the code that references the missing headers.
- [ecs](ecs.md) — the 4 base systems Path B registers.
- [BUILD_AND_TOOLING.md](BUILD_AND_TOOLING.md) — presets/commands.
- [ONBOARDING.md](ONBOARDING.md) — the build gate that points here.
