<!-- doc-verify subsystem=index commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# game01P Documentation Index

> The entry point for all documentation of `game01P`, a C++23 SDL3 + EnTT ECS tactical-combat roguelike (D&D 5.5e-inspired).

**Start here → [ONBOARDING.md](ONBOARDING.md)** if you are new to the project. Everything in this tree follows the rules in [DOC_RULES.md](DOC_RULES.md).

> **Important:** the working tree is mid-refactor and does **not** currently compile. See [RECOVERY.md](RECOVERY.md) before attempting a build. Subsystem docs that describe broken modules use the [UNVERIFIED] tag for recovered/intended behavior.

## Top-level docs

| Doc | Purpose |
| --- | --- |
| [DOC_RULES.md](DOC_RULES.md) | The documentation contract every file here obeys (rules R1–R10, template, enforcement). |
| [ONBOARDING.md](ONBOARDING.md) | First-day path: prerequisites, configure, build, run, where to read next. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | The big picture: layering, engine lifecycle, main loop, ctx services, ECS wiring. |
| [CONVENTIONS.md](CONVENTIONS.md) | Enforced coding conventions: naming, ECS/event/factory/state rules, dependency direction. |
| [BUILD_AND_TOOLING.md](BUILD_AND_TOOLING.md) | CMake/vcpkg presets per OS, clang-format/tidy/clangd, remote Windows build. |
| [RECOVERY.md](RECOVERY.md) | The broken-build state: what moved in the refactor, what still references old paths, and the restoration path. |
| [glossary.md](glossary.md) | The project vocabulary (registry, system, component, event bus, ...). |

## Subsystem docs

| Doc | Subsystem | Status |
| --- | --- | --- |
| [subsystems/core-engine.md](subsystems/core-engine.md) | `src/core/` — Engine, ctx services, lifecycle | As-built |
| [subsystems/ecs.md](subsystems/ecs.md) | `src/ecs/` — systems, components, SystemManager | As-built (combat systems missing) |
| [subsystems/events.md](subsystems/events.md) | `src/core/events/` — FEventBus dispatch | As-built |
| [subsystems/factories.md](subsystems/factories.md) | `src/core/factories/` — entity factories | As-built |
| [subsystems/tactical-combat.md](subsystems/tactical-combat.md) | Combat state machine + conditions + turn order | Intended (unbuilt) |
| [subsystems/app-flow-states.md](subsystems/app-flow-states.md) | `src/states/` — app-flow state machine | Dead/unwired |
| [subsystems/debug-overlay.md](subsystems/debug-overlay.md) | `src/debug/` — ImGui overlay, demo bootstrap | Partially broken |

## How to use this index

1. Start with [ONBOARDING.md](ONBOARDING.md) for the environment and build.
2. Read [ARCHITECTURE.md](ARCHITECTURE.md) once to understand how the pieces connect.
3. For a concrete task, open the subsystem doc that owns the code you will touch; each is decision-complete on its own (R9).
4. When your change lands, bump that doc's `doc-verify` header to the new HEAD (R4/R10).
