<!-- doc-verify subsystem=onboarding commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# Onboarding

> The first-day path for a new engineer: get the environment ready, hit the broken-build gate, and start reading the right docs to become productive.

> **Read this note first.** At HEAD the repository does **not** compile — `src/main.cpp:4` includes a missing `gameplay/SystemRegistration.h` [UNVERIFIED]. Do **not** run a full build before reading [RECOVERY.md](RECOVERY.md); that document explains the mid-refactor state and the two ways to get to a compiling tree. Everything below assumes you return here after recovery.

## 1. Clone and prerequisites

```bash
git clone <your-fork-or-remote> game01P
cd game01P
```

Install the toolchain from [BUILD_AND_TOOLING.md](BUILD_AND_TOOLING.md) prerequisites:

- **vcpkg** — clone vcpkg and export `VCPKG_ROOT` to its root.
- **CMake ≥ 3.21**.
- **Ninja** (macOS / Linux presets).
- **Compiler** — Xcode `clang` on macOS, `clang` on Linux, VS 2026 / MSVC on Windows.

First build will compile vcpkg dependencies (SDL3, EnTT, imgui, spdlog, ...) which can take several minutes.

## 2. Configure

Set `VCPKG_ROOT`, then:

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset macos-clang-debug     # macOS
# cmake --preset linux-clang-debug   # or Linux
# cmake --preset windows-msvc-debug  # or Windows
```

This also emits `out/build/<preset>/compile_commands.json` for `clangd` (see [BUILD_AND_TOOLING.md](BUILD_AND_TOOLING.md) #IDE / Lint tooling).

## 3. Build — STOP and read RECOVERY first

> **Build is broken.** `src/main.cpp:4` includes `gameplay/SystemRegistration.h` [UNVERIFIED], which does not exist in the current refactor; `src/debug/*` also reference removed `gameplay/tactical_d20/*` headers [UNVERIFIED]. Running `cmake --build` now fails at compile time.

Go to **[RECOVERY.md](RECOVERY.md)** now. It describes:
- what commit `b048ebd` moved and what still references the old paths,
- the recommended restoration path (restate `gameplay/SystemRegistration.h` + the `tactical_d20/` tree, or stub them to register only the base systems),
- follow-up to promote the `[UNVERIFIED]` combat claims to as-built.

Once the tree is restored, build and run:

```bash
cmake --build out/build/macos-clang-debug
out/build/macos-clang-debug/game01P
```

(Executable name/path differs on Windows: `out/build/windows-msvc-debug/Debug/game01P.exe`.) You should see the SDL window with the ImGui debug overlay: **Engine Stats**, **Entity Inspector**, and the **Tactical Combat** panel.

## 4. Where to read next

Read in this order:

1. [glossary.md](glossary.md) — vocabulary (registry, system, component, tag, event, factory).
2. [ARCHITECTURE.md](ARCHITECTURE.md) — how the engine, ECS, events, and services connect.
3. [CONVENTIONS.md](CONVENTIONS.md) — the rules you must follow when writing code.
4. The subsystem doc for whatever you're about to touch — each is decision-complete on its own (R9 in [DOC_RULES.md](DOC_RULES.md)).

## 5. Your first contribution

Pick the subsystem from the [index](README.md#subsystem-docs). Open its doc's **How to extend** section. Common starting tasks:

- Add a new **system** → [subsystems/ecs.md](subsystems/ecs.md).
- Add a new **component** (data or tag) → [subsystems/ecs.md](subsystems/ecs.md).
- Add a new **event + wiring** → [subsystems/events.md](subsystems/events.md).
- Add a new **entity factory** → [subsystems/factories.md](subsystems/factories.md).

After your change compiles, run the doc checks before committing:

```bash
bash scripts/check-docs.sh
```

and bump the `doc-verify` header of the doc(s) you touched (R4/R10 in [DOC_RULES.md](DOC_RULES.md)).

## Cross-references

- [BUILD_AND_TOOLING.md](BUILD_AND_TOOLING.md) — everything about building/tooling.
- [RECOVERY.md](RECOVERY.md) — the broken-build gate.
- [ARCHITECTURE.md](ARCHITECTURE.md)
- [glossary.md](glossary.md)
- [README.md](README.md)
