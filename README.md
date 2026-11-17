# Ember Dungeon / Game01P

A playable SDL3 + EnTT turn-based grid-tactics roguelike prototype.

## Game mode

The executable runs **Ember Dungeon** (controls and loop below); it has no other
mode. The removed forest prototype mode's switches (`--forest`, `--verify-prototype`,
`$env:GAME01P_MODE = "forest"`) print `forest mode was removed` and exit with
code 2. `--ember` is still accepted and changes nothing.

Ember runs on one turn scheduler, value-token action queue, AP/status pipeline,
collision/pathfinding and wall LOS rules, with path traps, rune transactions and
persistent dungeon checkpoints; there is no second combat scheduler or resolver.

## Play

On Windows, with CMake, Visual Studio 2026 and `VCPKG_ROOT` configured:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
.\out\build\windows-msvc-debug\Debug\game01P.exe
```

The executable loads its adjacent `assets` directory, so the build folder can
be moved together with its DLLs and assets. CI packages all three.

| Control | Action |
|---|---|
| Click an empty cyan cell | Move the active adventurer up to three orthogonal cells |
| WASD | Move one cell; key repeat does not spend extra turns |
| 1-4 or skill list | Choose a skill |
| Click a unit or a valid target button | Use the selected skill |
| Space or End turn | Finish the current adventurer's turn |
| Escape | Confirm abandoning the expedition |
| Backtick | Toggle the developer overlay |

Blue marks party units, red enemies, yellow the active unit/hover, and cyan
reachable cells. Spikes damage units **along the entire traversed path**.
Walls and living units block movement; walls also block skill line of sight.
KO units no longer block cells or take turns. Invalid actions are reported in
the battle journal and do not spend AP.

## Prototype loop

Lead a warrior, archer, mage and healer through three procedurally generated 12×12 combat rooms (the party starts at the bottom, a random enemy formation at the top, with a few random wall clusters and spike traps that never cut anyone off; the map is open, with no wall ring around the edge),
a shrine/cache decision after the first room, and a guardian in the third.
Initiative uses the existing speed scheduler. Each turn has separate movement
AP and skill AP; neither can substitute for the other.

Room clears grant run EXP, a growth-node choice, reinforcement materials and a
rune. HP, nodes and loadouts carry between rooms. At camp, freely fit/remove
collected runes subject to the existing socket, duplicate and tag rules; spend
materials on stat reinforcement or descend without spending them. Dungeons
continue with stronger enemies rather than ending the run at a fixed depth.
Rune changes are camp-only gameplay operations, not UI mutations. Removing a
rune that another fitted rune requires is rejected; remove the dependent rune
first. Rejected edits leave both the loadout and collected inventory unchanged.

Clearing a dungeon saves its checkpoint in SDL's user preferences directory
(`Game01P/EmberDungeon/checkpoint.json`). A new run may start at the next
unlocked dungeon, or start fresh at dungeon 1. Defeat/abandonment resets run
EXP, stat growth, materials and runes, not cleared checkpoints. KO is temporary;
room-clear recovery restores knocked-out members without permanent injuries.

This slice generates rooms from the run seed (`gameplay/dungeon/FRoomGenerator`, tuned by the `generation` block of `assets/data/dungeon.json`), has a minimal
party-wide growth choice, static unit sprites, and checkpoint persistence.
It does not implement the full planned individual node tree, expanded theme
content, multi-part bosses, character animations, or saving an in-progress run.
Ending an expedition at camp keeps the checkpoint, **not** a suspended build.

## Art

[`style.md`](style.md) is the user-provided style-v3 source of truth.
`assets/art` contains native 64×64 PixelLab MCP outputs, sampled with nearest
neighbor at runtime. No existing PNG was used as a reference. The manifest
records tool/job/seed provenance and rejected candidates. Family and repeated
tile previews were checked before integration; these remain prototype art,
not automatically approved style-reference images. The theme is **dark fantasy forest** with a Diablo-style HUD; fonts (Cinzel, Alegreya Sans, SIL OFL) live in `assets/fonts` and the shared look is implemented in `presentation/FUiTheme`.

New art is meant to go through [`asset-pipeline/`](asset-pipeline/README.md): an agent-driven pipeline (PixelLab + Layer
MCP) with a spec per asset, a code QA gate, an independent reviewer and explicit human approval, so every asset stays in the
`style.md` look. It is a self-contained folder because `assets/` is copied wholesale into the build. Its style lock
(palette, anchors, fingerprints) still needs to be completed by a person before it can generate anything.

### Full-screen HUD frame

Ember battles draw their HUD inside one full-screen frame: a top bar, a tall bottom dock and two side rails
around a transparent viewport that the isometric arena is fitted into (`presentation/FUiFrame`). Everything
about the frame is data in [`assets/data/ui_layout.json`](assets/data/ui_layout.json), authored on a
1280x800 design canvas: the band thickness, where the frame art is cut for stretching, and the named zones
(`title`, `turn_order`, `status`, `party`, `unit`, `skills`, `end_turn`, `journal`, `hints`, `targets`) the HUD
fills. Each zone has an anchor (`top-left`, `bottom-center`, ...) naming the screen edge it keeps its distance
from, so other window shapes stretch the middle of the bars and the viewport rather than distorting portraits
and slots. Moving a zone to fit new art needs no rebuild, only an edit of that file. `ctest` checks that the
zones stay inside their bands and clear of each other for a range of window sizes.

`assets/art/ui_frame.png` is the frame art. It is optional: without it the four bands are drawn with the existing
stone `ui_panel` skin, so the layout still works. Replacing the art needs no code change:

- Native 320x200 (a 4x design-pixel ratio, so the PNG's own size never matters), RGBA, nearest-neighbor.
- The center rectangle (art x 8-311, y 14-147) is the viewport and must be fully transparent; every other pixel is
  opaque. PixelLab does not produce alpha for large holes reliably: either generate the hole as flat `#FF00FF` and
  key that color out, or (as the shipped art does) build the frame from pieces and never paint the viewport at all.
- Cut lines for stretching are columns 100 and 220 (`frame.slice` left/right 400) and rows 14 and 148. The strip
  between the two columns is the only part that stretches when the window is not 16:10, and it has to hold the skill
  row at the narrowest window (`min_width - left - right >= 340`), so everything in it must stay uniform: the skill
  slots sit on one continuous recess, not on separate wells. The side blocks keep their size, so the portrait wells
  (left) and the journal and button (right) stay aligned at any window size.
- Dock bays, in art pixels (design / 4): four 20x20 portrait wells at x 10, 33, 56, 79, y 154; one skill recess at
  x 113-207, y 170-187 under the four 15x15 slots at x 117-203, y 172; a recessed journal field at x 220-310,
  y 151-177; a button plate at x 264-310, y 179-193. No text, no characters, no baked grid lines (see `style.md`).
- Keep the four outer corners clear of text: the title and status start 44 design px (11 art px) in from the
  screen edge, so the corner ornaments are 8x8.

The shipped PNG is a composite, because generating the whole frame in one go did not respect the dock layout
(text-only prompts ignored it, img2img from a layout guide either copied the guide or dropped slots). It combines a
stone texture and a corner filigree made with `create_image_pixflux` under a forced 20-colour Ember palette with the
viewport lip, rivets, well bezels and bay rectangles drawn at exact coordinates; the stone is darkened per band so
HUD text stays legible. `assets/art/manifest.json` lists the job ids and seeds of the two pieces and the rejected
candidates.

The narrowest window the engine allows is 1100x740 (`Engine.cpp`). `design.min_width` is 1160, so below that width
the layout is compressed rather than reflowed, which keeps at least 10 design px between neighbouring zones.

The art in `assets/art` predates the pipeline: it was requested through PixelLab MCP directly, with its explicit
style controls and subject-specific text. The CLI wrapper that `style.md` used to link no longer exists;
[`asset-pipeline/`](asset-pipeline/README.md) replaces it for all new art.

## Verify

```powershell
ctest --preset windows-msvc-debug --output-on-failure
.\out\build\windows-msvc-debug\Debug\game01P.exe --ember --smoke-test
.\out\build\windows-msvc-debug\Debug\game01P.exe --ember --capture .\out\title.png
$env:GAME01P_START = "battle"
.\out\build\windows-msvc-debug\Debug\game01P.exe --ember --capture .\out\battle.png
```

CTest covers collisions, path traversal/traps, LOS including diagonal corners,
AP separation, stale turn tokens, KO occupancy, generated-room invariants (enemies on top, connectivity, variety), three seeded complete dungeons,
AI progress, rewards, rune legality, checkpoint advancement and defeat reset.
It also verifies focused-HUD keyboard shortcuts, modal keyboard capture, bad
rune-drop content rejection, scheduler-independent AI token propagation, camp
rune conservation/rejections, and actual choice-window text under changed
balance data, empty-cell targeting and line pierce blocked by walls. It boots
Ember's title and battle with SDL's dummy video/software renderer and checks
that the removed forest prototype mode's switches exit with code 2.
Tests use isolated checkpoint files, not the player's save.

Content and balance live in `assets/data/dungeon.json`, `units.json`,
`skills.json`, `runes.json`, and `statuses.json`. Gameplay rules remain separate
from SDL/ImGui presentation. For automated combat, set `GAME01P_AUTOPLAY=1`
together with `GAME01P_START=battle`; this does not choose run rewards for you.
Use `--ember --frames 800 --capture .\out\reward.png` for a bounded capture after
combat, or `--frames N` to quit automatically after 1-100000 frames.

For semantic tooling, place a generated CMake `compile_commands.json` in the
repository root (ignored by Git). `.clangd` uses its compiler driver and C++
standard rather than forcing Clang flags onto MSVC commands. CMake's Visual
Studio generator does not export this database; use a Ninja configuration
with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` for indexing.

## Packaging

Developer checkout only (`.claude/` is not part of the published `release` branch): `node .claude/scripts/package.mjs` builds the static Release preset `windows-msvc-package` and writes a single file, `out/package/EmberDungeon.exe`: no DLLs, no `assets/` folder, no tests, no console window. The first run builds the static vcpkg tree and takes a long time.
The assets are embedded in the exe and unpacked on start into `%TEMP%\EmberDungeon\<pack hash>\assets`; set `GAME01P_PACKAGE_CACHE` to use another directory (the hash subfolder is still appended). The game log lands there too.
The packaged exe changes its working directory to that folder, so pass absolute paths to it (`--capture`, `--record`, `GAME01P_SAVE_DIR`).
Do not place the exe next to an `assets/` folder: that folder is searched before the embedded copy.
`out/package-evidence.json` records the tree, size, imported DLLs and the smoke result of the last package run.

## macOS package

On an Apple Silicon Mac with the Xcode command line tools (`xcode-select --install`), CMake, Ninja and pkg-config (`brew install cmake ninja pkg-config`):

    git clone --single-branch -b release https://github.com/neoguri7/game01P.git   # later: git pull
    bash package/macos/package.sh
    open out/package/EmberDungeon.app

The script builds the static Release preset `macos-clang-package` (the first run also downloads and builds the dependencies into `out/vcpkg`, about ten minutes) and writes `out/package/EmberDungeon.app` and `out/package/EmberDungeon-macos-arm64.zip`; the assets are inside the bundle and the app needs macOS 13.3 or newer.
The same script runs in GitHub Actions on every push to `release`; the workflow run's `EmberDungeon-macos-arm64` artifact holds the zip. A downloaded copy is quarantined by macOS: run `xattr -dr com.apple.quarantine EmberDungeon.app` once, or allow it under System Settings > Privacy & Security > Open Anyway. A bundle built on the same Mac starts without that step.
Saves are in `~/Library/Application Support/Game01P/EmberDungeon/`; the log is `game01p.log` in a folder under `$TMPDIR/EmberDungeon/`.
