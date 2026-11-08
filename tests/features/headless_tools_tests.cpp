// WP-00c (M3) tests: the windowless input script (parser + SDL injector), the injected SDL path under the
// dummy video driver + software renderer — into ImGui and into the game's FInputState (the click-timing
// rule keeps a HUD press out of the grid) — plus `expect state`, the autopilot policy parser, and the run
// autopilot driving a seeded run through reward/event/camp into dungeon 2 (twice, identical run-log events;
// plus an equip=auto drive that fits a picked rune). The run tests read the FROZEN FIXTURE
// (TestAssetRoots) and force every room victory — the autopilot is the unit under test, so no assertion may
// depend on a battle outcome (and therefore on ship balance; critic F1).
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
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/InputState.h"
#include "dev/FInputScript.h"
#include "dev/FRunAutopilot.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
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

using game::dev::EAutopilotStatus;
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
    CHECK(!defaults->equipAuto);  // WP-27: off unless written (the default keeps the M-FUN behaviour)

    // continue is present only when written; tokens left out take the default policy's value.
    const auto custom = game::dev::ParseAutopilotPolicy("reward=skip,event=second,chest=never,camp=+2");
    CHECK(custom.has_value());
    CHECK(custom->rewardSkip);
    CHECK(custom->eventOption == 1);
    CHECK(!custom->chestOnEvenDungeons);
    CHECK(!custom->campBuyOffer);
    CHECK(custom->campUpgrades == 2);
    CHECK(!custom->campContinue);
    CHECK(!custom->equipAuto);

    // Whitespace around tokens is trimmed; camp=offer+1 buys the offer and one upgrade.
    const auto spaced = game::dev::ParseAutopilotPolicy(" reward=card1 , camp=offer+1 , continue ");
    CHECK(spaced.has_value());
    CHECK(spaced->rewardCard == 1);
    CHECK(spaced->campBuyOffer);
    CHECK(spaced->campUpgrades == 1);
    CHECK(spaced->campContinue);

    // WP-27: equip=auto | equip=off only.
    const auto equipAuto = game::dev::ParseAutopilotPolicy("equip=auto");
    CHECK(equipAuto.has_value());
    CHECK(equipAuto->equipAuto);
    const auto equipOff = game::dev::ParseAutopilotPolicy("equip=off");
    CHECK(equipOff.has_value());
    CHECK(!equipOff->equipAuto);

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
    expectPolicyError("equip=yes");                 // bad equip value
    expectPolicyError("equip=auto,equip=off");      // duplicate token
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
        {"RejectsEachErrorKind", RejectsEachErrorKind},
        {"InjectsClickIntoImGuiUnderDummyDriver", InjectsClickIntoImGuiUnderDummyDriver},
        {"ParsesAutopilotPolicy", ParsesAutopilotPolicy},
        {"AutopilotDrivesSeededRunIntoDungeonTwo", AutopilotDrivesSeededRunIntoDungeonTwo},
        {"AutopilotStopsAtRoomLimit", AutopilotStopsAtRoomLimit},
        {"AutopilotEquipFitsPickedRune", AutopilotEquipFitsPickedRune},
    });
}
