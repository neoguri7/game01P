# Project Codemap

## Goal
<1-2 sentences: what this project is>
e.g. game01P: 2D turn-based roguelike built engine-free for the graduation capstone.

## Directory Tree
```
src/
  core/       # FGame, ResourceManager, GameState, SDLDeleter
  ecs/        # EnTT components and systems
  render/     # SDL3 rendering
  physics/    # Box2D v3 integration
  audio/      # miniaudio wrappers
  ui/         # Dear ImGui integration
assets/
tools/
tests/
```

## Core Entities
| Symbol | File | Role |
|---|---|---|
| FGame | src/core/FGame.cpp | Main loop, state machine |
| ResourceManager | src/core/ResourceManager.cpp | Asset lifetime, SDLDeleter RAII |
| GameState | src/core/GameState.h | State interface |
<!-- add more as the project grows -->

## Build & Test
- Configure: `cmake --preset windows-msvc`
- Build: `cmake --build --preset windows-msvc-debug`
- Test: `ctest --preset default`
- Run: `./build/windows-msvc/Debug/game01P.exe`

## External Dependencies
- SDL3 (vcpkg)
- EnTT
- Box2D v3
- GLM
- miniaudio
- Dear ImGui
- Toolchain: CMake + Ninja, MSVC x64, vcpkg

## Known Risks / Gotchas
<optional: platform quirks, past breakages, things to re-check>
