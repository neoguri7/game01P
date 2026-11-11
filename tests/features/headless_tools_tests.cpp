// WP-00c (M3) tests: the windowless input script (parser + SDL injector), the injected SDL path under the
// dummy video driver + software renderer — into ImGui and into the game's FInputState (the click-timing
// rule keeps a HUD press out of the grid) — plus `expect state`, the autopilot policy parser, and the run
// autopilot driving a seeded run through reward/event/camp into dungeon 2 (twice, identical run-log events;
// plus an equip=auto drive that fits a picked rune).
//
// WP-00d tests (review P6): the engine-completing policy (equip=engine / reward=engine) on CONSTRUCTED run
// state — rule order (complete > start > fallback), host rejection (1-socket shove, power-0 cell fog),
// socket reservations, camp consolidation and its refusal, pre-resolve card scoring (an owned duplicate is
// a refund, score 0), arm classification reasons, determinism of two full drives, and the armed-slot →
// ignition bridge via CheckEngineIgnition (the drive itself cannot ignite: ForceRoomVictory resolves rooms
// without resolving actions, so OnHit never fires there).
//
// The run tests read the FROZEN FIXTURE (TestAssetRoots) and force every room victory — the autopilot is
// the unit under test, so no assertion may depend on a battle outcome (and therefore on ship balance;
// critic F1).
//
// WP-09h (M1a, review P2): the autopilot's event branch is gate-aware and two-stage — it picks the policy
// index, falls back to the first open choice when that gate is shut, and continues the resolved result
// screen on the next decision point (the decision key carries the event stage).
#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "core/AssetManager.h"
#include "core/Engine.h"
#include "core/EngineState.h"
#include "core/FFramePlan.h"
#include "core/InputState.h"
#include "core/Logger.h"
#include "dev/FEnginePolicy.h"
#include "dev/FInputScript.h"
#include "dev/FRunAutopilot.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/combat/FEngineIgnition.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FEventEffects.h"
#include "gameplay/dungeon/FEventSection.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"
#include "presentation/FCompendiumView.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FSettings.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberModalHostState.h"

namespace {

using game::dev::EAutopilotEquip;
using game::dev::EAutopilotStatus;
using game::dev::EEngineArm;
using game::dev::EEngineRule;
using game::dev::EScriptCommand;
using game::dev::EScriptError;
using game::dev::FInputScript;
using game::dev::FInputScriptFrame;
using game::dev::FRunAutopilot;
using game::dev::FScriptCommand;

// ---------------------------------------------------------------------------------------------
// Filesystem helpers (the content_a / compendium_tests recipe)
// ---------------------------------------------------------------------------------------------

/// Always the override CMake/build-test.ps1 set (WP-00b), never the user's real save.
[[nodiscard]] std::filesystem::path SaveDirRoot() {
    if (const char* overrideDir = std::getenv(game::kSettingsSaveDirEnv);
        overrideDir != nullptr && *overrideDir != '\0') {
        return std::filesystem::path(overrideDir);
    }
    return std::filesystem::current_path() / "out" / "test-saves" / "headless_tools_tests";
}

/// A process-unique suffix so two harnesses in one process never share a checkpoint or a log directory.
[[nodiscard]] std::string UniqueSuffix(std::string_view label) {
    static int counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string{label} + "-" + std::to_string(ticks) + "-" + std::to_string(counter++);
}

/// A private, freshly emptied subdirectory of the isolated save dir.
[[nodiscard]] std::filesystem::path FreshDir(const char* name) {
    const std::filesystem::path dir = SaveDirRoot() / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    CHECK(!error);
    return dir;
}

// ---------------------------------------------------------------------------------------------
// 1. Input script parser
// ---------------------------------------------------------------------------------------------

/// One line per command kind — comments (whole line + trailing), a blank line and CRLF endings included.
void ParsesEveryCommand() {
    const std::string text =
        "# every command kind, one line each\r\n"
        "\r\n"
        "1 move 10 20   # trailing comment\r\n"
        "2 click 30 40\r\n"
        "3 click 50 60 right\r\n"
        "4 click 70 80 middle\r\n"
        "5 down 90 100 right\r\n"
        "6 up 90 100 right\r\n"
        "7 key Escape\r\n"
        "8 key Left Shift\r\n"
        "9 key Return\r\n"
        "10 key Space\r\n"
        "11 key 1\r\n"
        "12 wheel -3\r\n"
        "13 capture out/cap with spaces/cap 1.png\r\n"
        "14 quit\r\n";
    const auto parsed = game::dev::ParseInputScript(text);
    CHECK(parsed.has_value());
    const std::vector<FScriptCommand>& commands = *parsed;
    CHECK(commands.size() == 14);
    for (std::size_t index = 0; index < commands.size(); ++index) {
        CHECK(commands[index].frame == static_cast<std::uint32_t>(index + 1));
    }

    CHECK(commands[0].kind == EScriptCommand::Move);
    CHECK(commands[0].x == 10.f && commands[0].y == 20.f);
    CHECK(commands[0].line == 3);  // 1-based: the comment and the blank line count

    CHECK(commands[1].kind == EScriptCommand::Click);
    CHECK(commands[1].x == 30.f && commands[1].y == 40.f);
    CHECK(commands[1].button == SDL_BUTTON_LEFT);  // no button word = left

    CHECK(commands[2].kind == EScriptCommand::Click && commands[2].button == SDL_BUTTON_RIGHT);
    CHECK(commands[3].kind == EScriptCommand::Click && commands[3].button == SDL_BUTTON_MIDDLE);

    CHECK(commands[4].kind == EScriptCommand::Down);
    CHECK(commands[4].x == 90.f && commands[4].y == 100.f && commands[4].button == SDL_BUTTON_RIGHT);
    CHECK(commands[5].kind == EScriptCommand::Up && commands[5].button == SDL_BUTTON_RIGHT);

    // Critic P1b: the keycode side of the name resolution (the scancode side is pinned in the injection test).
    CHECK(commands[6].kind == EScriptCommand::Key && commands[6].key == SDLK_ESCAPE);
    CHECK(commands[7].kind == EScriptCommand::Key && commands[7].key == SDLK_LSHIFT);  // a spaced key name
    CHECK(commands[8].kind == EScriptCommand::Key && commands[8].key == SDLK_RETURN);
    CHECK(commands[9].kind == EScriptCommand::Key && commands[9].key == SDLK_SPACE);
    CHECK(commands[10].kind == EScriptCommand::Key && commands[10].key == SDLK_1);

    CHECK(commands[11].kind == EScriptCommand::Wheel && commands[11].wheel == -3.f);

    CHECK(commands[12].kind == EScriptCommand::Capture);
    CHECK(commands[12].path == "out/cap with spaces/cap 1.png");

    CHECK(commands[13].kind == EScriptCommand::Quit);

    // Critic P7: `expect state <id>` — the app-flow check (path = the state id, line = the source line).
    const auto expectParsed = game::dev::ParseInputScript("3 expect state ember.camp\n4 quit\n");
    CHECK(expectParsed.has_value());
    CHECK(expectParsed->size() == 2);
    CHECK((*expectParsed)[0].kind == EScriptCommand::ExpectState);
    CHECK((*expectParsed)[0].frame == 3);
    CHECK((*expectParsed)[0].path == "ember.camp");
    CHECK((*expectParsed)[0].line == 1);
    CHECK((*expectParsed)[1].kind == EScriptCommand::Quit);
}

// ---------------------------------------------------------------------------------------------
// 2. Parser errors — one bad script per EScriptError
// ---------------------------------------------------------------------------------------------

void RejectsEachErrorKind() {
    const auto expectParseError = [](const std::string& text, EScriptError kind, int line) {
        const auto parsed = game::dev::ParseInputScript(text);
        CHECK(!parsed.has_value());
        CHECK(parsed.error().kind == kind);
        CHECK(parsed.error().line == line);
        CHECK(parsed.error().message.find("line " + std::to_string(line)) != std::string::npos);
    };

    expectParseError("x move 1 2", EScriptError::BadFrame, 1);  // non-integer frame
    expectParseError("0 quit", EScriptError::BadFrame, 1);      // frames are 1-based
    expectParseError("5 move 1 2\n4 move 3 4", EScriptError::FrameOrder, 2);
    expectParseError("1 teleport 1 2", EScriptError::UnknownCommand, 1);
    expectParseError("5 move 1", EScriptError::BadArguments, 1);
    expectParseError("5 click 1 2 sideways", EScriptError::BadButton, 1);
    expectParseError("5 key NoSuchKey", EScriptError::UnknownKey, 1);
    expectParseError("5 expect foo", EScriptError::BadArguments, 1);    // `expect` takes only `state <id>`
    expectParseError("5 expect state", EScriptError::BadArguments, 1);  // ... with a non-empty id

    // FileUnreadable has line 0 and names the path instead of a line ("input script: cannot read <path>").
    const std::filesystem::path missing = SaveDirRoot() / "definitely-missing-input-script.txt";
    std::error_code error;
    std::filesystem::remove(missing, error);
    const auto loaded = game::dev::LoadInputScript(missing.string());
    CHECK(!loaded.has_value());
    CHECK(loaded.error().kind == EScriptError::FileUnreadable);
    CHECK(loaded.error().line == 0);
    CHECK(loaded.error().message.find("cannot read") != std::string::npos);
    CHECK(loaded.error().message.find("definitely-missing-input-script.txt") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// 3. Injected SDL events reach ImGui under the dummy driver
// ---------------------------------------------------------------------------------------------

void InjectsClickIntoImGuiUnderDummyDriver() {
    // The same windowless pair game-headless.ps1 uses. SDL_SetHintWithPriority + SDL_HINT_OVERRIDE: a plain
    // SDL_SetHint returns false when the same env var is set (SDL3: the env var beats normal priority — the
    // critic's F2), and the driver CHECK below is the real guarantee.
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE);
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    CHECK(std::string(SDL_GetCurrentVideoDriver()) == "dummy");

    SDL_Window* window = SDL_CreateWindow("headless-tools", 1280, 800, 0);
    CHECK(window != nullptr);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "software");
    CHECK(renderer != nullptr);
    const SDL_WindowID windowId = SDL_GetWindowID(window);

    ImGuiContext* context = ImGui::CreateContext();
    CHECK(context != nullptr);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280.f, 800.f);
    CHECK(ImGui_ImplSDL3_InitForSDLRenderer(window, renderer));
    CHECK(ImGui_ImplSDLRenderer3_Init(renderer));

    std::uint32_t currentFrame = 0;
    bool buttonHovered = false;
    int clickCount = 0;
    int escapePressedFrame = 0;
    ImVec2 buttonMin{0.f, 0.f};
    ImVec2 buttonMax{0.f, 0.f};

    // The game-side input state, fed every polled event alongside ImGui — the engine path (processInput):
    // the begin-frame step first (uiCaptures* = the PREVIOUS ImGui frame's io flags), then per event
    // ImGui_ImplSDL3_ProcessEvent + FInputState::processEvent (EngineInputPipeline.cpp).
    game::FInputState input;
    std::vector<SDL_Scancode> keyDowns;  // the current frame's key-down scancodes (critic P1b capture)

    const auto drawButton = [&] {
        ImGui::SetNextWindowPos(ImVec2(100.f, 100.f));
        ImGui::SetNextWindowSize(ImVec2(300.f, 200.f));
        if (ImGui::Begin("HeadlessInject", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::Button("INJECT", ImVec2(120.f, 40.f))) {
                ++clickCount;
            }
            buttonHovered = ImGui::IsItemHovered();
            buttonMin = ImGui::GetItemRectMin();
            buttonMax = ImGui::GetItemRectMax();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) escapePressedFrame = static_cast<int>(currentFrame);
        }
        ImGui::End();
    };

    // One frame, in the engine's order: the begin-frame step (uiCaptures* = the previous ImGui frame's io
    // flags), inject this frame's script events, feed every SDL event to both the ImGui backend and the
    // game's FInputState, then the NewFrame order (SDL_Renderer3 → SDL3 → ImGui) and the draw.
    const auto pump = [&](FInputScript* script, std::uint32_t frameIndex, auto&& draw,
                          std::vector<SDL_Scancode>* keyDownSink = nullptr) {
        currentFrame = frameIndex;
        input.beginFrame(io.WantCaptureKeyboard, io.WantCaptureMouse);
        FInputScriptFrame injected;
        if (script != nullptr) injected = script->inject(frameIndex, windowId);
        if (keyDownSink != nullptr) keyDownSink->clear();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (keyDownSink != nullptr && event.type == SDL_EVENT_KEY_DOWN) {
                keyDownSink->push_back(event.key.scancode);
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
            input.processEvent(event);
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        return injected;
    };

    // Phase A — a fixed-position ImGui window with one button pins the four injection contracts directly.
    FInputScript warmup{std::vector<FScriptCommand>{}};
    pump(&warmup, 1, drawButton);
    pump(&warmup, 2, drawButton);
    CHECK(warmup.finished());
    CHECK(buttonMax.x > buttonMin.x && buttonMax.y > buttonMin.y);
    const ImVec2 center{(buttonMin.x + buttonMax.x) * .5f, (buttonMin.y + buttonMax.y) * .5f};

    const std::string centerText = std::format("{:.3f} {:.3f}", center.x, center.y);
    const std::string actText =
        "3 move 12 34\n4 move " + centerText + "\n6 click " + centerText + "\n8 key Escape\n9 quit\n";
    const auto parsedAct = game::dev::ParseInputScript(actText);
    CHECK(parsedAct.has_value());
    FInputScript act{*parsedAct};

    // (a) after a move frame the injected point is exactly io.MousePos — the dummy driver's backend never
    // overrides it with SDL_GetGlobalMouseState (MouseCanUseGlobalState is false off the whitelist).
    const FInputScriptFrame moved = pump(&act, 3, drawButton);
    CHECK(moved.pushed >= 1);
    CHECK(io.MousePos.x == 12.f && io.MousePos.y == 34.f);

    // (b) moving onto the button (no click) hovers it.
    pump(&act, 4, drawButton);
    CHECK(buttonHovered);

    // (c) a click at the CURRENT point: down at F (6), up at F+1 (7) — the button fires exactly once.
    pump(&act, 5, drawButton);
    CHECK(clickCount == 0);
    pump(&act, 6, drawButton);
    CHECK(io.MouseDown[0]);  // the down is this frame
    CHECK(clickCount == 0);
    pump(&act, 7, drawButton);
    CHECK(!io.MouseDown[0]);
    CHECK(clickCount == 1);

    // (d) the injected key reaches ImGui on its own frame and carries the game's scancode (critic P1b).
    pump(&act, 8, drawButton, &keyDowns);
    CHECK(keyDowns.size() == 1);
    CHECK(keyDowns[0] == SDL_SCANCODE_ESCAPE);
    CHECK(clickCount == 1);
    CHECK(escapePressedFrame == 8);

    const FInputScriptFrame finished = pump(&act, 9, drawButton);
    CHECK(finished.quit);
    CHECK(act.finished());

    // (e) a click from a NEW point: motion at F, down at F+1, up at F+2 (the header's click-timing rule) —
    // and the press must NOT reach the game: FInputState.uiCapturesMouse is the PREVIOUS frame's
    // io.WantCaptureMouse, which turned true when the pointer hovered this window during frame F's NewFrame,
    // so the down at F+1 is swallowed while ImGui still gets its click.
    const std::string tailText =
        "10 move 20 20\n"
        "11 click " + centerText + "\n"
        "15 move 20 20\n"
        "19 key Return\n"
        "21 key Space\n"
        "23 key 1\n"
        "26 expect state ember.camp\n"
        "27 quit\n";
    const auto parsedTail = game::dev::ParseInputScript(tailText);
    CHECK(parsedTail.has_value());
    FInputScript tail{*parsedTail};

    pump(&tail, 10, drawButton);  // pointer away: the previous frame's WantCaptureMouse is false
    CHECK(io.MousePos.x == 20.f && io.MousePos.y == 20.f);
    CHECK(!io.WantCaptureMouse);
    CHECK(!input.mouseLeftPressed);

    pump(&tail, 11, drawButton);  // F: the motion onto the button — no button event yet
    CHECK(io.MousePos.x >= buttonMin.x && io.MousePos.x <= buttonMax.x);
    CHECK(io.MousePos.y >= buttonMin.y && io.MousePos.y <= buttonMax.y);
    CHECK(buttonHovered);
    CHECK(!io.MouseDown[0]);  // the down was deferred to F+1
    CHECK(clickCount == 1);
    CHECK(!input.mouseLeftPressed);
    CHECK(io.WantCaptureMouse);  // hovered now, so the next frame's begin-frame captures the mouse

    pump(&tail, 12, drawButton);  // F+1: the deferred down — ImGui sees it, the game must not
    CHECK(io.MouseDown[0]);
    CHECK(clickCount == 1);
    CHECK(input.uiCapturesMouse);
    CHECK(!input.mouseLeftPressed);  // the old same-frame shape leaked here (shown in (f))

    pump(&tail, 13, drawButton);  // F+2: the deferred up — the button fires once
    CHECK(!io.MouseDown[0]);
    CHECK(clickCount == 2);
    CHECK(!input.mouseLeftPressed);

    pump(&tail, 14, drawButton);  // no command
    pump(&tail, 15, drawButton);  // pointer away again, for the (f) demo's begin-frame state
    CHECK(!io.WantCaptureMouse);

    // (f) the old same-frame shape (motion + down in one frame) with a false previous-frame capture WOULD
    // leak — the direct proof that (e)'s assertion is not vacuous (the critic's P2 hazard, shown).
    const auto pushMotion = [&](float x, float y) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = windowId;
        event.motion.x = x;
        event.motion.y = y;
        CHECK(SDL_PushEvent(&event));
    };
    const auto pushButton = [&](bool down, float x, float y) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = windowId;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down;
        event.button.x = x;
        event.button.y = y;
        CHECK(SDL_PushEvent(&event));
    };
    pushMotion(center.x, center.y);
    pushButton(true, center.x, center.y);
    pump(&tail, 16, drawButton);
    CHECK(clickCount == 2);         // not released yet
    CHECK(input.mouseLeftPressed);  // ...and the game would have acted on it
    pushButton(false, center.x, center.y);
    pump(&tail, 17, drawButton);
    CHECK(clickCount == 3);
    CHECK(!input.mouseLeftPressed);
    CHECK(!input.mouseLeftHeld);

    pump(&tail, 18, drawButton);  // no command

    // (g) critic P1b: the pushed key events carry the scancodes the game binds (InputState::ActionScancode:
    // Return = Confirm, Space = EndTurn, 1 = Skill1; Escape was checked at frame 8).
    pump(&tail, 19, drawButton, &keyDowns);
    CHECK(keyDowns.size() == 1);
    CHECK(keyDowns[0] == SDL_SCANCODE_RETURN);
    pump(&tail, 20, drawButton, &keyDowns);  // the key-up
    CHECK(keyDowns.empty());
    pump(&tail, 21, drawButton, &keyDowns);
    CHECK(keyDowns.size() == 1);
    CHECK(keyDowns[0] == SDL_SCANCODE_SPACE);
    pump(&tail, 22, drawButton, &keyDowns);
    CHECK(keyDowns.empty());
    pump(&tail, 23, drawButton, &keyDowns);
    CHECK(keyDowns.size() == 1);
    CHECK(keyDowns[0] == SDL_SCANCODE_1);
    pump(&tail, 24, drawButton, &keyDowns);
    CHECK(keyDowns.empty());

    // (h) critic P7: a due `expect state` is returned in `expects` (id + source line) and pushes no event.
    const FInputScriptFrame expectFrame = pump(&tail, 26, drawButton, &keyDowns);
    CHECK(expectFrame.expects.size() == 1);
    CHECK(expectFrame.expects[0].path == "ember.camp");
    CHECK(expectFrame.expects[0].line == 7);  // the tail script's 7th line
    CHECK(expectFrame.pushed == 0);
    CHECK(keyDowns.empty());
    const FInputScriptFrame tailQuit = pump(&tail, 27, drawButton, &keyDowns);
    CHECK(tailQuit.quit);
    CHECK(tail.finished());

    // Phase B — the real compendium modal (the ModalPopsOnClose recipe) through injected SDL events: an
    // injected click on its Close row must make the menu host pop.
    const std::filesystem::path compendiumDir = FreshDir("compendium-modal");
    entt::registry uiRegistry;
    game::FAssetManager::Initialize(uiRegistry, ShipAssetRoots());
    CHECK(game::gameplay::FGameplayServices::Initialize(uiRegistry));
    CHECK(game::InitializeDungeon(uiRegistry, (compendiumDir / "checkpoint.json").string()));
    auto& uiWindow = uiRegistry.ctx().emplace<game::FEngineWindowState>();
    uiWindow.width = 1280;
    uiWindow.height = 800;
    game::InitializeDungeonUi(uiRegistry);
    CHECK(game::FindModalView(uiRegistry, game::kCompendiumModalId) != nullptr);  // the Pop below is the click's

    const auto drawCompendium = [&uiRegistry] { game::RenderCompendiumView(uiRegistry); };
    FInputScript settle{std::vector<FScriptCommand>{}};
    pump(&settle, 30, drawCompendium);
    pump(&settle, 31, drawCompendium);
    pump(&settle, 32, drawCompendium);

    // The Close button is the last row of the popup: full content width, 40 px tall, one padding above the
    // popup's bottom edge (compendium_tests::ModalPopsOnClose computes the same point).
    ImGuiWindow* popup = ImGui::FindWindowByName("Compendium");
    CHECK(popup != nullptr);
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 closeCenter{popup->Pos.x + popup->Size.x * .5f,
                             popup->Pos.y + popup->Size.y - style.WindowPadding.y - 20.f};
    const std::string closePoint = std::format("{:.3f} {:.3f}", closeCenter.x, closeCenter.y);
    const std::string closeText = "33 move " + closePoint + "\n34 click " + closePoint + "\n36 quit\n";
    const auto parsedClose = game::dev::ParseInputScript(closeText);
    CHECK(parsedClose.has_value());
    FInputScript closeScript{*parsedClose};
    pump(&closeScript, 33, drawCompendium);
    pump(&closeScript, 34, drawCompendium);
    pump(&closeScript, 35, drawCompendium);
    pump(&closeScript, 36, drawCompendium);
    CHECK(closeScript.finished());

    FEmberModalHostState host(std::string(game::kCompendiumModalId), EHostMode::Menu);
    const FStateTransition transition = host.Update(uiRegistry, 1.f / 60.f);
    CHECK(transition.kind == EStateChangeKind::Pop);

    // Clean shutdown in reverse order.
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::error_code cleanup;
    std::filesystem::remove_all(compendiumDir, cleanup);
}

// ---------------------------------------------------------------------------------------------
// 4. Autopilot policy parser
// ---------------------------------------------------------------------------------------------

void ParsesAutopilotPolicy() {
    // The default string mirrors the M-FUN harness: card 0, first event option, chest on even dungeons,
    // buy the camp offer, one upgrade, then continue.
    const auto defaults = game::dev::ParseAutopilotPolicy(game::dev::kDefaultAutopilotPolicy);
    CHECK(defaults.has_value());
    CHECK(defaults->rewardCard == 0);
    CHECK(!defaults->rewardSkip);
    CHECK(defaults->eventOption == 0);
    CHECK(defaults->chestOnEvenDungeons);
    CHECK(defaults->campBuyOffer);
    CHECK(defaults->campUpgrades == 1);
    CHECK(defaults->campContinue);
    CHECK(defaults->equip == EAutopilotEquip::Off);  // WP-27: off unless written (the default keeps the M-FUN design)
    CHECK(!defaults->rewardEngine);                  // WP-00d: reward=engine is opt-in too

    // continue is present only when written; tokens left out take the default policy's value.
    const auto custom = game::dev::ParseAutopilotPolicy("reward=skip,event=second,chest=never,camp=+2");
    CHECK(custom.has_value());
    CHECK(custom->rewardSkip);
    CHECK(custom->eventOption == 1);
    CHECK(!custom->chestOnEvenDungeons);
    CHECK(!custom->campBuyOffer);
    CHECK(custom->campUpgrades == 2);
    CHECK(!custom->campContinue);
    CHECK(custom->equip == EAutopilotEquip::Off);

    // Whitespace around tokens is trimmed; camp=offer+1 buys the offer and one upgrade.
    const auto spaced = game::dev::ParseAutopilotPolicy(" reward=card1 , camp=offer+1 , continue ");
    CHECK(spaced.has_value());
    CHECK(spaced->rewardCard == 1);
    CHECK(spaced->campBuyOffer);
    CHECK(spaced->campUpgrades == 1);
    CHECK(spaced->campContinue);

    // WP-27 → WP-00d: equip=off | auto | engine (the bool became the policy enum).
    const auto equipAuto = game::dev::ParseAutopilotPolicy("equip=auto");
    CHECK(equipAuto.has_value());
    CHECK(equipAuto->equip == EAutopilotEquip::Auto);
    const auto equipOff = game::dev::ParseAutopilotPolicy("equip=off");
    CHECK(equipOff.has_value());
    CHECK(equipOff->equip == EAutopilotEquip::Off);
    const auto equipEngine = game::dev::ParseAutopilotPolicy("equip=engine");
    CHECK(equipEngine.has_value());
    CHECK(equipEngine->equip == EAutopilotEquip::Engine);

    // WP-00d: reward=engine — the pre-resolve card rule; rewardCard/Skip keep their defaults.
    const auto rewardEngine = game::dev::ParseAutopilotPolicy("reward=engine");
    CHECK(rewardEngine.has_value());
    CHECK(rewardEngine->rewardEngine);
    CHECK(!rewardEngine->rewardSkip);
    CHECK(rewardEngine->rewardCard == 0);
    CHECK(rewardEngine->equip == EAutopilotEquip::Off);

    // The full engine policy text the wave-QA script builds (review P5) parses into the expected fields.
    const auto enginePolicy = game::dev::ParseAutopilotPolicy(
        "reward=engine,event=first,chest=open,camp=offer+1,continue,equip=engine");
    CHECK(enginePolicy.has_value());
    CHECK(enginePolicy->rewardEngine);
    CHECK(enginePolicy->equip == EAutopilotEquip::Engine);
    CHECK(enginePolicy->campContinue && enginePolicy->campBuyOffer && enginePolicy->campUpgrades == 1);

    const auto expectPolicyError = [](const std::string& text) {
        const auto parsed = game::dev::ParseAutopilotPolicy(text);
        CHECK(!parsed.has_value());
        CHECK(!parsed.error().empty());
    };
    expectPolicyError("warp=9");                    // unknown token
    expectPolicyError("reward=skip,reward=card1");  // duplicate token
    expectPolicyError("reward=cardX");              // bad reward value
    expectPolicyError("camp=offer+x");              // bad camp spec
    expectPolicyError("");                          // empty policy
    expectPolicyError("equip=yes");                  // bad equip value
    expectPolicyError("equip=engines");              // bad equip value (review P6)
    expectPolicyError("equip=auto,equip=off");       // duplicate token
    expectPolicyError("reward=engine,reward=card0");  // duplicate token (both write `reward`)
}

// ---------------------------------------------------------------------------------------------
// 5./6. Run autopilot (the content_a FDungeonHarness recipe, test-local)
// ---------------------------------------------------------------------------------------------

/// A booted ember dungeon on the FROZEN FIXTURE content (critic F1: TestAssetRoots — never ship balance) plus
/// a private run-log directory so two runs of the same seed never share log files (the file-id counter would
/// make their log ids differ).
struct FHeadlessRunHarness {
    std::filesystem::path checkpoint;
    std::filesystem::path runSave;
    std::filesystem::path logDir;
    entt::registry registry;

    explicit FHeadlessRunHarness(std::uint32_t seed, std::string_view label) {
        checkpoint = SaveDirRoot() / ("headless-tools-" + UniqueSuffix(label) + ".json");
        logDir = SaveDirRoot() / ("headless-logs-" + UniqueSuffix(label));
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        game::InitializeRunLog(registry, logDir.string());
        if (const auto* save = registry.ctx().find<game::FDungeonSave>()) runSave = save->path;
        game::StartDungeonRun(registry, seed, true);
    }

    ~FHeadlessRunHarness() {
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(checkpoint.string() + ".run.json", error);
        std::filesystem::remove(checkpoint.string() + ".run.json.tmp", error);
        if (!runSave.empty()) {
            std::filesystem::remove(runSave, error);
            std::filesystem::remove(runSave.string() + ".tmp", error);
        }
        std::filesystem::remove_all(logDir, error);
    }

    FHeadlessRunHarness(const FHeadlessRunHarness&) = delete;
    FHeadlessRunHarness& operator=(const FHeadlessRunHarness&) = delete;

    /// Sets up the current room the way the game does (FEmberBattleState::OnEnter builds the battle scene,
    /// the party entities included) and resolves it through the real flow with a FORCED victory, then exits.
    /// The autopilot is the unit under test, not combat: forcing the result is what keeps the run
    /// deterministic on the frozen fixture instead of depending on a battle outcome a balance change could
    /// flip (critic F1). The resolution runs while the battle is still entered — the game's order
    /// (FEmberBattleState::Update ~:240) — so CaptureRunParty captures the real party and its skill slots.
    void ForceRoomVictory() {
        FEmberBattleState battle;
        battle.OnEnter(registry);
        const auto resolved = game::ResolveRoomVictory(registry, true);
        CHECK(resolved.has_value());
        battle.OnExit(registry);
    }
};

/// Decision/battle steps that cover dungeon 1 and the descent into dungeon 2 (~10 in practice).
constexpr int kAutopilotStepGuard = 40;

/// The run log without its environment-dependent fields: `log_session` (wall-clock `log_started_at`), the
/// wall-clock `ms` and engine-frame `frame` keys FRunLog.cpp writes on run_start/room_start, and the
/// run_end fields derived from them. Each harness gets a private log directory, so `log_id` is the same
/// ("run-<seed>-1") for both runs and stays in the comparison.
std::vector<nlohmann::json> NormalizedRunLogEvents(const game::FRunLog& log) {
    std::vector<nlohmann::json> events;
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        const auto type = event.find("t");
        if (type == event.end() || !type->is_string()) continue;
        const std::string& name = type->get_ref<const std::string&>();
        if (name == "log_session") continue;
        nlohmann::json copy = event;
        copy.erase("ms");
        copy.erase("frame");
        if (name == "run_end") {
            copy.erase("frames_to_first_battle");
            copy.erase("ms_to_first_battle");
        }
        events.push_back(std::move(copy));
    }
    return events;
}

/// WP-27's successful equip line: {"t":"loadout",...,"rune":<id>,"action":"equip"} (FDungeonRun.cpp ~:943).
[[nodiscard]] bool HasLoadoutEquip(const game::FRunLog& log) {
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        if (event.value("t", std::string{}) == "loadout" && event.value("action", std::string{}) == "equip") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int CountLoadoutEquips(const std::vector<nlohmann::json>& events) {
    int count = 0;
    for (const auto& event : events) {
        if (event.value("t", std::string{}) == "loadout" && event.value("action", std::string{}) == "equip") ++count;
    }
    return count;
}

/// The rune id of the first loadout/equip line, "" when the log has none.
[[nodiscard]] std::string FirstEquipRuneId(const std::vector<nlohmann::json>& events) {
    for (const auto& event : events) {
        if (event.value("t", std::string{}) == "loadout" && event.value("action", std::string{}) == "equip") {
            return event.value("rune", std::string{});
        }
    }
    return {};
}

/// Runes fitted into the run party's skill slots (FRunMember::skills -> FSkillSlot::runeIds).
[[nodiscard]] int FittedRuneCount(const std::vector<game::FRunMember>& party) {
    int count = 0;
    for (const auto& member : party) {
        for (const auto& slot : member.skills) {
            count += static_cast<int>(slot.runeIds.size());
        }
    }
    return count;
}

[[nodiscard]] bool PartyHasRune(const std::vector<game::FRunMember>& party, std::string_view runeId) {
    for (const auto& member : party) {
        for (const auto& slot : member.skills) {
            for (const auto& fitted : slot.runeIds) {
                if (std::string_view{fitted} == runeId) return true;
            }
        }
    }
    return false;
}

struct FAutopilotDrive {
    std::vector<nlohmann::json> events;
    std::vector<game::FRunMember> party;  // snapshot taken before the harness (and its run) is destroyed
    std::string equipRune;                // the rune of the first loadout/equip line, "" when none
    int roomsCompleted{0};
    int campsVisited{0};
    int rewardsSeen{0};
    int fittedRunes{0};
    int loadoutEquips{0};
    int steps{0};
    long long elapsedMs{0};
    bool reachedDungeonTwo{false};
    bool equipped{false};  // stopped at the first successful equip
    bool finished{false};  // the autopilot reported Finished (run end or room limit)
    bool stuck{false};
};

/// Where a drive stops.
enum class EDriveStop : std::uint8_t {
    DungeonTwo,  // dungeon 2's first battle is on screen (the WP-00c acceptance drive)
    FirstEquip,  // the run log has its first successful rune equip (the equip=auto drive)
};

/// Drives one seeded fixture-content run with `policyText`: every battle room is set up and forced to victory
/// (ForceRoomVictory), then the modal host consumes the autopilot's posted intent exactly like the game
/// (ApplyRunIntent). `stop` picks the success condition.
FAutopilotDrive DriveAutopilot(std::uint32_t seed, std::string_view label, std::string_view policyText,
                               EDriveStop stop) {
    FAutopilotDrive drive;
    const auto started = std::chrono::steady_clock::now();
    const auto policy = game::dev::ParseAutopilotPolicy(policyText);
    CHECK(policy.has_value());
    FHeadlessRunHarness harness{seed, label};
    FRunAutopilot pilot{*policy};
    for (; drive.steps < kAutopilotStepGuard; ++drive.steps) {
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        if (stop == EDriveStop::FirstEquip && HasLoadoutEquip(harness.registry.ctx().get<game::FRunLog>())) {
            drive.equipped = true;
            break;
        }
        if (run.depth == 2 && run.phase == game::EDungeonPhase::Battle) {
            drive.reachedDungeonTwo = true;
            break;
        }
        if (run.phase == game::EDungeonPhase::Battle) {
            harness.ForceRoomVictory();
            continue;
        }
        if (run.phase == game::EDungeonPhase::Camp) ++drive.campsVisited;
        if (run.phase == game::EDungeonPhase::Reward) ++drive.rewardsSeen;
        const std::string top = game::RunModalIdForPhase(run.phase);
        CHECK(!top.empty());
        const EAutopilotStatus status = pilot.tick(harness.registry, top);
        if (status == EAutopilotStatus::Stuck) {
            drive.stuck = true;
            break;
        }
        if (status == EAutopilotStatus::Finished) {
            drive.finished = true;
            break;
        }
        FEmberModalHostState host(top, EHostMode::RunPhase);
        host.Update(harness.registry, 1.f / 60.f);
    }
    const auto& run = harness.registry.ctx().get<game::FDungeonRun>();
    drive.party = run.party;
    drive.fittedRunes = FittedRuneCount(drive.party);
    drive.roomsCompleted = pilot.roomsCompleted();
    drive.elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    drive.events = NormalizedRunLogEvents(harness.registry.ctx().get<game::FRunLog>());
    drive.loadoutEquips = CountLoadoutEquips(drive.events);
    drive.equipRune = FirstEquipRuneId(drive.events);
    const char* stopReason = "guard";
    if (drive.stuck) stopReason = "stuck";
    else if (drive.equipped) stopReason = "first-equip";
    else if (drive.reachedDungeonTwo) stopReason = "dungeon2";
    else if (drive.finished) stopReason = "run-end";
    std::cout << "[report] autopilot '" << label << "' seed " << seed << ": " << drive.elapsedMs << " ms, "
              << drive.steps << " steps, rooms " << drive.roomsCompleted << ", camps " << drive.campsVisited
              << ", rewards " << drive.rewardsSeen << ", fitted " << drive.fittedRunes << ", equip lines "
              << drive.loadoutEquips << ", stop=" << stopReason << '\n';
    return drive;
}

void AutopilotDrivesSeededRunIntoDungeonTwo() {
    // Seed 469 keeps the drive reproducible; on the frozen fixture the run shape is fixed and every victory is
    // forced (critic F1), so the assertion never rides on shipped balance — the ship run stays with
    // mfun-headless.ps1 / the exe acceptance proof.
    const FAutopilotDrive first =
        DriveAutopilot(469u, "seed-469-a", game::dev::kDefaultAutopilotPolicy, EDriveStop::DungeonTwo);
    CHECK(!first.stuck);
    CHECK(first.reachedDungeonTwo);
    CHECK(first.roomsCompleted >= 1);  // at least one reward was handled
    CHECK(first.campsVisited >= 1);
    CHECK(first.rewardsSeen >= 1);

    // The same policy + seed in a fresh registry must produce the same run-log event sequence.
    const FAutopilotDrive second =
        DriveAutopilot(469u, "seed-469-b", game::dev::kDefaultAutopilotPolicy, EDriveStop::DungeonTwo);
    CHECK(!second.stuck);
    CHECK(second.reachedDungeonTwo);
    CHECK(first.events.size() == second.events.size());
    std::size_t divergence = first.events.size();
    for (std::size_t index = 0; index < first.events.size() && index < second.events.size(); ++index) {
        if (first.events[index] != second.events[index]) {
            divergence = index;
            break;
        }
    }
    if (divergence != first.events.size()) {
        std::cout << "[report] run logs diverge at event " << divergence << "\n  a: "
                  << first.events[divergence].dump() << "\n  b: " << second.events[divergence].dump() << '\n';
    }
    CHECK(divergence == first.events.size());
}

/// WP-27 equip=auto on the frozen fixture: every offered candidate is legal (the offer gate and the equip
/// validator use the same base-skill tag rule) and dungeon 1's pity (threshold 2, three battle rooms) forces
/// at least one offer, so the first fit is guaranteed — asserted unconditionally (critic F5). The autopilot
/// fits the picked rune into the first legal socket, so a real party skill slot carries it (captured before
/// OnExit) and the run log has its `loadout` line.
void AutopilotEquipFitsPickedRune() {
    const std::string policyText = std::string(game::dev::kDefaultAutopilotPolicy) + ",equip=auto";
    const FAutopilotDrive drive = DriveAutopilot(469u, "seed-469-equip", policyText, EDriveStop::FirstEquip);
    CHECK(!drive.stuck);
    CHECK(drive.equipped);  // the fixture composition fixes the offer and the fit
    CHECK(drive.fittedRunes >= 1);
    CHECK(drive.loadoutEquips >= 1);
    CHECK(!drive.equipRune.empty());
    // The log's equip line and the run party agree: the logged rune sits in a real skill slot.
    CHECK(PartyHasRune(drive.party, drive.equipRune));
}

void AutopilotStopsAtRoomLimit() {
    const auto policy = game::dev::ParseAutopilotPolicy(game::dev::kDefaultAutopilotPolicy);
    CHECK(policy.has_value());
    FHeadlessRunHarness harness{469u, "room-limit"};
    FRunAutopilot pilot{*policy, 1};

    // Room 1: set up the room and force the victory through the real resolution path.
    harness.ForceRoomVictory();
    auto& run = harness.registry.ctx().get<game::FDungeonRun>();
    CHECK(run.phase == game::EDungeonPhase::Reward);

    // The reward is handled (one ContinueReward posted) but the modal is still open: the room limit must
    // not fire before the host consumed the intent.
    CHECK(pilot.tick(harness.registry, std::string(game::kRunModalReward)) == EAutopilotStatus::Running);
    CHECK(pilot.roomsCompleted() == 1);

    FEmberModalHostState host(std::string(game::kRunModalReward), EHostMode::RunPhase);
    host.Update(harness.registry, 1.f / 60.f);  // consumes the posted intent through ApplyRunIntent
    CHECK(run.phase != game::EDungeonPhase::Reward);  // the reward modal has closed

    // The next tick at the new decision point reports Finished.
    const std::string nextTop = run.phase == game::EDungeonPhase::Battle
                                    ? std::string(kEmberBattleId)
                                    : game::RunModalIdForPhase(run.phase);
    CHECK(pilot.tick(harness.registry, nextTop) == EAutopilotStatus::Finished);
}

// ---------------------------------------------------------------------------------------------
// 7. WP-00d: the engine-completing rune policy — equip=engine / reward=engine (review P6 spec)
// ---------------------------------------------------------------------------------------------

/// Captures spdlog output while alive with the message-only pattern (`%v`) — one line per LOG_ call, so
/// the `[autopilot]` decision lines compare as exact strings (camp_tests' FLogCapture + the pattern pin).
struct FLogCapture {
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger;

    FLogCapture() {
        logger = std::make_shared<spdlog::logger>(
            "engine-policy-capture", std::make_shared<spdlog::sinks::ostream_sink_mt>(stream));
        logger->set_level(spdlog::level::trace);
        logger->set_pattern("%v");
        SetCurrentGameLogger(logger);
    }

    ~FLogCapture() { SetCurrentGameLogger(nullptr); }

    [[nodiscard]] std::string Text() const { return stream.str(); }
};

/// The captured lines containing `needle` (the capture is one message per newline-terminated line).
[[nodiscard]] std::vector<std::string> LinesContaining(const std::string& text, std::string_view needle) {
    std::vector<std::string> lines;
    std::istringstream input{text};
    std::string line;
    while (std::getline(input, line)) {
        if (line.find(needle) != std::string::npos) lines.push_back(line);
    }
    return lines;
}

/// CHECK a capture contains `needle`, printing the whole capture first when it does not (diagnostics must
/// land before the throwing CHECK — nothing after a failed CHECK in the same case runs).
void CheckContains(const std::string& text, std::string_view needle, int line) {
    if (text.find(needle) != std::string::npos) return;
    std::cout << "[report] expected '" << needle << "' in captured log:\n" << text << '\n';
    game_test::Check(false, "captured log contains the expected line", line);
}

/// The WP-00d fixture layout for constructed states — m0 warrior, m1 archer, m2 mage, m3 healer (the
/// fixture party contract), every slot empty: each test attaches exactly the runes it needs.
[[nodiscard]] std::vector<game::FSkillSlot> FixtureMemberSkills(std::size_t memberIndex) {
    switch (memberIndex) {
        case 0:
            return {{"skill.strike", {}}, {"skill.cleave", {}}, {"skill.lunge", {}}};
        case 1:
            return {{"skill.arrow", {}}, {"skill.volley", {}}, {"skill.snipe", {}}};
        case 2:
            return {{"skill.bolt", {}}, {"skill.fog", {}}, {"skill.heal", {}},
                    {"skill.buff", {}}, {"skill.debuff", {}}, {"skill.firebolt", {}}};
        default:
            return {{"skill.heal", {}}, {"skill.bolt", {}}, {"skill.buff", {}}};
    }
}

/// Constructed-state recipe (review P6): boot, force ONE room victory (the party capture reads the real
/// battle entities), then OVERWRITE the run state — explicit fixture layout, empty pool, wanted phase,
/// closed offer. Every test sets its own offer/pool/slot runes; nothing depends on the offer RNG.
void SetupConstructedRun(FHeadlessRunHarness& harness, game::EDungeonPhase phase) {
    harness.ForceRoomVictory();
    auto& run = harness.registry.ctx().get<game::FDungeonRun>();
    CHECK(run.party.size() == 4);
    for (std::size_t memberIndex = 0; memberIndex < run.party.size(); ++memberIndex) {
        run.party[memberIndex].skills = FixtureMemberSkills(memberIndex);
    }
    run.runes.clear();
    run.phase = phase;
    harness.registry.ctx().get<game::FRewards>().offer = game::FRuneOffer{};
}

/// One reward decision: opens `candidates` as the offer and ticks once on the reward modal with
/// `policyText` (the tick resolves the pick and fits it under the policy; the posted ContinueReward intent
/// stays on the context — the modal host is not needed for the state assertions).
void RewardTick(FHeadlessRunHarness& harness, std::string_view policyText,
                const std::vector<std::string>& candidates) {
    harness.registry.ctx().get<game::FRewards>().offer = game::FRuneOffer{true, candidates};
    const auto policy = game::dev::ParseAutopilotPolicy(policyText);
    CHECK(policy.has_value());
    game::dev::FRunAutopilot pilot{*policy};
    CHECK(pilot.tick(harness.registry, std::string(game::kRunModalReward)) == EAutopilotStatus::Running);
}

/// The rune ids fitted in one run member slot.
[[nodiscard]] std::vector<std::string> SlotRunes(const game::FDungeonRun& run, std::size_t member,
                                                 std::size_t slot) {
    return run.party[member].skills[slot].runeIds;
}

/// Every fitted rune plus the pool — the consolidation refusal's byte-identical comparison.
[[nodiscard]] std::string LoadoutSignature(const game::FDungeonRun& run) {
    std::string signature;
    for (std::size_t member = 0; member < run.party.size(); ++member) {
        for (std::size_t slot = 0; slot < run.party[member].skills.size(); ++slot) {
            signature += std::format("{}:{}=", member, slot);
            for (const std::string& rune : run.party[member].skills[slot].runeIds) {
                signature += rune + ',';
            }
            signature += ';';
        }
    }
    signature += "pool=";
    for (const std::string& rune : run.runes) signature += rune + ',';
    return signature;
}

/// The run-log events of one `t` type (the ignition bridge reads `engine_ignition`, the camp test `loadout`).
[[nodiscard]] std::vector<nlohmann::json> RunLogEventsOf(const entt::registry& registry, std::string_view type) {
    std::vector<nlohmann::json> events;
    const auto* log = registry.ctx().find<game::FRunLog>();
    if (log == nullptr) return events;
    for (const auto& event : log->events) {
        if (event.is_object() && event.value("t", std::string{}) == std::string{type}) {
            events.push_back(event);
        }
    }
    return events;
}

/// `loadout` events with this action and rune id (the camp move's remove + equip pair).
[[nodiscard]] int CountLoadoutEvents(const std::vector<nlohmann::json>& events, std::string_view action,
                                     std::string_view runeId) {
    int count = 0;
    for (const auto& event : events) {
        if (event.value("action", std::string{}) == std::string{action} &&
            event.value("rune", std::string{}) == std::string{runeId}) {
            ++count;
        }
    }
    return count;
}

/// Test 2: rule 1 completes a half-pair in place. (a) fire on m0 s0 Strike → ember completes there;
/// (b) fire on m3 s1 Bolt proves the policy differs from plain auto (which fits the first legal socket,
/// m2 s5 Firebolt — not the half pair).
void EnginePolicyCompletesHalfPair() {
    {
        FHeadlessRunHarness harness{469u, "engine-complete-strike"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.fire"};
        FLogCapture capture;
        RewardTick(harness, "reward=card0,camp=none,equip=engine", {"rune.ember"});
        CHECK((SlotRunes(run, 0, 0) == std::vector<std::string>{"rune.fire", "rune.ember"}));
        CHECK(run.runes.empty());  // granted by the resolve, then fitted — not left in the pool
        CheckContains(capture.Text(),
                      "[autopilot] equip rune=rune.ember member=0 slot=0 skill=skill.strike rule=complete "
                      "engine=engine.ember",
                      __LINE__);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-complete-bolt"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[3].skills[1].runeIds = {"rune.fire"};
        FLogCapture capture;
        RewardTick(harness, "reward=card0,camp=none,equip=engine", {"rune.ember"});
        CHECK((SlotRunes(run, 3, 1) == std::vector<std::string>{"rune.fire", "rune.ember"}));
        CheckContains(capture.Text(), "member=3 slot=1 skill=skill.bolt rule=complete engine=engine.ember",
                      __LINE__);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-complete-bolt-auto"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[3].skills[1].runeIds = {"rune.fire"};
        FLogCapture capture;
        RewardTick(harness, "reward=card0,camp=none,equip=auto", {"rune.ember"});
        CHECK((SlotRunes(run, 3, 1) == std::vector<std::string>{"rune.fire"}));   // auto does not complete
        CHECK((SlotRunes(run, 2, 5) == std::vector<std::string>{"rune.ember"}));  // first legal socket
        CHECK(capture.Text().find("rule=complete") == std::string::npos);
        CheckContains(capture.Text(), "member=2 slot=5 skill=skill.firebolt", __LINE__);
    }
}

/// Test 3: rule 2 starts a pair, and only on a HOST — 1-socket shove and power-0 cell fog are rejected, so
/// fire starts on m0 s2 Lunge. Plain auto takes m0 s0 (shove) instead: the walk, not the pair.
void EnginePolicyStartsOnHostOnly() {
    {
        FHeadlessRunHarness harness{469u, "engine-start-lunge"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0] = game::FSkillSlot{"skill.shove", {}};
        run.party[0].skills[1] = game::FSkillSlot{"skill.fog", {}};

        const std::optional<game::dev::FEnginePlacement> placement =
            game::dev::ChooseEnginePlacement(harness.registry, "rune.fire");
        CHECK(placement.has_value());
        CHECK(placement->member == 0);
        CHECK(placement->slot == 2);
        CHECK(placement->rule == EEngineRule::Start);
        CHECK(placement->engineId == "engine.ember");

        FLogCapture capture;
        RewardTick(harness, "reward=card0,camp=none,equip=engine", {"rune.fire"});
        CHECK((SlotRunes(run, 0, 2) == std::vector<std::string>{"rune.fire"}));
        CheckContains(capture.Text(), "member=0 slot=2 skill=skill.lunge rule=start engine=engine.ember",
                      __LINE__);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-start-shove-auto"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0] = game::FSkillSlot{"skill.shove", {}};
        run.party[0].skills[1] = game::FSkillSlot{"skill.fog", {}};
        RewardTick(harness, "reward=card0,camp=none,equip=auto", {"rune.fire"});
        CHECK((SlotRunes(run, 0, 0) == std::vector<std::string>{"rune.fire"}));
        CHECK(run.party[0].skills[2].runeIds.empty());
    }
}

/// Test 4: rule 3 keeps a reservation — Strike [power, fire] reserves its last socket for ember, so swift
/// (no engine) goes to the next unreserved slot (m0 s1 Cleave), rule=fallback. Plain auto consumes the
/// reserved socket instead.
void EnginePolicyFallbackKeepsReservation() {
    {
        FHeadlessRunHarness harness{469u, "engine-reserve-fallback"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.power", "rune.fire"};
        FLogCapture capture;
        RewardTick(harness, "reward=card0,camp=none,equip=engine", {"rune.swift"});
        CHECK((SlotRunes(run, 0, 0) == std::vector<std::string>{"rune.power", "rune.fire"}));
        CHECK((SlotRunes(run, 0, 1) == std::vector<std::string>{"rune.swift"}));
        CheckContains(capture.Text(), "member=0 slot=1 skill=skill.cleave rule=fallback", __LINE__);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-reserve-auto"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.power", "rune.fire"};
        RewardTick(harness, "reward=card0,camp=none,equip=auto", {"rune.swift"});
        CHECK((SlotRunes(run, 0, 0) == std::vector<std::string>{"rune.power", "rune.fire", "rune.swift"}));
        CHECK(run.party[0].skills[1].runeIds.empty());
    }
}

/// Test 5: camp consolidation (P4) — a split pair fuses into one host slot with one remove+equip move and
/// the loadout events prove both edits; a pooled partner is re-fitted by the retry step first.
void CampConsolidationMovesSplitPair() {
    {
        FHeadlessRunHarness harness{469u, "engine-camp-move"};
        SetupConstructedRun(harness, game::EDungeonPhase::Camp);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.fire"};
        run.party[2].skills[5].runeIds = {"rune.ember"};
        FLogCapture capture;
        CHECK(game::dev::ConsolidateEnginesAtCamp(harness.registry) == 1);
        CHECK(run.party[0].skills[0].runeIds.empty());
        CHECK((SlotRunes(run, 2, 5) == std::vector<std::string>{"rune.ember", "rune.fire"}));
        const auto loadouts = RunLogEventsOf(harness.registry, "loadout");
        CHECK(CountLoadoutEvents(loadouts, "remove", "rune.fire") == 1);
        CHECK(CountLoadoutEvents(loadouts, "equip", "rune.fire") == 1);
        CheckContains(capture.Text(),
                      "[autopilot] camp move rune=rune.fire from=0/0 to=2/5 engine=engine.ember", __LINE__);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-camp-retry"};
        SetupConstructedRun(harness, game::EDungeonPhase::Camp);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.fire"};
        run.runes = {"rune.ember"};
        CHECK(game::dev::ConsolidateEnginesAtCamp(harness.registry) == 0);  // (a) fitted it; (b) nothing to move
        CHECK((SlotRunes(run, 0, 0) == std::vector<std::string>{"rune.fire", "rune.ember"}));
        CHECK(run.runes.empty());
    }
}

/// WP-43 F13a: the `rest=<P>` token parses 0..100, defaults to 0, rejects bad values and duplicates.
void RestTokenParses() {
    const auto parsed = game::dev::ParseAutopilotPolicy("rest=60");
    CHECK(parsed.has_value());
    if (parsed) CHECK(parsed->campRestBelow == 60);
    const auto fallback = game::dev::ParseAutopilotPolicy(game::dev::kDefaultAutopilotPolicy);
    CHECK(fallback.has_value());
    if (fallback) CHECK(fallback->campRestBelow == 0);
    for (const char* bad : {"rest=101", "rest=x", "rest=", "rest=10,rest=20"}) {
        CHECK(!game::dev::ParseAutopilotPolicy(bad).has_value());
    }
}

/// WP-43 F13b: rest only below the threshold, only when the rest validates.
void AutopilotRestsBelowThreshold() {
    FHeadlessRunHarness harness{469u, "autopilot-rest-threshold"};
    SetupConstructedRun(harness, game::EDungeonPhase::Camp);
    auto& run = harness.registry.ctx().get<game::FDungeonRun>();
    const auto policy = game::dev::ParseAutopilotPolicy("rest=50");
    CHECK(policy.has_value());
    if (!policy) return;
    const auto zero = game::dev::ParseAutopilotPolicy("rest=0");
    CHECK(zero.has_value());
    if (!zero) return;
    for (auto& member : run.party) member.health = game::DungeonMemberMaxHealth(harness.registry, member);
    CHECK(!game::dev::AutopilotShouldRest(harness.registry, *policy));
    for (auto& member : run.party) member.health = 1;
    CHECK(game::dev::AutopilotShouldRest(harness.registry, *policy));
    CHECK(!game::dev::AutopilotShouldRest(harness.registry, *zero));
    CHECK(!game::RestAtDungeonCamp(harness.registry).has_value());
    CHECK(!game::dev::AutopilotShouldRest(harness.registry, *policy));
}

/// WP-43 F13c: at camp the autopilot rests and buys no stat upgrade; without the token it reinforces.
void AutopilotCampRestSkipsUpgrades() {
    {
        FHeadlessRunHarness harness{469u, "autopilot-camp-rest"};
        SetupConstructedRun(harness, game::EDungeonPhase::Camp);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.materials = 1000;
        for (auto& member : run.party) member.health = 1;
        const auto policy = game::dev::ParseAutopilotPolicy("camp=+2,rest=100");
        CHECK(policy.has_value());
        if (!policy) return;
        const auto upgradesBefore = harness.registry.ctx().get<game::FCampProgress>().upgradesTotal;
        game::dev::FRunAutopilot pilot{*policy};
        CHECK(pilot.tick(harness.registry, std::string(game::kRunModalCamp)) == EAutopilotStatus::Finished);
        const auto& progress = harness.registry.ctx().get<game::FCampProgress>();
        CHECK(game::CampChoiceThisCamp(progress, run.depth) == game::ECampChoice::Rest);
        CHECK(progress.upgradesTotal == upgradesBefore);
    }
    {
        FHeadlessRunHarness harness{469u, "autopilot-camp-reinforce"};
        SetupConstructedRun(harness, game::EDungeonPhase::Camp);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.materials = 1000;
        for (auto& member : run.party) member.health = 1;
        const auto policy = game::dev::ParseAutopilotPolicy("camp=+2");
        CHECK(policy.has_value());
        if (!policy) return;
        const auto upgradesBefore = harness.registry.ctx().get<game::FCampProgress>().upgradesTotal;
        game::dev::FRunAutopilot pilot{*policy};
        (void)pilot.tick(harness.registry, std::string(game::kRunModalCamp));
        const auto& progress = harness.registry.ctx().get<game::FCampProgress>();
        CHECK(game::CampChoiceThisCamp(progress, run.depth) == game::ECampChoice::Reinforce);
        CHECK(progress.upgradesTotal >= upgradesBefore + 1);
    }
}

/// Test 6: consolidation refuses a move whose removal breaks a co-fitted rune (fire supplies inferno's
/// Fire tag, and Strike is full): zero moves, byte-identical loadout, `camp move none engine=engine.ember`.
void CampConsolidationRefusesBrokenRemoval() {
    FHeadlessRunHarness harness{469u, "engine-camp-refuse"};
    SetupConstructedRun(harness, game::EDungeonPhase::Camp);
    auto& run = harness.registry.ctx().get<game::FDungeonRun>();
    run.party[0].skills[0].runeIds = {"rune.fire", "rune.inferno", "rune.power"};
    run.party[2].skills[5].runeIds = {"rune.ember"};
    const std::string before = LoadoutSignature(run);
    FLogCapture capture;
    CHECK(game::dev::ConsolidateEnginesAtCamp(harness.registry) == 0);
    const std::string after = LoadoutSignature(run);
    if (before != after) {
        std::cout << "[report] consolidation changed the loadout\n  before: " << before << "\n  after:  " << after
                  << '\n';
    }
    CHECK(after == before);
    CheckContains(capture.Text(), "[autopilot] camp move none engine=engine.ember", __LINE__);
}

/// Test 7: reward=engine scores every card BEFORE resolving (P3) — the completing ember beats card 0
/// power; an owned duplicate scores 0, so cold (start, 1) wins the tie over the owned fire.
void RewardEnginePicksCompletingCard() {
    {
        FHeadlessRunHarness harness{469u, "engine-card-complete"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.fire"};
        FLogCapture capture;
        RewardTick(harness, "reward=engine,camp=none", {"rune.power", "rune.ember"});
        CheckContains(capture.Text(), "pick=1 rune=rune.ember rule=complete", __LINE__);
        CHECK((run.runes == std::vector<std::string>{"rune.ember"}));  // granted, not fitted (no equip token)
    }
    {
        FHeadlessRunHarness harness{469u, "engine-card-owned"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.fire"};

        CHECK(game::dev::ScoreEngineCard(harness.registry, "rune.ember") == 2);  // completes engine.ember
        CHECK(game::dev::ScoreEngineCard(harness.registry, "rune.cold") == 1);   // starts engine.frost
        CHECK(game::dev::ScoreEngineCard(harness.registry, "rune.power") == 0);  // no engine
        CHECK(game::dev::ScoreEngineCard(harness.registry, "rune.fire") == 0);   // owned duplicate = refund only

        FLogCapture capture;
        RewardTick(harness, "reward=engine,camp=none", {"rune.fire", "rune.power", "rune.cold"});
        CheckContains(capture.Text(), "pick=2 rune=rune.cold rule=start", __LINE__);
    }
}

/// Test 8: the run-end vocabulary (P7) — engine.ember per state: nothing owned → not-offered, a pooled
/// partner → partner-unfitted, one host slot → armed (member/slot), a pair in a non-host → no-host, a
/// split whose every move is refused → moved-refused.
void ClassifyEnginesReasons() {
    CHECK(game::dev::EngineArmName(EEngineArm::Armed) == "armed");
    CHECK(game::dev::EngineArmName(EEngineArm::NotOffered) == "not-offered");
    CHECK(game::dev::EngineArmName(EEngineArm::PartnerUnfitted) == "partner-unfitted");
    CHECK(game::dev::EngineArmName(EEngineArm::MovedRefused) == "moved-refused");
    CHECK(game::dev::EngineArmName(EEngineArm::NoHost) == "no-host");

    {
        FHeadlessRunHarness harness{469u, "engine-arm-none"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        const auto statuses = game::dev::ClassifyEngines(harness.registry);
        CHECK(statuses.size() == 4);  // the frozen fixture's four rows, in data order
        CHECK(statuses[0].engineId == "engine.ember");
        CHECK(statuses[0].state == EEngineArm::NotOffered);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-arm-pooled"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.fire"};
        run.runes = {"rune.ember"};
        const auto statuses = game::dev::ClassifyEngines(harness.registry);
        CHECK(statuses[0].state == EEngineArm::PartnerUnfitted);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-arm-armed"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        harness.registry.ctx().get<game::FDungeonRun>().party[0].skills[0].runeIds = {"rune.fire", "rune.ember"};
        const auto statuses = game::dev::ClassifyEngines(harness.registry);
        CHECK(statuses[0].state == EEngineArm::Armed);
        CHECK(statuses[0].member == 0);
        CHECK(statuses[0].slot == 0);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-arm-nohost"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        harness.registry.ctx().get<game::FDungeonRun>().party[2].skills[1].runeIds = {"rune.fire", "rune.ember"};
        const auto statuses = game::dev::ClassifyEngines(harness.registry);
        CHECK(statuses[0].state == EEngineArm::NoHost);
    }
    {
        FHeadlessRunHarness harness{469u, "engine-arm-refused"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[0].skills[0].runeIds = {"rune.fire", "rune.inferno", "rune.power"};
        run.party[2].skills[5].runeIds = {"rune.ember"};
        const auto statuses = game::dev::ClassifyEngines(harness.registry);
        CHECK(statuses[0].state == EEngineArm::MovedRefused);
    }
}

/// Test 9: two fresh driving runs (seed 469) under the full engine policy produce the identical
/// `[autopilot]` decision sequence and the identical run-log loadout events — deterministic end to end,
/// reaching dungeon 2 without getting stuck.
void EnginePolicyIsDeterministic() {
    const std::string policyText = "reward=engine,event=first,chest=open,camp=offer+1,continue,equip=engine";
    FAutopilotDrive first;
    FAutopilotDrive second;
    std::vector<std::string> firstLines;
    std::vector<std::string> secondLines;
    {
        FLogCapture capture;
        first = DriveAutopilot(469u, "engine-det-a", policyText, EDriveStop::DungeonTwo);
        firstLines = LinesContaining(capture.Text(), "[autopilot]");
    }
    {
        FLogCapture capture;
        second = DriveAutopilot(469u, "engine-det-b", policyText, EDriveStop::DungeonTwo);
        secondLines = LinesContaining(capture.Text(), "[autopilot]");
    }
    CHECK(!first.stuck && !second.stuck);
    CHECK(first.reachedDungeonTwo && second.reachedDungeonTwo);
    CHECK(!firstLines.empty());
    if (firstLines != secondLines) {
        const std::size_t shared = std::min(firstLines.size(), secondLines.size());
        for (std::size_t index = 0; index < shared; ++index) {
            if (firstLines[index] != secondLines[index]) {
                std::cout << "[report] autopilot lines diverge at " << index << "\n  a: " << firstLines[index]
                          << "\n  b: " << secondLines[index] << '\n';
                break;
            }
        }
        if (firstLines.size() != secondLines.size()) {
            std::cout << "[report] autopilot line counts differ: " << firstLines.size() << " vs "
                      << secondLines.size() << '\n';
        }
    }
    CHECK(firstLines == secondLines);

    // The run-log side: identical `loadout` events too (same equip/remove sequence).
    const auto loadouts = [](const FAutopilotDrive& drive) {
        std::vector<nlohmann::json> events;
        for (const auto& event : drive.events) {
            if (event.value("t", std::string{}) == "loadout") events.push_back(event);
        }
        return events;
    };
    const std::vector<nlohmann::json> firstLoadouts = loadouts(first);
    const std::vector<nlohmann::json> secondLoadouts = loadouts(second);
    if (firstLoadouts != secondLoadouts) {
        std::cout << "[report] loadout events differ: " << firstLoadouts.size() << " vs "
                  << secondLoadouts.size() << '\n';
    }
    CHECK(firstLoadouts == secondLoadouts);
    CHECK(!firstLoadouts.empty());  // the policy fit at least one pick on the way into dungeon 2
}

/// Test 10 (P6 bridge, replacing the impossible drive-side ignition assertion): the armed SLOT is what
/// CheckEngineIgnition needs. Copy the policy-armed run slots onto a battle entity and give the target the
/// trigger status → 1 ignition + one `engine_ignition` run-log event; the equip=auto loadout of the same
/// state (the pair split across Bolt and Firebolt) ignites nothing.
void ArmedSlotIgnitesAutoDoesNot() {
    {
        FHeadlessRunHarness harness{469u, "engine-bridge-armed"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[3].skills[1].runeIds = {"rune.fire"};
        RewardTick(harness, "reward=card0,camp=none,equip=engine", {"rune.ember"});
        CHECK((SlotRunes(run, 3, 1) == std::vector<std::string>{"rune.fire", "rune.ember"}));  // armed

        const std::vector<game::FSkillSlot> slots = run.party[3].skills;
        const entt::entity actor = harness.registry.create();
        harness.registry.emplace<game::FPartyMember>(actor);
        harness.registry.emplace<game::FSkillLoadout>(actor, game::FSkillLoadout{slots});
        const entt::entity target = harness.registry.create();
        harness.registry.emplace<game::FActiveStatuses>(
            target, game::FActiveStatuses{{game::FStatusEffect{"status.burn", 3, 1}}});

        const auto& content = harness.registry.ctx().get<game::gameplay::FContentRegistry>();
        CHECK(game::CheckEngineIgnition(harness.registry, actor, target, content, "skill.bolt") == 1);
        const auto ignitions = RunLogEventsOf(harness.registry, "engine_ignition");
        CHECK(ignitions.size() == 1);
        CHECK(ignitions[0]["engine"] == "engine.ember");
    }
    {
        FHeadlessRunHarness harness{469u, "engine-bridge-auto"};
        SetupConstructedRun(harness, game::EDungeonPhase::Reward);
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        run.party[3].skills[1].runeIds = {"rune.fire"};
        RewardTick(harness, "reward=card0,camp=none,equip=auto", {"rune.ember"});
        CHECK((SlotRunes(run, 3, 1) == std::vector<std::string>{"rune.fire"}));
        CHECK((SlotRunes(run, 2, 5) == std::vector<std::string>{"rune.ember"}));

        const std::vector<game::FSkillSlot> healerSlots = run.party[3].skills;
        const std::vector<game::FSkillSlot> mageSlots = run.party[2].skills;
        const auto& content = harness.registry.ctx().get<game::gameplay::FContentRegistry>();

        const auto makeActor = [&](const std::vector<game::FSkillSlot>& slots) {
            const entt::entity actor = harness.registry.create();
            harness.registry.emplace<game::FPartyMember>(actor);
            harness.registry.emplace<game::FSkillLoadout>(actor, game::FSkillLoadout{slots});
            return actor;
        };
        const auto makeBurningTarget = [&] {
            const entt::entity target = harness.registry.create();
            harness.registry.emplace<game::FActiveStatuses>(
                target, game::FActiveStatuses{{game::FStatusEffect{"status.burn", 3, 1}}});
            return target;
        };

        CHECK(game::CheckEngineIgnition(harness.registry, makeActor(healerSlots), makeBurningTarget(), content,
                                        "skill.bolt") == 0);
        CHECK(game::CheckEngineIgnition(harness.registry, makeActor(mageSlots), makeBurningTarget(), content,
                                        "skill.firebolt") == 0);
        CHECK(RunLogEventsOf(harness.registry, "engine_ignition").empty());
    }
}

// ---------------------------------------------------------------------------------------------
// 8. WP-09h (M1a P2): the gate-aware two-stage event policy
// ---------------------------------------------------------------------------------------------

/// The constructed-event tests need the fixture party at pure base stats: SetupConstructedRun drives a room
/// victory first, which grants EXP/levels — a stat bonus riding along would open the sealed-cache gate and
/// make the gate-closed precondition vacuous (review P2's reset note, kept instead of dropping the assert).
void ResetPartyStatBonuses(game::FDungeonRun& run) {
    for (auto& member : run.party) {
        member.bonusHealth       = 0;
        member.bonusAttack       = 0;
        member.bonusMagic        = 0;
        member.bonusDefense      = 0;
        member.bonusMagicDefense = 0;
        member.bonusSpeed        = 0;
    }
}

/// P2: on the frozen fixture's sealed cache the policy index (0) is gated shut (the party is authored one
/// point short: attack 8 < 9), so the autopilot picks the FIRST OPEN choice (1) and logs the fallback. The
/// resolve keeps (depth, room, phase) constant, so only the widened key (event stage) makes the second tick
/// post ContinueEvent instead of timing out to Stuck. With the gate open (a ctx FRunMember attack bonus) the
/// policy index 0 itself is chosen; an instance whose id has no def is an immediate Stuck.
// WP-41 F18: the autopilot resolves an offer the event opened before it continues; fork= parses.
void AutopilotResolvesEventOffer() {
    const auto variant = game::dev::ParseAutopilotPolicy("fork=variant");
    CHECK(variant.has_value() && variant->forkVariant);
    const auto standard = game::dev::ParseAutopilotPolicy("fork=standard");
    CHECK(standard.has_value() && !standard->forkVariant);
    CHECK(!game::dev::ParseAutopilotPolicy("fork=x").has_value());

    FHeadlessRunHarness harness{7, "event-offer"};
    auto& registry = harness.registry;
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    if (registry.ctx().get<game::FRewards>().offer.active) CHECK(game::ResolveRuneOffer(registry, -1));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ChooseEventOption, 0).has_value());
    CHECK(game::OpenRuneOffer(registry, "normal"));
    FRunAutopilot pilot{*game::dev::ParseAutopilotPolicy(game::dev::kDefaultAutopilotPolicy)};
    for (int tick = 0; tick < 10; ++tick) {
        if (registry.ctx().get<game::FDungeonRun>().phase != game::EDungeonPhase::Event) break;
        const EAutopilotStatus status = pilot.tick(registry, game::kRunModalEvent);
        CHECK(status != EAutopilotStatus::Stuck);
        FEmberModalHostState host(std::string(game::kRunModalEvent), EHostMode::RunPhase);
        host.Update(registry, 1.f / 60.f);
    }
    CHECK(!registry.ctx().get<game::FRewards>().offer.active);
    CHECK(registry.ctx().get<game::FDungeonRun>().phase != game::EDungeonPhase::Event);
}

void AutopilotPicksOpenOptionPastGate() {
    const auto policy = game::dev::ParseAutopilotPolicy("event=first");
    CHECK(policy.has_value());

    {
        FHeadlessRunHarness harness{469u, "autopilot-event-gate"};
        SetupConstructedRun(harness, game::EDungeonPhase::Event);
        ResetPartyStatBonuses(harness.registry.ctx().get<game::FDungeonRun>());
        harness.registry.ctx().emplace<game::FEventInstance>(
            game::FEventInstance{"event.test_sealed_cache", -1, false});

        const auto def = game::FindEventDef(harness.registry, "event.test_sealed_cache");
        CHECK(def.has_value());
        CHECK(def->choices.size() >= 2);
        CHECK(def->choices[0].check.has_value());
        // Anti-vacuous: the policy index really is shut on the fixture party.
        CHECK(!game::EvaluateEventGate(harness.registry, def->choices[0]).open);

        const game::FDungeonRun& runBefore = harness.registry.ctx().get<game::FDungeonRun>();
        const int depth = runBefore.depth;
        const int room = runBefore.room;

        game::dev::FRunAutopilot pilot{*policy};
        FLogCapture capture;
        CHECK(pilot.tick(harness.registry, std::string(game::kRunModalEvent)) == EAutopilotStatus::Running);
        const auto* request = harness.registry.ctx().find<game::FRunIntentRequest>();
        CHECK(request != nullptr);
        CHECK(request->intent == game::ERunIntent::ChooseEventOption);
        CHECK(request->choice == 1);  // the first open choice, not the shut policy index 0
        CheckContains(capture.Text(),
                      std::format("[autopilot] event depth={} room={} choice=1 (gate fallback)", depth, room),
                      __LINE__);

        // The host order: consume (erase) the request, then apply the intent. The resolve flips only
        // `resolved` — (depth, room, phase) stay put.
        harness.registry.ctx().erase<game::FRunIntentRequest>();
        CHECK(game::ApplyRunIntent(harness.registry, game::ERunIntent::ChooseEventOption, 1).has_value());
        CHECK(harness.registry.ctx().get<game::FEventInstance>().resolved);

        // Tick 2 is a NEW decision point: the autopilot continues the result screen. Without the event stage
        // in the key this tick would repeat the choose intent and eventually report Stuck.
        CHECK(pilot.tick(harness.registry, std::string(game::kRunModalEvent)) == EAutopilotStatus::Running);
        request = harness.registry.ctx().find<game::FRunIntentRequest>();
        CHECK(request != nullptr);
        CHECK(request->intent == game::ERunIntent::ContinueEvent);
        const int roomBefore = harness.registry.ctx().get<game::FDungeonRun>().room;
        harness.registry.ctx().erase<game::FRunIntentRequest>();
        CHECK(game::ApplyRunIntent(harness.registry, game::ERunIntent::ContinueEvent).has_value());
        const game::FDungeonRun& run = harness.registry.ctx().get<game::FDungeonRun>();
        CHECK(run.room == roomBefore + 1);
        CHECK(run.phase == game::EDungeonPhase::Battle);

        // Exactly one EventChosen line, for the open fallback choice (choice 0 was never attempted).
        const auto eventLines = RunLogEventsOf(harness.registry, "event");
        CHECK(eventLines.size() == 1);
        CHECK(eventLines[0].value("event", std::string{}) == "event.test_sealed_cache");
        CHECK(eventLines[0].value("choice", -1) == 1);
    }
    {
        FHeadlessRunHarness harness{469u, "autopilot-event-open"};
        SetupConstructedRun(harness, game::EDungeonPhase::Event);
        ResetPartyStatBonuses(harness.registry.ctx().get<game::FDungeonRun>());
        harness.registry.ctx().emplace<game::FEventInstance>(
            game::FEventInstance{"event.test_sealed_cache", -1, false});

        const auto def = game::FindEventDef(harness.registry, "event.test_sealed_cache");
        CHECK(def.has_value());
        CHECK(def->choices[0].check.has_value());
        // Control: the ctx attack bonus guarantees the party best reaches the gate; the policy index 0 wins.
        harness.registry.ctx().get<game::FDungeonRun>().party[0].bonusAttack += def->choices[0].check->difficulty;
        CHECK(game::EvaluateEventGate(harness.registry, def->choices[0]).open);

        const game::FDungeonRun& runBefore = harness.registry.ctx().get<game::FDungeonRun>();
        const int depth = runBefore.depth;
        const int room = runBefore.room;
        game::dev::FRunAutopilot pilot{*policy};
        FLogCapture capture;
        CHECK(pilot.tick(harness.registry, std::string(game::kRunModalEvent)) == EAutopilotStatus::Running);
        const auto* request = harness.registry.ctx().find<game::FRunIntentRequest>();
        CHECK(request != nullptr);
        CHECK(request->intent == game::ERunIntent::ChooseEventOption);
        CHECK(request->choice == 0);  // the policy index itself — no fallback
        CheckContains(capture.Text(),
                      std::format("[autopilot] event depth={} room={} choice=0", depth, room), __LINE__);
        CHECK(capture.Text().find("(gate fallback)") == std::string::npos);
    }
    {
        FHeadlessRunHarness harness{469u, "autopilot-event-no-def"};
        SetupConstructedRun(harness, game::EDungeonPhase::Event);
        harness.registry.ctx().emplace<game::FEventInstance>(
            game::FEventInstance{"event.no_such_def", -1, false});

        game::dev::FRunAutopilot pilot{*policy};
        CHECK(pilot.tick(harness.registry, std::string(game::kRunModalEvent)) == EAutopilotStatus::Stuck);
    }
}

/// WP-42 F9: fast-forward steps a fixed 1/60 s and skips rasterization unless a capture is due.
void FastForwardFramePlan() {
    const auto plan = [](bool fixed, bool fast, bool capture) { return game::PlanEngineFrame(fixed, fast, capture, 0.25f); };
    CHECK(plan(false, false, false).dt == 0.25f);
    CHECK(plan(false, false, false).rasterize);
    CHECK(plan(true, false, false).dt == 1.f / 60.f);
    CHECK(plan(true, false, false).rasterize);
    CHECK(plan(false, true, false).dt == 1.f / 60.f);
    CHECK(!plan(false, true, false).rasterize);
    CHECK(plan(false, true, true).dt == 1.f / 60.f);
    CHECK(plan(false, true, true).rasterize);
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);  // a crash must still show how far the suite got
    // Save safety: ctest gives every feature test GAME01P_SAVE_DIR (WP-00b); this fallback only covers
    // running the executable directly, outside ctest — plain runs would otherwise target the user's real
    // save under %APPDATA%. Commit the override before anything can touch disk.
    if (const char* existing = std::getenv(game::kSettingsSaveDirEnv); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "headless_tools_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s(game::kSettingsSaveDirEnv, value.c_str());
#else
        setenv(game::kSettingsSaveDirEnv, value.c_str(), 1);
#endif
    }
    return game_test::RunTests("headless tools", {
        {"ParsesEveryCommand", ParsesEveryCommand},
        {"AutopilotResolvesEventOffer", AutopilotResolvesEventOffer},
        {"RejectsEachErrorKind", RejectsEachErrorKind},
        {"InjectsClickIntoImGuiUnderDummyDriver", InjectsClickIntoImGuiUnderDummyDriver},
        {"ParsesAutopilotPolicy", ParsesAutopilotPolicy},
        {"AutopilotDrivesSeededRunIntoDungeonTwo", AutopilotDrivesSeededRunIntoDungeonTwo},
        {"AutopilotStopsAtRoomLimit", AutopilotStopsAtRoomLimit},
        {"AutopilotEquipFitsPickedRune", AutopilotEquipFitsPickedRune},
        {"EnginePolicyCompletesHalfPair", EnginePolicyCompletesHalfPair},
        {"EnginePolicyStartsOnHostOnly", EnginePolicyStartsOnHostOnly},
        {"EnginePolicyFallbackKeepsReservation", EnginePolicyFallbackKeepsReservation},
        {"CampConsolidationMovesSplitPair", CampConsolidationMovesSplitPair},
        {"RestTokenParses", RestTokenParses},
        {"AutopilotRestsBelowThreshold", AutopilotRestsBelowThreshold},
        {"AutopilotCampRestSkipsUpgrades", AutopilotCampRestSkipsUpgrades},
        {"CampConsolidationRefusesBrokenRemoval", CampConsolidationRefusesBrokenRemoval},
        {"RewardEnginePicksCompletingCard", RewardEnginePicksCompletingCard},
        {"ClassifyEnginesReasons", ClassifyEnginesReasons},
        {"EnginePolicyIsDeterministic", EnginePolicyIsDeterministic},
        {"ArmedSlotIgnitesAutoDoesNot", ArmedSlotIgnitesAutoDoesNot},
        {"AutopilotPicksOpenOptionPastGate", AutopilotPicksOpenOptionPastGate},
        {"FastForwardFramePlan", FastForwardFramePlan},
    });
}
