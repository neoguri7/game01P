<!-- doc-verify subsystem=build commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# Build & Tooling

> How to configure, build, and run `game01P`, plus the lint/format/IDE tooling. Commands here are runnable verbatim (R8).

> **As-built warning:** the tree currently does **not** compile — `src/main.cpp` includes a missing `gameplay/SystemRegistration.h` [UNVERIFIED]. Before your first build, read [RECOVERY.md](RECOVERY.md). The commands here are what *will* work once the tree is restored.

## Prerequisites

- **vcpkg** — required by every preset (toolchain at `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`, `CMakePresets.json:11-14`). Set `VCPKG_ROOT` to your vcpkg checkout.
- **CMake ≥ 3.21** — `cmake_minimum_required` (`CMakeLists.txt:3`).
- **Ninja** — generator for `linux-clang-*` and `macos-clang-*` presets (`CMakePresets.json:67`, `:99`).
- **Compiler** — MSVC 2026 for Windows; `clang`/`clang++` for Linux (x64-linux) and macOS (`CMakePresets.json:64-109`).

Dependencies are declared in `find_package`/`find_path` (`CMakeLists.txt:31-39`): SDL3, EnTT, glm, SDL3_image, imgui, Tracy, miniaudio (via `find_path MINIAUDIO_INCLUDE_DIRS`, `CMakeLists.txt:37`), SDL3_ttf, spdlog. They are linked at `CMakeLists.txt:43-53`.

## Configure + build (as-built intended commands)

The project is compiled as a single executable from `GLOB_RECURSE` over `src/` (`.cpp`/`.h`/`.hpp`) with `CONFIGURE_DEPENDS` so added/removed files trigger a reconfigure (`CMakeLists.txt:15-19`). Include dir is `src` (`CMakeLists.txt:25`). C++23, no extensions (`CMakeLists.txt:55-58`).

For each supported host, configure then build:

```bash
# macOS (Clang, Ninja)
cmake --preset macos-clang-debug
cmake --build out/build/macos-clang-debug

# Linux (Clang, Ninja)
cmake --preset linux-clang-debug
cmake --build out/build/linux-clang-debug

# Windows (MSVC, VS 2026)
cmake --preset windows-msvc-debug
cmake --build out/build/windows-msvc-debug
```

Binary output: `out/build/<preset>/game01P` (macOS/Linux), `out/build/<preset>/Debug/game01P.exe` (Windows MSVC). Release presets (`*-release`) mirror these.

Preset index: `windows-msvc-debug/release` (`CMakePresets.json:36`,`:41`), `linux-clang-debug/release` (`:80`,`:88`), `macos-clang-debug/release` (`:111`,`:119`), plus legacy `x64-debug`/`x64-release`/`x86-debug` aliases (`:127-140`).

## Config flags

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set for all presets (`CMakePresets.json:10`), producing `out/build/<preset>/compile_commands.json` for `clangd` (see `.clangd:3-4`).

## IDE / Lint tooling

| Tool | Config | Role |
| --- | --- | --- |
| `clangd` | `.clangd` | Language server; points at `out/build` compile DB with fallback `-std=c++23 -Isrc -Wall -Wextra` (`.clangd:6-13`); enforces identifier-naming (`.clangd:21-25`). |
| `clang-format` | `.clang-format` | Formatting. Run `clang-format -i <file>` before committing. |
| `clang-tidy` | `.clang-tidy` | Static analysis (~150 lines), surfaced through clangd diagnostics (`.clangd:18-26`). |
| `.editorconfig` | `.editorconfig` | Base editor whitespace/encoding. |
| `compile_commands.json` | symlinked to root | `ln -sf out/build/<preset>/compile_commands.json compile_commands.json` (`.clangd:3-4`). |

## Cross-platform workflow

The team convention (per `README.md:5`): use macOS/WSL for writing code and light checks; treat **native Windows as the canonical build/verify environment**.

Windows remote-build helper: `scripts/remote-win-build.sh` (see `README.md:9-12`). It SSHs to a Windows host and runs the configured preset there; defaults are environment-variable driven (`scripts/remote-win-build.sh:4-13`).

**Checkout sync rule** (`README.md:15-17`): the macOS/Linux checkout and the Windows checkout are separate working folders — commit/push/pull between them before crossing, rather than sharing files.

## Missing docs

- `README.md` links to `DEV_WORKFLOW.md` (`README.md:7`), which does **not** exist on disk. [UNVERIFIED] The intended contents (per the README) describe the macOS-as-authoring / Windows-as-verify workflow summarized above. Create it as part of the recovery effort in [RECOVERY.md](RECOVERY.md).

## Cross-references

- [ONBOARDING.md](ONBOARDING.md)
- [ARCHITECTURE.md](ARCHITECTURE.md)
- [CONVENTIONS.md](CONVENTIONS.md)
- [RECOVERY.md](RECOVERY.md)
