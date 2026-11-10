# WP-36: Remove the forest prototype mode

Size: L · Player-facing: no (the shipped game is Ember; forest was a dev-only prototype) · Certainty: settled · Base commit: (set at handoff, after WP-35 merges) · Branch: wp/WP-36

## 1. Goal [S]
Delete the old "Forest Prototype" mode so Ember is the only game, cutting the code, tests and boot work that every WP currently pays for. Removed switches fail loudly so stale scripts surface.
- Pillar served: none directly (scope reduction).
- Decisions it rests on: user ruling of 2026-11-10 "forest mode 삭제" (`docs/design/decisions.md` [확정] "forest 프로토타입 모드 삭제", 사용자 2026-11-10, WP-36).

§2 (Player experience) and §4 (Numbers) do not apply: no player-facing change, no gameplay values.

## 3. Behaviors [S]
| ID | Behavior | Proving test |
|---|---|---|
| F1a | WHEN `game01P` starts with `--forest` THEN it SHALL write `forest mode was removed` to stderr and exit with code 2 before the engine initializes | `ctest::removed_forest_flag` |
| F1b | WHEN `game01P` starts with `--verify-prototype` THEN it SHALL write `forest mode was removed` to stderr and exit with code 2 before the engine initializes | `ctest::removed_verify_flag` |
| F1c | WHEN `game01P` starts with env `GAME01P_MODE=forest` (here with `--smoke-test`) THEN it SHALL write `forest mode was removed` to stderr and exit with code 2 before the engine initializes | `ctest::removed_mode_env` |
| F2a | WHEN `game01P --ember --smoke-test` runs with `assets/data/prototype-run.json` and `assets/data/battlefield.json` absent from the staged assets THEN the title smoke SHALL pass (`--ember` is an accepted no-op) | `ctest::dungeon_title_smoke` (existing, unchanged) |
| F2b | WHEN the same runs with `GAME01P_START=battle` THEN the battle smoke SHALL pass | `ctest::dungeon_battle_smoke` (existing, unchanged) |
| F4 | WHEN the node-tree fixture is loaded through the test-local helper `LoadTreeFile` (file → JSON → `FPassiveTree::Load`) THEN v1 and v2 SHALL load and v3 and a missing file SHALL be rejected, as before | `node_tree_tests::PassiveTreeFileLoadsV1AndV2` |
| F5 | WHEN a battle runs without an `FDungeonRun` THEN no run-log line SHALL be appended (former `ForestBattleNoAppend`, body unchanged) | `boss_goals_tests::NoDungeonRunBattleNoAppend` |

Proving-test grammar and the `features.json` schema are WP-35 §3a; the gate is WP-35 §3c. `features.json` sets `"allowTimeoutChange": true` (the only TIMEOUT change is deleting the forest-only `TIMEOUT 90` override) and `"allowCheckDrop"` for `state_machine_tests.cpp` and `pipeline_tests.cpp` (forest-only tests deleted, §7). No file under `tests/` is deleted.

## 5. File map [S]
Line numbers are hints only ("≈"): WP-35 edits `CMakeLists.txt` before this WP, and earlier edits in a file shift later lines. Find every edit by the quoted text. "Replace X with Y" means exact text; "delete" means the quoted lines only.

**Delete (T3, T5):**
- `src/states/FTitleState.h`, `src/states/FTitleState.cpp`, `src/states/FHubState.h`, `src/states/FHubState.cpp`, `src/states/FBattleState.h`, `src/states/FBattleState.cpp`
- `src/debug/PrototypeChecks.h`, `src/debug/PrototypeChecks.cpp`
- `src/ecs/systems/GridRenderSystem.h`, `src/ecs/systems/CombatHudSystem.h`
- `src/gameplay/run/FPrototypeRun.h`, `src/gameplay/run/FPrototypeRun.cpp` (the `src/gameplay/run/` directory becomes empty)
- `src/gameplay/FGameMode.h`
- `src/gameplay/grid/FBattleLayout.h` — after the other deletions its only users (`PrototypeChecks.cpp`, `CombatHudSystem.h`, `FBattleState.cpp`, the forest branch of `GridCameraSystem.h`) are gone; `BattleLayout` and `FitBattleCamera` (≈22) have no caller left. `gameplay/grid/FCameraMath.h` stays (other users).
- `assets/data/prototype-run.json`, `assets/data/battlefield.json`
- `assets/art/forest_ground.png`, `assets/art/forest_oak.png`, `assets/art/forest_waystone.png` (only `GridRenderSystem.h` loads them)

**Create:**
- `tests/check_removed_flag.cmake` — exact content:
```cmake
# WP-36: runs game01P with one switch of the removed forest prototype mode and requires the rejection contract:
# exit code 2 and "forest mode was removed" on stderr.
# Usage: cmake -DEXE=<game01P path> -DARG=<argument> [-DMODE_ENV=<GAME01P_MODE value>] -P check_removed_flag.cmake
if(NOT DEFINED EXE OR NOT DEFINED ARG)
    message(FATAL_ERROR "check_removed_flag.cmake needs -DEXE=<game01P path> and -DARG=<argument>")
endif()
if(DEFINED MODE_ENV)
    set(ENV{GAME01P_MODE} "${MODE_ENV}")
endif()
execute_process(
    COMMAND "${EXE}" "${ARG}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
    TIMEOUT 30)
if(NOT result STREQUAL "2")
    message(FATAL_ERROR "expected exit code 2 for '${ARG}', got '${result}'. stderr: ${stderr_text}")
endif()
string(FIND "${stderr_text}" "forest mode was removed" found)
if(found EQUAL -1)
    message(FATAL_ERROR "stderr for '${ARG}' lacks 'forest mode was removed': ${stderr_text}")
endif()
message(STATUS "'${ARG}' rejected with exit code 2")
```

**Modify — `CMakeLists.txt`** (inside `if(BUILD_TESTING)`):
- C1 (T1): insert this block immediately before the line starting `    # B7: always run asset staging.` (≈113):
```cmake
    # WP-36: the removed forest prototype mode rejects its switches with exit code 2 before the engine starts, so
    # these tests need no staged assets (no AssetStage fixture, default working directory). The dummy drivers and
    # the save directory only matter while the old binary still boots (the red run).
    add_test(NAME removed_forest_flag COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:game01P> -DARG=--forest
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_removed_flag.cmake")
    add_test(NAME removed_verify_flag COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:game01P> -DARG=--verify-prototype
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_removed_flag.cmake")
    add_test(NAME removed_mode_env COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:game01P> -DARG=--smoke-test
        -DMODE_ENV=forest -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_removed_flag.cmake")
    set_tests_properties(removed_forest_flag removed_verify_flag removed_mode_env PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software;GAME01P_SAVE_DIR=${CMAKE_BINARY_DIR}/test-saves/removed_flags"
        RUN_SERIAL TRUE)
```
- C2 (T2): delete the three lines
  `    add_test(NAME forest_title_smoke COMMAND game01P --forest --smoke-test)` ·
  `    add_test(NAME forest_battle_smoke COMMAND game01P --forest --smoke-test)` ·
  `    add_test(NAME forest_prototype_checks COMMAND game01P --forest --verify-prototype)` (≈92–94).
- C3 (T2): replace `    set_tests_properties(dungeon_title_smoke dungeon_battle_smoke forest_title_smoke forest_battle_smoke forest_prototype_checks PROPERTIES` (≈95) with `    set_tests_properties(dungeon_title_smoke dungeon_battle_smoke PROPERTIES`. The property lines under it (WP-35's `ENVIRONMENT`, `TIMEOUT 30`, `RUN_SERIAL`, `FIXTURES_REQUIRED`) stay as they are.
- C4 (T2): delete the two lines `    # The prototype verifier runs ~26 s of scripted checks on a software renderer: give it headroom over the 30 s.` and `    set_tests_properties(forest_prototype_checks PROPERTIES TIMEOUT 90)` (≈101–102), and the line `    set_property(TEST forest_battle_smoke APPEND PROPERTY ENVIRONMENT "GAME01P_START=battle")` (≈104). Keep the `dungeon_battle_smoke` line above it.
- C5 (T2): replace `    set(SMOKE_TESTS dungeon_title_smoke dungeon_battle_smoke forest_title_smoke forest_battle_smoke forest_prototype_checks)` (≈107) with `    set(SMOKE_TESTS dungeon_title_smoke dungeon_battle_smoke)`.

**Modify — `src/main.cpp`** (all T2):
- M1 includes: delete `#include "debug/PrototypeChecks.h"` (≈6), `#include "ecs/systems/CombatHudSystem.h"` (≈17), `#include "ecs/systems/GridRenderSystem.h"` (≈24), `#include "gameplay/FGameMode.h"` (≈32), `#include "gameplay/run/FPrototypeRun.h"` (≈33). Replace `#include <cstdlib>` (≈46) with the two lines `#include <cstdio>` and `#include <cstdlib>`. Keep every other include (including `debug/DebugOverlay.h`, `presentation/FUiTheme.h`, `presentation/FForestScene.h`).
- M2 `RegisterSystems`: delete `    systems.addSystem<game::ecs::GridRenderSystem>();` (≈72) and `    systems.addSystem<game::ecs::CombatHudSystem>();` (≈75). `EmberGridRenderSystem`/`EmberCombatHudSystem` stay.
- M3 argument parsing: replace the block from `    game::EGameMode mode = game::EGameMode::Ember;` (≈125) through `        else if (argument == "--check-animation") checkAnimation = true;` (≈138) — i.e. the mode variable, the `SDL_getenv("GAME01P_MODE")` block, `bool verifyPrototype`, `bool checkAnimation`, `int windowWidth`, `int windowHeight`, the `for` header, `const std::string_view argument`, and the `--ember`/`--forest`/`--verify-prototype`/`--check-animation` lines — with:
```cpp
    // [WP-36] The removed forest prototype mode: its switches and GAME01P_MODE=forest exit with code 2 before the
    // engine starts, so stale scripts surface instead of silently booting Ember.
    if (const char* value = std::getenv("GAME01P_MODE"); value != nullptr && std::string_view(value) == "forest") {
        std::fputs("forest mode was removed\n", stderr);
        return 2;
    }
    bool checkAnimation = false;
    int windowWidth = 1280;
    int windowHeight = 0;  // 0 = the default 800 unless --window says otherwise
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--ember") continue;  // Ember is the only mode; the switch stays accepted for old scripts.
        if (argument == "--forest" || argument == "--verify-prototype") {
            std::fputs("forest mode was removed\n", stderr);
            return 2;
        }
        if (argument == "--check-animation") checkAnimation = true;
```
  The next original line `        else if (argument == "--smoke-test") {` and the rest of the loop stay unchanged.
- M4 engine setup: replace the four lines
```cpp
    const bool ember = mode == game::EGameMode::Ember;
    if (windowHeight == 0) windowHeight = ember ? 800 : 720;
    // The catalog owns this mode's state ids and factories; the engine creates no state of its own.
    engine.setStateCatalog(ember ? game::FStateCatalog::MakeEmber() : game::FStateCatalog::MakeForest());
```
  plus the following `    if (!engine.initialize(ember ? "Ember Dungeon - Game01P" : "Game01P - Forest Prototype", windowWidth, windowHeight)) {` (≈185–189) with:
```cpp
    if (windowHeight == 0) windowHeight = 800;
    // The catalog owns the state ids and factories; the engine creates no state of its own.
    engine.setStateCatalog(game::FStateCatalog::MakeEmber());
    if (!engine.initialize("Ember Dungeon - Game01P", windowWidth, windowHeight)) {
```
- M5: delete `    engine.getRegistry().ctx().emplace<game::FGameMode>(mode);` (≈209).
- M6 `shutdown` lambda: delete `        engine.getRegistry().ctx().erase<game::FPrototypeRun>();` (≈212); replace `        // [WP-20] no-op when meta was never initialized (forest/verify shutdown paths).` (≈216) with `        // [WP-20] no-op when meta was never initialized (a boot that failed before InitializeMetaProgress).`
- M7: replace `    // [WP-20] meta save: the service initializes inside the ember block below (it needs InitializeDungeon's checkpoint;` (≈236) with `    // [WP-20] meta save: the service initializes in the boot block below (it needs InitializeDungeon's checkpoint;` (the next comment line stays).
- M8: replace `    engine.setOverlayRenderer(ember ? game::RenderDungeonOverlay : game::RenderDebugOverlay);` (≈239) with `    engine.setOverlayRenderer(game::RenderDungeonOverlay);` and `    if ((!ember || checkAnimation) && !LoadStage0Definitions(startupScene)) {` (≈240) with `    if (checkAnimation && !LoadStage0Definitions(startupScene)) {`.
- M9: delete the prototype load and verify block, from `    const auto* content = engine.getRegistry().ctx().find<game::gameplay::FContentRegistry>();` (≈255) through the `    }` that closes `    if (verifyPrototype) {` (≈269) — 15 lines: `content`, `auto run = …`, the `if (!run) {…}` block, the `emplace<game::FPrototypeRun>` line, and the `if (verifyPrototype) {…}` block. The `FGameplayServices::Initialize` block above it stays.
- M10: replace the block from `    if (ember) {` (≈270) through the `    }` that closes `    } else {` / `        game::ui::ApplyTheme();` (≈303) with the Ember body, de-indented by one level — exact result:
```cpp
    if (!game::InitializeDungeon(engine.getRegistry()) || !game::InitializeDungeonArt(engine.getRegistry()) ||
        !game::InitializeForestScene(engine.getRegistry()) || !game::InitializeBattleAnim(engine.getRegistry()) ||
        !game::ui::InitializeUiFrame(engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }
    // [WP-20] meta save: shipped-content fail-fast; resolves/loads/imports after the checkpoint exists.
    if (!game::InitializeMetaProgress(engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }
    // [WP-21b] boot validation of the roster's skill pools — the prep screen's slot contract depends on
    // it. The check lives here, not in InitializeMetaProgress: it validates SHIPPED content, while tests
    // boot with the frozen fixture (3-skill pools) and call ValidateRosterContent directly when they
    // mean to test the rule.
    if (const auto problem = game::ValidateRosterContent(engine.getRegistry())) {
        LOG_ERROR("Roster content is invalid: {}", *problem);
        shutdown();
        return EXIT_FAILURE;
    }
    // [WP-09h] boot validation of the unlock catalog's consumers — a kind=skill/rune row that reaches
    // no content would sell a no-op (w9-hub critic F4). Same shipped-content grade as the roster check
    // above; the frozen fixture world keeps its deliberately consumer-less rows because no test boots
    // through this file — tests call ValidateUnlockTargets directly.
    if (const auto problem = game::ValidateUnlockTargets(engine.getRegistry())) {
        LOG_ERROR("Unlock content is invalid: {}", *problem);
        shutdown();
        return EXIT_FAILURE;
    }
    game::InitializeDungeonUi(engine.getRegistry());
```
- M11: inside `    if (const char* policyText = std::getenv(game::dev::kAutopilotEnv); …) {` (≈310), replace from `        if (!ember) {` (≈311) through the `        }` that closes its `} else {` branch (≈351, the line before the `    }` that closes the `policyText` `if`) with the else body de-indented by one level — exact result between the `if (const char* policyText …) {` line and its closing `    }`:
```cpp
        const bool defaultPolicy = std::string_view(policyText) == "1" || std::string_view(policyText) == "default";
        const std::string_view policyValue =
            defaultPolicy ? game::dev::kDefaultAutopilotPolicy : std::string_view(policyText);
        const auto policy = game::dev::ParseAutopilotPolicy(policyValue);
        if (!policy) {
            SDL_Log("GAME01P_AUTOPILOT: %s", policy.error().c_str());
            shutdown();
            return EXIT_FAILURE;
        }
        int maxRooms = 0;
        if (const char* roomsText = std::getenv(game::dev::kAutopilotMaxRoomsEnv);
            roomsText != nullptr && roomsText[0] != '\0') {
            const std::string_view rooms(roomsText);
            const auto roomsResult = std::from_chars(rooms.data(), rooms.data() + rooms.size(), maxRooms);
            if (roomsResult.ec != std::errc{} || roomsResult.ptr != rooms.data() + rooms.size() || maxRooms < 1) {
                SDL_Log("GAME01P_AUTOPILOT_MAX_ROOMS must be an integer >= 1.");
                shutdown();
                return EXIT_FAILURE;
            }
        }
        autopilot = std::make_shared<game::dev::FRunAutopilot>(*policy, maxRooms);
        engine.setFrameDriver([autopilot](entt::registry& registry, std::string_view topStateId) {
            switch (autopilot->tick(registry, topStateId)) {
            case game::dev::EAutopilotStatus::Running:
                return game::EFrameDriverResult::Continue;
            case game::dev::EAutopilotStatus::Finished:
                return game::EFrameDriverResult::Stop;
            case game::dev::EAutopilotStatus::Stuck:
                return game::EFrameDriverResult::Fail;
            }
            return game::EFrameDriverResult::Continue;
        });
        LOG_INFO("[autopilot] policy='{}' max_rooms={}", policyValue, maxRooms);
        if (const char* autoplay = std::getenv("GAME01P_AUTOPLAY");
            autoplay == nullptr || std::string_view(autoplay) != "1") {
            LOG_WARN("[autopilot] GAME01P_AUTOPLAY is not 1: battles will wait for input");
        }
```
  After M3–M11 no variable named `ember`, `mode`, `verifyPrototype`, `content` or `run` remains in `main()` (the words may still appear in comments and strings).

**Modify — `src/ecs/systems/GridCameraSystem.h`** (T2): replace the whole file with (Ember path unconditional; forest pan/zoom path, `lastWidth`/`lastHeight`, `FGameMode.h`, `FBattleLayout.h` and `<cmath>` gone; `FGridTypes.h` now included directly because `FGridMetrics` used to come through `FBattleLayout.h`):
```cpp
#pragma once

#include <algorithm>
#include <entt/entt.hpp>
#include <string>

#include "core/EngineState.h"
#include "core/InputState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/ISystem.h"
#include "gameplay/grid/FGridTypes.h"

namespace game::ecs {

struct GridCameraSystem : ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        auto* input = registry.ctx().find<FInputState>();
        if (!input) return;

        entt::entity cameraEntity = entt::null;
        for (const auto entity : registry.view<FCamera, FTag>()) {
            if (registry.get<FTag>(entity).value == "battle_camera") {
                cameraEntity = entity;
                break;
            }
        }
        if (cameraEntity == entt::null) return;

        auto& camera = registry.get<FCamera>(cameraEntity);
        const auto* window = registry.ctx().find<FEngineWindowState>();
        const auto* metrics = registry.ctx().find<FGridMetrics>();
        if (!window || !metrics) return;
        const glm::vec2 extent = glm::vec2{static_cast<float>(metrics->width), static_cast<float>(metrics->height)} *
            metrics->tileSize;
        camera.zoom = std::clamp(std::min((window->width * .58f - 64.f) / extent.x,
                                          (window->height - 220.f) / extent.y), .25f, 1.f);
        camera.position = metrics->origin + extent * .5f +
            glm::vec2{window->width * .21f, -25.f} / camera.zoom;
    }

    std::string name() const override { return "GridCameraSystem"; }
};

} // namespace game::ecs
```
This must land in the same task as M5 (deleting the `FGameMode` emplace): without the emplace, the old `IsEmberMode` test is false and Ember would take the forest camera path.

**Modify — `src/states/FStateCatalog.h`** (T3): replace `/// Registration is explicit inside MakeEmber()/MakeForest(). No static self-registration: game01P_runtime is a` (≈17) with `/// Registration is explicit inside MakeEmber(). No static self-registration: game01P_runtime is a`; replace `    /// Set the two boot ids (normal start, GAME01P_START=battle). MakeEmber()/MakeForest() use it; a custom` (≈40) with `    /// Set the two boot ids (normal start, GAME01P_START=battle). MakeEmber() uses it; a custom`; delete `    static FStateCatalog MakeForest();` (≈45).

**Modify — `src/states/FStateCatalog.cpp`** (T3): delete `#include "states/FBattleState.h"` (≈7), `#include "states/FHubState.h"` (≈14), `#include "states/FTitleState.h"` (≈15); delete the whole function `FStateCatalog FStateCatalog::MakeForest() { … }` (≈91–98, 8 lines) and the one blank line above it, so `MakeEmber`'s closing `}` is followed by one blank line and `} // namespace game`.

**Modify — `tests/features/state_machine_tests.cpp`** (T3):
- Delete the includes `#include "gameplay/run/FPrototypeRun.h"` (≈30), `#include "states/FBattleState.h"` (≈31), `#include "states/FHubState.h"` (≈36), `#include "states/FTitleState.h"` (≈38).
- `CatalogResolvesEveryRegisteredId`: replace its doc comment `/// Both shipped catalogs resolve every registered id and expose a contained start/debug-battle id.` (≈180) with `/// The shipped catalog resolves every registered id and exposes a contained start/debug-battle id.`; delete `    check(game::FStateCatalog::MakeForest(), {"forest.title", "forest.hub", "forest.battle"});` (≈198) and the three lines `    const auto forest = game::FStateCatalog::MakeForest();`, `    CHECK(forest.startId() == "forest.title");`, `    CHECK(forest.debugBattleId() == "forest.battle");` (≈203–205). The `MakeEmber` checks stay.
- `StartStateCreatedOnce`: replace the three lines (≈416–418)
```cpp
    // Both shipped catalogs: the id selector is pure and the real engine boots on startId()/debugBattleId().
    for (const bool ember : {true, false}) {
        auto catalog = ember ? game::FStateCatalog::MakeEmber() : game::FStateCatalog::MakeForest();
```
  with
```cpp
    // The shipped catalog: the id selector is pure and the real engine boots on startId()/debugBattleId().
    {
        auto catalog = game::FStateCatalog::MakeEmber();
```
  The rest of that block (through its closing `    }`, ≈433) stays unchanged — it is now a plain scope block.
- Delete `ForestMappingTable`: from `/// forest: every reachable return point maps 1:1 to the WP-01 §4 table.` (≈577) through the function's closing `}` (≈630) and the one blank line after it (≈631), so the preceding function's closing `}` (≈575) and one blank line are followed directly by `/// ember: every reachable return point maps 1:1 to the WP-01 §4 table.` Delete its registration `        {"forest mapping table", ForestMappingTable},` (≈728).

**Modify — `tests/features/pipeline_tests.cpp`** (T3): delete `#include "gameplay/run/FPrototypeRun.h"` (≈52) (`FPassiveStatBonuses` still arrives through `gameplay/dungeon/FDungeonRun.h`). Delete from the `// ----…` line (≈669) directly above `// Forest (frozen mode): the inline passive add stays the only source of those bonuses` (≈670) through the closing `}` of `ForestNoDoubleBonus` (≈813) and the blank line after it (≈814) — this removes `ForestBattlefieldDocument`, `FForestFixture`, `ForestPathUnchanged` and `ForestNoDoubleBonus`. Keep `PreviewLogsReason` (ends ≈667 with `    CHECK(flooredFound);` / `}`) and the blank line after it, then `} // namespace`. In `main()` delete only `        {"forest path unchanged", ForestPathUnchanged},` and `        {"forest no double bonus", ForestNoDoubleBonus},` (≈831–832).

**Modify — `tests/features/node_tree_tests.cpp`** (T1):
- Delete `#include "gameplay/run/FPrototypeRun.h"` (≈42). (`<filesystem>`, `<fstream>`, `<optional>`, `nlohmann/json.hpp` and `gameplay/progression/FPassiveTree.h` are already included.)
- Replace the two lines (≈681–682) — the line starting `// --- prototype tree file loading (WP-32h) ` and `void PrototypeTreeFileLoadsV1AndV2() {` — with:
```cpp
// --- passive tree file loading (WP-32h) ---------------------------------------------------------
// Test-local file reader (formerly FPrototypeRun::LoadPassiveTreeFile, minus its logging): a missing file or
// invalid JSON gives nullopt, anything else is up to FPassiveTree::Load.
std::optional<game::FPassiveTree> LoadTreeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    auto document = nlohmann::json::parse(file, nullptr, false);
    if (document.is_discarded()) return std::nullopt;
    return game::FPassiveTree::Load(document);
}

void PassiveTreeFileLoadsV1AndV2() {
```
- Replace each of the four occurrences of `game::FPrototypeRun::LoadPassiveTreeFile(` (≈684, 694, 706, 709) with `LoadTreeFile(`. Nothing else in the function changes (same CHECKs).
- Replace the registration `        {"PrototypeTreeFileLoadsV1AndV2", PrototypeTreeFileLoadsV1AndV2},` (≈784) with `        {"PassiveTreeFileLoadsV1AndV2", PassiveTreeFileLoadsV1AndV2},`.

**Modify — `tests/features/boss_goals_tests.cpp`** (T1): replace `void ForestBattleNoAppend() {` (≈1434) with `void NoDungeonRunBattleNoAppend() {`; replace `    // A forest-like registry (no FDungeonRun) must not append: AppendRunEvent would create a run log.` (≈1435) with `    // A registry without FDungeonRun must not append: AppendRunEvent would create a run log.`; replace `        {"ForestBattleNoAppend", ForestBattleNoAppend},` (≈2142) with `        {"NoDungeonRunBattleNoAppend", NoDungeonRunBattleNoAppend},`.

**Modify — comments only (T4).** Each row: file, ≈line, original line(s) → new line(s). Code is unchanged; the `registry.ctx().contains<FDungeonRun>()` guards stay.
| File | ≈Line | Original | New |
|---|---|---|---|
| `src/ecs/systems/ActionResolverSystem.h` | 367–368 | `                        // leaves no trace. Ember-only guard: forest passives are added inline in` / `                        // FBattleState and a refresh would flatten them back to base (FCombatSheet.h).` | `                        // leaves no trace. Guarded on FDungeonRun: a registry without a dungeon run (test` / `                        // fixtures, validation probes) keeps the sheet it was set up with (FCombatSheet.h).` |
| `src/ecs/systems/CombatOutcomeSystem.h` | 77–78 | `            // Forest battles have no FDungeonRun; the guard matters because AppendRunEvent creates a log when` / `            // none exists.` | `            // A battle without FDungeonRun (test fixtures) must not append: AppendRunEvent creates a log when` / `            // none exists.` |
| `src/ecs/systems/TurnFlowSystem.h` | 152–153 | `            // the skip turn. Idempotent; Ember-only guard — a forest refresh would flatten the inline` / `            // passives to base (FCombatSheet.h × FBattleState.cpp).` | `            // the skip turn. Idempotent; guarded on FDungeonRun — a registry without a dungeon run keeps the` / `            // sheet it was set up with (FCombatSheet.h).` |
| `src/gameplay/combat/FBattleFlow.h` | 13 | `/// reads it. An absent context means "nothing to show" (headless tests, fixtures, forest).` | `/// reads it. An absent context means "nothing to show" (headless tests, fixtures).` |
| `src/gameplay/combat/FBattleFlow.h` | 71–72 | `/// Level-triggered wait predicate (WP-05a): the show is over when there is no gate (headless, fixtures,` / `/// forest), nothing is left to show, or the fail-safe clock has timed out.` | `/// Level-triggered wait predicate (WP-05a): the show is over when there is no gate (headless, fixtures),` / `/// nothing is left to show, or the fail-safe clock has timed out.` |
| `src/gameplay/combat/FBattleFlow.cpp` | 24 | `    if (gate == nullptr) return true;  // headless, fixtures, forest: nothing is being shown` | `    if (gate == nullptr) return true;  // headless, fixtures: nothing is being shown` |
| `src/gameplay/combat/FBattleCommand.h` | 15 | `/// entered FActionQueue::pending through a direct push (forest, the pre-WP-12 HUD, tests that predate the` | `/// entered FActionQueue::pending through a direct push (the pre-WP-12 HUD, tests that predate the` |
| `src/gameplay/battle/FBattleSetup.cpp` | 28–29 | `/// WP-22: the fallback loadout is capped to four skills — the HUD hotkeys, the 4-slot bar and the forest` / ``/// skill_slots are all 4 (W7 review, 4-slot hard limit). units.json `skills` is the authored POOL; the`` | `/// WP-22: the fallback loadout is capped to four skills — the HUD hotkeys and the 4-slot bar are both 4` / ``/// (W7 review, 4-slot hard limit). units.json `skills` is the authored POOL; the`` |
| `src/gameplay/battle/FBattleSetup.cpp` | 55–56 | `    // Only a boss-slot dungeon battle has both the run and the marker; forest battles and validation probes` / `    // (SetupCombatUnits on a throwaway registry) are a no-op.` | `    // Only a boss-slot dungeon battle has both the run and the marker; battles without FDungeonRun and` / `    // validation probes (SetupCombatUnits on a throwaway registry) are a no-op.` |
| `src/gameplay/battle/FBattleSetup.cpp` | 161–162 | `        // FStats/FHealth/FSpeed와 동일 — 동작 불변. 여기서 갱신을 호출하지 않는다: forest는 인라인 패시브` / `        // 가산(FBattleState.cpp)이 뒤에 오므로 순서가 겹치면 이중 가산이 된다.` | one line: `        // FStats/FHealth/FSpeed와 동일 — 동작 불변. 여기서는 갱신(RefreshCombatSheet)을 호출하지 않는다.` |
| `src/gameplay/combat/FCombatSheet.h` | 18–19 | `/// ②는 FDungeonRun 경로 전용이다 — forest 패시브(FPrototypeRun)는 FBattleState의 인라인 가산이 계속` / `/// 소유하므로 여기서 읽지 않는다(읽으면 이중 가산 — ForestNoDoubleBonus).` | one line: `/// ②는 FDungeonRun 경로 전용이다 — FDungeonRun이 없으면 런 보너스를 더하지 않는다.` |
| `src/gameplay/combat/FCombatSheet.cpp` | 46–47 | `    // ② 런 보너스 — FDungeonRun 경로(Ember)에서만. forest 패시브(FPrototypeRun)는 FBattleState.cpp의` / `    //    인라인 가산이 계속 소유하므로 여기서 읽지 않는다(읽으면 이중 가산 — ForestNoDoubleBonus).` | one line: `    // ② 런 보너스 — FDungeonRun 경로(Ember)에서만.` |
| `src/gameplay/combat/FCombatTypes.h` | 90 | `/// Per-battle bookkeeping owned by the battle state (FBattleState creates and clears it with the rest of` | `/// Per-battle bookkeeping owned by the battle state (FEmberBattleState creates and clears it with the rest of` |
| `src/gameplay/grid/FBattlefieldData.h` | 15 | ``/// One unit as placed in `assets/data/battlefield.json`. `unitTemplateId` names the content unit whose`` | ``/// One unit as placed on a battlefield. `unitTemplateId` names the content unit whose`` |
| `src/gameplay/meta/FMetaProgress.cpp` | 722 | `    // A no-op without an initialized store (the forest/verify shutdown paths). The wiring marker survives` | `    // A no-op without an initialized store (a boot that failed before InitializeMetaProgress). The wiring marker survives` |
| `src/gameplay/meta/FMetaProgress.h` | 42 | `/// Erases the store. A no-op without one (the forest/verify shutdown paths). The listener registration` | `/// Erases the store. A no-op without one (a boot that failed before InitializeMetaProgress). The listener registration` |
| `src/gameplay/meta/FMetaProgress.h` | 136 | in the line starting `/// (meta never initialized — tests, forest): parsed from the FAssetManager documents`, replace the substring `tests, forest)` | `tests)` (rest of the line unchanged) |
| `src/presentation/FSettings.h` | 50 | `/// The loaded settings, or the 1x / no-skip defaults when nothing was loaded (headless tests, forest).` | `/// The loaded settings, or the 1x / no-skip defaults when nothing was loaded (headless tests).` |
| `tests/features/battle_command_tests.cpp` | 1013 | `    // sheet is recomputed from FBaseCombatStats; without it (forest) the tampered values must survive.` | `    // sheet is recomputed from FBaseCombatStats; without it the tampered values must survive.` |
| `tests/features/battle_command_tests.cpp` | 1128–1129 | `    // Forest control: the same hit applies the status, but without FDungeonRun the sheet (and FSpeed) is` / `    // left as-is — the forest passives stay in charge of those values.` | `    // Control without FDungeonRun: the same hit applies the status, but the sheet (and FSpeed) is left` / `    // as-is.` |
| `tests/features/battle_command_tests.cpp` | 1201 | `    // ② no FSettings (headless, forest): the pause runs at 1x — one half-delay step is not enough.` | `    // ② no FSettings (headless): the pause runs at 1x — one half-delay step is not enough.` |

The fixture names `"sheet-forest"`, `"tick-forest"`, `"freeze-forest"` (battle_command_tests ≈1031, 1066, 1131) stay.

**Modify — `assets/art/manifest.json`** (T5): delete the three objects for `forest_ground.png`, `forest_oak.png`, `forest_waystone.png` — from the `    {` line directly above `      "file": "forest_ground.png",` (≈208) through the `    },` that closes the `forest_waystone.png` object (≈228), 21 lines; the `shrine.png` object before and the `icon_sword.png` object after keep their commas. Delete the whole line starting `    "forest": "Forest mode (default executable) now uses the same art set as Ember:` (≈39; the next line `"theme"` keeps the object valid).

**Modify — `README.md`** (T5):
- Replace from `## Two preserved game modes` (≈5) through `there is no second combat scheduler or resolver.` (≈24) with:
```markdown
## Game mode

The executable runs **Ember Dungeon** (controls and loop below); it has no other
mode. The removed forest prototype mode's switches (`--forest`, `--verify-prototype`,
`$env:GAME01P_MODE = "forest"`) print `forest mode was removed` and exit with
code 2. `--ember` is still accepted and changes nothing.

Ember runs on one turn scheduler, value-token action queue, AP/status pipeline,
collision/pathfinding and wall LOS rules, with path traps, rune transactions and
persistent dungeon checkpoints; there is no second combat scheduler or resolver.
```
- In `## Verify`, delete the line `.\out\build\windows-msvc-debug\Debug\game01P.exe --forest --verify-prototype` (≈145).
- Replace the four lines (≈158–161) `balance data, empty-cell targeting and line pierce blocked by walls. It boots` / `both modes' title/battle with SDL's dummy video/software renderer and runs the` / ``user's `--verify-prototype` harness (including preparation/passives, hitboxes,`` / `AP/tokens, autoplay and the complete forest room lifecycle).` with:
```markdown
balance data, empty-cell targeting and line pierce blocked by walls. It boots
Ember's title and battle with SDL's dummy video/software renderer and checks
that the removed forest prototype mode's switches exit with code 2.
```

**Modify — `.github/workflows/windows-build.yml`** (T5): replace `      - name: Verify dungeon prototype` (≈52) with `      - name: Run tests`. Its `run:` lines (`ctest --test-dir build/windows …`) stay.

**Keep (Ember uses them despite the name):** `src/presentation/FForestScene.h/.cpp` (`FForestScene`, `ParseForestScene`, `InitializeForestScene`), `assets/data/scene_forest.json`, `src/presentation/FDungeonArt.h` (≈24 "forest scene" comment), `EmberGridRenderSystem.cpp`, `assets/data/passive-tree.json`, `FPassiveTreePanel`, `SpriteRenderSystem`, `GridSelectSystem`, `src/debug/DebugOverlay.*` (`RenderDebugOverlay` is still called by `FDungeonOverlay.cpp`), `tests/fixtures/**`.

**Do not touch:** gameplay rules and the `contains<FDungeonRun>()` guards, Ember states, `assets/data/*` other than the two deletions, `docs/`, `.claude/`, `style.md` ("dark fantasy forest" is Ember's art theme).

## 6. Tasks [S]
Build command everywhere: `node .claude/scripts/build.mjs` (WP-35; preset `windows-msvc-agent`; prints only errors and failing tests; writes `out/evidence.json`). Run it with a tool timeout ≥ 600000 ms, never in the background, one at a time. Commit after each task.
1. **T1 Tests first** — create `tests/check_removed_flag.cmake`; CMake C1; node_tree_tests edits (F4); boss_goals_tests edits (F5). Done: `node .claude/scripts/build.mjs` → build ok; the only failing tests are `removed_forest_flag`, `removed_verify_flag`, `removed_mode_env` (each fails in ≤ 30 s: the old binary exits 1 or is killed by the script's `TIMEOUT 30`); `node_tree_tests` and `boss_goals_tests` pass. Commit `WP-36: add failing tests`.
2. **T2 Ember-only boot** — `main.cpp` M1–M11; `GridCameraSystem.h` (same task as M5); CMake C2–C5. Done: `node .claude/scripts/build.mjs` exits 0 (all tests pass, the three `removed_*` included; the forest smokes no longer exist). The forest state/run code still compiles in `game01P_runtime` but nothing boots it.
3. **T3 Delete forest code** — delete the source files of §5 "Delete" (all `src/` paths: states, `PrototypeChecks`, `GridRenderSystem.h`, `CombatHudSystem.h`, `FPrototypeRun`, `FGameMode.h`, `FBattleLayout.h`); `FStateCatalog.h/.cpp`; `state_machine_tests.cpp`; `pipeline_tests.cpp`. Done: `cmake --preset windows-msvc-agent` (deleted sources need a reconfigure; timeout ≥ 600000 ms), then `node .claude/scripts/build.mjs` exits 0.
4. **T4 Comments** — the comment table of §5. Done: `node .claude/scripts/build.mjs` exits 0.
5. **T5 Data, art, docs** — delete `assets/data/prototype-run.json`, `assets/data/battlefield.json`, the three `assets/art/forest_*.png`; `manifest.json`; `README.md`; workflow step. Then remove the stale staged copies (asset staging copies without deleting) — PowerShell 7:
   `foreach ($f in 'prototype-run.json','battlefield.json') { $p = "out/build/windows-msvc-agent/Debug/assets/data/$f"; if (Test-Path $p) { Remove-Item $p } }`
   Done, in this order:
   - `node -e "JSON.parse(require('fs').readFileSync('assets/art/manifest.json','utf8'))"` exits 0.
   - `node .claude/scripts/build.mjs` exits 0, and afterwards `Test-Path out/build/windows-msvc-agent/Debug/assets/data/prototype-run.json` and `Test-Path out/build/windows-msvc-agent/Debug/assets/data/battlefield.json` both print `False` (F2 proof: the smokes ran without them).
   - Symbol check prints nothing:
     `git grep -n -E 'MakeForest|FPrototypeRun|PrototypeChecks|FGameMode|EGameMode|IsEmberMode|FBattleState([^A-Za-z]|$)|FHubState|FTitleState|kForest|(^|[^A-Za-z])(GridRenderSystem|CombatHudSystem)|FitBattleCamera|FBattleLayout|LoadPassiveTreeFile|EPrototypeRoom|verifyPrototype|forest_(title|battle)_smoke|forest_prototype_checks|forest_(ground|oak|waystone)|prototype-run|battlefield\.json|ForestBattleNoAppend|PrototypeTreeFile|ForestMappingTable|ForestPathUnchanged|ForestNoDoubleBonus|"forest":' -- src tests CMakeLists.txt README.md .github assets`
   - Word check prints nothing:
     `git grep -n -i forest -- src tests CMakeLists.txt README.md .github | grep -v -E 'ForestScene|scene_forest|forest scene|forest battlefield|(sheet|tick|freeze)-forest|forest mode was removed|removed forest prototype mode|--forest|GAME01P_MODE|removed_forest_flag|MODE_ENV=forest|dark fantasy forest|^tests/fixtures/'`
     The filter admits exactly these remaining lines: `src/main.cpp` — `#include "presentation/FForestScene.h"`, the `[WP-36]` comment line, the `GAME01P_MODE` `getenv` line, the `"--forest" || "--verify-prototype"` line, the two `fputs("forest mode was removed…` lines, the `InitializeForestScene` line; `src/ecs/systems/EmberGridRenderSystem.cpp` — its 5 `FForestScene` lines; `src/presentation/FForestScene.h/.cpp` — all their lines; `src/presentation/FDungeonArt.h` ≈24 (forest scene); `tests/features/battle_command_tests.cpp` — the 3 fixture-name lines; `tests/fixtures/assets/data/skills.json:2` (frozen fixture); `tests/check_removed_flag.cmake` — its header and `forest mode was removed` lines; `CMakeLists.txt` — the C1 comment, `removed_forest_flag` and `MODE_ENV=forest` lines; `README.md` — the new "Game mode" lines, the new Verify line, and ≈90 "dark fantasy forest" (art theme). Any other line is a leftover to fix.

## 7. Tests
- **F1a–F1c** (`removed_forest_flag`, `removed_verify_flag`, `removed_mode_env`): each runs `cmake -DEXE=<game01P> -DARG=<arg> [-DMODE_ENV=forest] -P tests/check_removed_flag.cmake`; the script fails unless the exit code is exactly 2 AND stderr contains `forest mode was removed` — so a silent Ember fallback (exit 0) or a crash (other code) both fail. Rejection precedes engine init, so no `AssetStage` fixture and no working directory are set; the ctest default working directory (the build dir) holds no `assets/`, so in the red run the old binary fails boot quickly instead of running. Red in T1, green from T2.
- **F2a/F2b**: the existing smokes, unchanged (`--ember --smoke-test`, battle with `GAME01P_START=battle`). They prove `--ember` is an accepted no-op and that Ember boots without the two data files once T5 deletes their staged copies before the final build. Regression tests: green before and after.
- **F4**: `PassiveTreeFileLoadsV1AndV2` keeps every CHECK (v1 fixture loads, v2 temp document loads, v3 rejected, missing path rejected); only the loader moves into the test file. Green before and after (rename + helper in T1).
- **F5**: `NoDungeonRunBattleNoAppend` — the body of `ForestBattleNoAppend` unchanged, only the name and one comment. Green before and after.
- **Removed tests** (cover deleted code only): `forest_title_smoke`, `forest_battle_smoke`, `forest_prototype_checks` (CMake); `state_machine_tests` case `forest mapping table` and the `MakeForest` CHECKs in `catalog resolves every registered id`; `pipeline_tests` cases `forest path unchanged`, `forest no double bonus`. The CHECK drop in those two files is the reason for `allowCheckDrop`. No Ember assertion is weakened; `start state created once` still boots the Ember catalog. Removing `MakeForest`, `FGameMode` and `FPrototypeRun` is proven by the build (no caller may remain) and the §6 T5 symbol check.
- Timeouts: only the forest-only `TIMEOUT 90` override is deleted; the new tests add no ctest TIMEOUT (the script's own `execute_process(… TIMEOUT 30)` bounds them).

## 8. Decisions
- Resolved: removed switches (`--forest`, `--verify-prototype`, env `GAME01P_MODE=forest`) print `forest mode was removed` to stderr and exit 2, checked before engine init — because a silent fallback to Ember would hide stale scripts and CI steps. `--ember` stays an accepted no-op so existing smokes, README commands and user scripts keep working.
- Resolved: `FPrototypeRun` is deleted with no replacement — it has no Ember consumer. Its only references are `main.cpp` (load, verify, shutdown erase), the forest states, `PrototypeChecks`, and three test files (node_tree via `LoadPassiveTreeFile`, replaced by a test-local helper; state_machine and pipeline forest tests, deleted); the remaining mentions are comments (`FCombatSheet.h` ≈18, `FCombatSheet.cpp` ≈46), reworded in T4. `FPassiveStatBonuses` lives in `FPassiveTree.h`, not in `FPrototypeRun.h`. The `content` lookup in `main.cpp` existed only for the prototype load and goes with it; unusable content still stops the boot inside `FGameplayServices::Initialize`.
- Resolved: `FGameMode` is deleted, not kept as a one-value enum: its only readers were `main.cpp` and `GridCameraSystem`.
- Resolved: `FBattleLayout.h` (`BattleLayout`, `FitBattleCamera`) is deleted — no caller remains after the forest files go (verified: `PrototypeChecks.cpp`, `CombatHudSystem.h`, `FBattleState.cpp`, `GridCameraSystem.h` were the only includers). Keeping dead code would contradict the WP's goal.
- Resolved: `GridCameraSystem` keeps the `FInputState` early return (unchanged Ember behavior: no camera fit before input exists).
- Resolved: `windowHeight` default stays 800 (Ember's former value); the 720 forest height goes.
- Alternatives considered: rejecting via ctest `PASS_REGULAR_EXPRESSION` — rejected because it ignores the exit code; WILL_FAIL — rejected because any non-zero code (a crash) would pass.
- Defaults for anything unspecified: leave the code as it is.
- Out of scope: renaming `FForestScene`/`scene_forest.json`/`InitializeForestScene` (Ember's lit arena; separate WP if wanted); removing now-unused includes beyond those named in §5; `docs/` history mentioning forest.

## 9. Pitfalls
- WP-35 rewrites `CMakeLists.txt` (the smoke `ENVIRONMENT` line becomes `SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software`) and other WPs may shift lines: match the quoted text, never a line number. Leave WP-35's lines as they are.
- `main.cpp` aborts boot if `FPrototypeRun::Load` fails (M9): M9 lands in T2, before T5 deletes `prototype-run.json`. Do not reorder.
- `GridCameraSystem.h` must change in the same task as M5, or Ember uses the forest camera path for the rest of the WP.
- Deleting sources needs an explicit `cmake --preset windows-msvc-agent` before the build (T3), or MSBuild still lists the deleted files (C1083).
- Asset staging (`stage_assets`, POST_BUILD `copy_directory`) copies but never deletes: the staged `prototype-run.json`/`battlefield.json` survive until removed by hand (T5 command), and without that the F2 proof is void.
- One build/ctest at a time per worktree; an `MSB3073`/`MSB3075` DLL-copy failure is a transient lock — `build.mjs` already retries once.
- Manual game runs stay windowless (`SDL_VIDEO_DRIVER=dummy`) with `GAME01P_SAVE_DIR` under `out/`; never touch `%APPDATA%\Game01P`.
- If removing the `main.cpp` includes (M1) leaves a symbol undeclared (C3861/C2039/C2065), add `#include` of the header that declares it — find it with `git grep -n "<symbol>(" -- "src/*.h"`. Expected: none (verified: every remaining call's header is still included directly or via `BattleAnimationSystem.h` for `InitializeBattleAnim`).
- `python3` is a Windows Store stub: use `node` for JSON checks.
- Do not delete any file under `tests/` (gate rule `test-deleted`); only edit the five test files listed.

## 10. Open questions
None.
