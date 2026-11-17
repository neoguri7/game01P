// WP-50: expedition briefing, boss introduction and in-run settings.
//
// Every case asserts the designed behavior of docs/wp/WP-50/spec.md §3: the exact state transition, the run
// state, the model strings derived from data, the layout rectangles, the pictures on disk and the pause flow.
// Threat rosters are never hard-coded: they are derived in the test from the shipped JSON (WP-52 will add
// enemies), and the pure rule runs on a hand-built table.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <SDL3/SDL.h>

#include "core/Engine.h"
#include "core/EngineState.h"
#include "core/InputState.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "ecs/systems/TurnSystem.h"
#include "dev/FRunAutopilot.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/combat/FBattleFlow.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FEncounterContent.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunBriefing.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunPath.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "presentation/FBossIntroView.h"
#include "presentation/FBriefingView.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonViews.h"
#include "presentation/FFlowArt.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FPreparationView.h"
#include "presentation/FSettings.h"
#include "presentation/FTitleMenuView.h"
#include "states/FBaseState.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberBriefingState.h"
#include "states/FEmberMenuRequest.h"
#include "states/FEmberModalHostState.h"
#include "states/FEmberPreparationState.h"
#include "states/FEmberTitleState.h"
#include "states/FRunFlowStateMap.h"
#include "states/FStateCatalog.h"
#include "states/GameStateMachine.h"

namespace {

constexpr std::uint32_t kSeed = 4242;

std::string UniquePath(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return "out/" + std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
}

void RemoveFiles(const std::string& checkpoint) {
    std::error_code error;
    std::filesystem::remove(checkpoint, error);
    std::filesystem::remove(checkpoint + ".run.json", error);
    std::filesystem::remove(checkpoint + ".run.json.tmp", error);
}

// --- a booted registry with its own checkpoint and save slot (run_modal_tests.cpp:96) ---------------
struct FBoot {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;

    explicit FBoot(std::string path, bool prepPools = false) : checkpoint(std::move(path)) {
        game::FAssetManager::Initialize(registry, prepPools ? ShipAssetRoots() : TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
    }

    ~FBoot() {
        RemoveFiles(checkpoint);
        RemoveFiles(runSave);
    }
};

/// "Ship boot at index i": the shipped content with a run installed at dungeon `index`.
void InstallShipRunAt(FBoot& boot, int index) {
    boot.registry.ctx().get<game::FDungeonCheckpoint>().cleared = index;
    game::InstallDungeonRunAt(boot.registry, kSeed, index);
    CHECK(boot.registry.ctx().contains<game::FDungeonRun>());
}

// --- a battle with a real FEmberBattleState (run_modal_tests.cpp:115) ------------------------------
struct FBattle {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;
    game::ecs::TurnSystem turns;
    FEmberBattleState battle;
    bool entered{false};

    FBattle(std::string path, bool startRun = true, bool enter = true) : checkpoint(std::move(path)) {
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        turns.onRegister(registry);
        if (startRun) game::StartDungeonRun(registry, kSeed, true);
        if (enter) {
            battle.OnEnter(registry);
            entered = true;
        }
    }

    ~FBattle() {
        if (entered) battle.OnExit(registry);
        RemoveFiles(checkpoint);
        RemoveFiles(runSave);
    }
};

// --- a view fixture: ImGui + software renderer + dungeon art + the UI registry (run_modal_tests.cpp:144)
struct FViewFixture {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, &SDL_DestroySurface};
    FRendererPtr renderer{nullptr};
    entt::registry registry;
    std::string checkpoint;
    std::string runSave;
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{nullptr, &ImGui::DestroyContext};

    explicit FViewFixture(std::string path) : checkpoint(std::move(path)) {
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        surface.reset(SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888));
        CHECK(surface != nullptr);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        CHECK(renderer != nullptr);
        registry.ctx().emplace<game::FEngineWindowState>().width = 1280;
        registry.ctx().get<game::FEngineWindowState>().height = 800;
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        CHECK(game::InitializeDungeonArt(registry));
        context.reset(ImGui::CreateContext());
        CHECK(context != nullptr);
        auto& io = ImGui::GetIO();
        io.DisplaySize = {1280, 800};
        io.DeltaTime = 1.f / 60.f;
        io.Fonts->Build();
        game::InitializeDungeonUi(registry);
    }

    ~FViewFixture() {
        context.reset();
        RemoveFiles(checkpoint);
        RemoveFiles(runSave);
    }
};

/// Routes LOG_WARN/LOG_ERROR into a string stream (settings_tests.cpp:135).
class FLogCapture {
public:
    FLogCapture() {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream);
        sink->set_pattern("%v");
        logger_ = std::make_shared<spdlog::logger>("wp50-test", sink);
        logger_->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger_);
    }
    ~FLogCapture() { SetCurrentGameLogger(nullptr); }
    FLogCapture(const FLogCapture&) = delete;
    FLogCapture& operator=(const FLogCapture&) = delete;

    [[nodiscard]] std::string text() const { return stream.str(); }

private:
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger_;
};

/// A per-test scratch directory under out/ (preparation_tests.cpp:113), removed when the case ends.
struct FScratchRoot {
    std::filesystem::path root;

    explicit FScratchRoot(const char* stem) {
        static std::uint32_t serial = 0;
        root = std::filesystem::path("out") / (std::string(stem) + "-" + std::to_string(++serial) + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code error;
        std::filesystem::create_directories(root / "data", error);
        CHECK(!error);
    }
    ~FScratchRoot() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    FScratchRoot(const FScratchRoot&) = delete;
    FScratchRoot& operator=(const FScratchRoot&) = delete;
};

/// The shipped content with `data/flow.json` overridden (or absent) by a scratch root.
struct FFlowBoot {
    FScratchRoot scratch;
    entt::registry registry;

    FFlowBoot(const char* stem, const std::function<void(const std::filesystem::path&)>& overrides)
        : scratch(stem) {
        if (overrides) overrides(scratch.root / "data");
        game::FAssetManager::Initialize(registry, {scratch.root.string(), kShipAssetRoot});
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, (scratch.root / "checkpoint.json").string()));
    }
};

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    CHECK(file.good());
    file << text;
}

void PressKey(entt::registry& registry, SDL_Scancode scancode) {
    auto* input = registry.ctx().find<game::FInputState>();
    CHECK(input != nullptr);
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    (void)input->processEvent(event);
}

nlohmann::json ReadJsonFile(const std::string& path) {
    std::ifstream file(path);
    CHECK(file.good());
    nlohmann::json document;
    file >> document;
    return document;
}

/// The camp of dungeon `index` (0-based), reached through ContinueReward (run_modal_tests.cpp:705).
void CampAfterDungeon(entt::registry& registry, int index) {
    registry.ctx().get<game::FDungeonCheckpoint>().cleared = index;
    game::InstallDungeonRunAt(registry, kSeed, index);
    const auto* sequence = game::FindRunSequence(registry);
    CHECK(sequence != nullptr);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Reward;
    run.room = sequence->BattleCountAt(sequence->PositionOf(index)) - 1;
    if (auto* rewards = registry.ctx().find<game::FRewards>()) rewards->offer.active = false;
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Camp);
}

/// Pins the dungeon's fork before the briefing is built (EnsureRunPath keeps a valid stored fork).
void PinFork(entt::registry& registry, bool battle, int slot, int battleOrdinal, std::string affixId, int choice) {
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    registry.ctx().get<game::FRunPathState>() =
        game::FRunPathState{run.seed, run.depth, true, slot, battle, battleOrdinal, std::move(affixId), choice};
}

void PinNoFork(entt::registry& registry) {
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    registry.ctx().get<game::FRunPathState>() = game::FRunPathState{run.seed, run.depth, false, 0, false, 0, {}, -1};
}

game::FRunBriefing BuildShip(FBoot& boot, int index, bool fork = false) {
    InstallShipRunAt(boot, index);
    if (!fork) PinNoFork(boot.registry);
    const auto briefing = game::BuildRunBriefing(boot.registry);
    CHECK(briefing.has_value());
    return *briefing;
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

/// Draws `draw` between NewFrame and Render with ImGui's log capturing every text call.
std::string CaptureLog(const char* stem, const std::function<void()>& draw) {
    const auto logPath = (std::filesystem::path("out") / (std::string(stem) + "-ui.log")).string();
    ImGui::NewFrame();
    ImGui::LogToFile(0, logPath.c_str());
    draw();
    ImGui::LogFinish();
    ImGui::Render();
    std::ifstream capture(logPath);
    CHECK(capture.good());
    return std::string{std::istreambuf_iterator<char>(capture), std::istreambuf_iterator<char>()};
}

// ---------------------------------------------------------------------------------------------
// F1-F5: routing
// ---------------------------------------------------------------------------------------------

void PrepareStartRootsBriefing() {
    FBoot boot(UniquePath("wp50-prepare"), true);
    auto& registry = boot.registry;
    registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{true, -1});
    FEmberPreparationState prepare;
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    registry.ctx().insert_or_assign<game::FPreparationCommand>(game::FPreparationCommand{game::EPreparationCommand::Start});
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, "ember.briefing"}));
    auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.depth == 1 && run.room == 0 && run.phase == game::EDungeonPhase::Battle);
    run.materials = 777;
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
    CHECK(registry.ctx().get<game::FDungeonRun>().materials == 777);
}

void CampDescendRootsBriefing() {
    {
        FBoot boot(UniquePath("wp50-camp"), true);
        auto& registry = boot.registry;
        CampAfterDungeon(registry, 0);
        game::PostRunIntent(registry, game::ERunIntent::LeaveCamp);
        FEmberModalHostState host("ember.camp", EHostMode::RunPhase);
        CHECK((host.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, "ember.briefing"}));
        const auto& run = registry.ctx().get<game::FDungeonRun>();
        CHECK(run.depth == 2 && run.room == 0 && run.phase == game::EDungeonPhase::Battle);
    }
    {
        FBoot boot(UniquePath("wp50-camp-theme"), true);
        auto& registry = boot.registry;
        CampAfterDungeon(registry, 3);  // the theme-cleared camp: "Descend deeper"
        game::PostRunIntent(registry, game::ERunIntent::LeaveCamp);
        FEmberModalHostState host("ember.camp", EHostMode::RunPhase);
        CHECK((host.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, "ember.briefing"}));
        const auto& run = registry.ctx().get<game::FDungeonRun>();
        CHECK(run.depth == 5 && run.room == 0 && run.phase == game::EDungeonPhase::Battle);
    }
    const game::FRunResult next{game::ERunExit::NextRoom};
    CHECK((game::ToStateTransition(next, "ember.camp") == FStateTransition{EStateChangeKind::Root, "ember.briefing"}));
    CHECK((game::ToStateTransition(next, "ember.reward") == FStateTransition{EStateChangeKind::Root, "ember.battle"}));
    CHECK((game::ToStateTransition(next, "ember.event") == FStateTransition{EStateChangeKind::Root, "ember.battle"}));
    CHECK((game::ToStateTransition(next, "ember.battle") == FStateTransition{EStateChangeKind::Root, "ember.battle"}));
}

void BriefingEntersBattleOnce() {
    FBoot boot(UniquePath("wp50-enter"), true);
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    const auto before = registry.ctx().get<game::FDungeonRun>();
    FEmberBriefingState state;
    state.OnEnter(registry);
    CHECK((state.Update(registry, 0.f) == FStateTransition{}));
    registry.ctx().insert_or_assign<game::FBriefingCommand>(game::FBriefingCommand{game::EBriefingCommand::Enter});
    CHECK((state.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, "ember.battle"}));
    CHECK(!registry.ctx().contains<game::FBriefingCommand>());
    CHECK((state.Update(registry, 0.f) == FStateTransition{}));

    game::FInputState::Initialize(registry);
    PressKey(registry, SDL_SCANCODE_RETURN);
    CHECK((state.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, "ember.battle"}));

    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.depth == before.depth && run.room == before.room && run.phase == before.phase &&
          run.materials == before.materials);

    registry.ctx().erase<game::FDungeonRun>();
    CHECK((state.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
}

void ContinueSkipsBriefing() {
    FBoot boot(UniquePath("wp50-continue"));
    game::StartRun(boot.registry, kSeed, true);
    boot.registry.ctx().erase<game::FDungeonRun>();
    boot.registry.ctx().get<game::FMenuRequest>().value = game::EMenuRequest::Continue;
    const auto next = ResolveEmberMenuRequest(boot.registry);
    CHECK(next.has_value() && (*next == FStateTransition{EStateChangeKind::Root, "ember.battle"}));
    const auto& run = boot.registry.ctx().get<game::FDungeonRun>();
    CHECK(run.room == 0 && run.phase == game::EDungeonPhase::Battle);
}

void CatalogHasBriefing() {
    const auto catalog = game::FStateCatalog::MakeEmber();
    CHECK(catalog.contains("ember.briefing"));
    const auto state = catalog.create("ember.briefing");
    CHECK(state != nullptr);
    CHECK(state->GetName() == "Briefing");
}

// ---------------------------------------------------------------------------------------------
// F6-F11: the briefing model
// ---------------------------------------------------------------------------------------------

void BriefingRoute() {
    FBoot boot(UniquePath("wp50-route"), true);
    {
        const auto briefing = BuildShip(boot, 0);
        const std::vector<std::string> labels{"Entrance", "Battle", "Event", "Battle", "Battle", "Camp"};
        const std::vector<game::EBriefingNodeKind> kinds{
            game::EBriefingNodeKind::Entrance, game::EBriefingNodeKind::Battle, game::EBriefingNodeKind::Event,
            game::EBriefingNodeKind::Battle,   game::EBriefingNodeKind::Battle, game::EBriefingNodeKind::Camp};
        CHECK(briefing.route.size() == labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) {
            CHECK(briefing.route[i].label == labels[i]);
            CHECK(briefing.route[i].kind == kinds[i]);
            CHECK(!briefing.route[i].altKind.has_value());
            CHECK(briefing.route[i].altLabel.empty());
        }
    }
    {
        InstallShipRunAt(boot, 0);
        PinFork(boot.registry, true, 2, 1, "affix.thorned", -1);
        const auto briefing = game::BuildRunBriefing(boot.registry);
        CHECK(briefing.has_value());
        CHECK(briefing->route.size() == 6);
        CHECK(briefing->route[3].altKind.has_value() && *briefing->route[3].altKind == game::EBriefingNodeKind::Elite);
        CHECK(briefing->route[3].altLabel == "Elite: Thorned");
    }
    {
        InstallShipRunAt(boot, 0);
        PinFork(boot.registry, false, 1, 0, "", -1);
        const auto briefing = game::BuildRunBriefing(boot.registry);
        CHECK(briefing.has_value());
        CHECK(briefing->route[2].altKind.has_value() && *briefing->route[2].altKind == game::EBriefingNodeKind::Mystery);
        CHECK(briefing->route[2].altLabel == "?");
    }
    {
        const auto briefing = BuildShip(boot, 2);
        CHECK(briefing.route.size() == 6);
        CHECK(briefing.route[4].kind == game::EBriefingNodeKind::Boss);
        CHECK(briefing.route[4].label == "Boss: Stone Guardian");
    }
    {
        const auto briefing = BuildShip(boot, 3);
        CHECK(briefing.route.size() == 7);
        CHECK(briefing.route[5].kind == game::EBriefingNodeKind::Boss);
        CHECK(briefing.route[5].label == "Boss: Cinder Warden");
    }
    CHECK(game::NodeIconId(game::EBriefingNodeKind::Entrance) == "art/ui/node_start");
    CHECK(game::NodeIconId(game::EBriefingNodeKind::Battle) == "art/ui/node_battle");
    CHECK(game::NodeIconId(game::EBriefingNodeKind::Elite) == "art/ui/node_elite");
    CHECK(game::NodeIconId(game::EBriefingNodeKind::Event) == "art/ui/node_event");
    CHECK(game::NodeIconId(game::EBriefingNodeKind::Mystery) == "art/ui/node_unknown");
    CHECK(game::NodeIconId(game::EBriefingNodeKind::Boss) == "art/ui/node_boss");
    CHECK(game::NodeIconId(game::EBriefingNodeKind::Camp) == "art/ui/node_camp");
    CHECK(game::ThemeBannerId("theme.ember_crypt") == "art/ui/theme_ember_crypt");
    CHECK(game::ThemeBannerId("theme.ashen_reach") == "art/ui/theme_ashen_reach");
}

std::string ExpectedRewardLine(entt::registry& registry, bool bossSentence) {
    std::string line = "Each battle won: +" + std::to_string(game::DungeonBalance(registry, "room_experience")) +
                       " EXP per member, +" + std::to_string(game::DungeonBalance(registry, "room_materials")) +
                       " materials";
    if (const auto* rewards = registry.ctx().find<game::FRewards>()) {
        line += ", " + std::to_string(std::lround(rewards->tables.chance * 100.0)) + "% chance of a rune offer";
    }
    line += ".";
    if (bossSentence) line += " The boss always offers a rune.";
    return line;
}

void BriefingHeaderLines() {
    FBoot boot(UniquePath("wp50-header"), true);
    const std::string plainDanger = "Danger: depth 1. Enemies grow stronger with each dungeon and each room.";
    {
        const auto b = BuildShip(boot, 0);
        CHECK(b.themeName == "Ember Crypt");
        CHECK(b.themeId == "theme.ember_crypt");
        CHECK(b.dungeonName == "The broken gate");
        CHECK(b.infoLine == "Dungeon 1 of 4  \xC2\xB7  Depth 1");
        CHECK(b.objective == "Objective: Clear every room.");
        CHECK(b.danger == plainDanger);
        CHECK(b.enterLabel == "Enter The broken gate");
        CHECK(b.rewardLine == ExpectedRewardLine(boot.registry, false));
        CHECK(b.eliteLine.empty() && b.eliteStakes.empty());
    }
    {
        const auto b = BuildShip(boot, 2);
        CHECK(b.objective == "Objective: Defeat Stone Guardian.");
        CHECK(b.rewardLine == ExpectedRewardLine(boot.registry, true));
        CHECK(b.rewardLine.ends_with(" The boss always offers a rune."));
    }
    {
        const auto b = BuildShip(boot, 3);
        CHECK(b.objective == "Objective: Defeat Cinder Warden to conquer Ember Crypt.");
    }
    {
        const auto b = BuildShip(boot, 4);
        CHECK(b.themeName == "Ashen Reach");
        CHECK(b.themeId == "theme.ashen_reach");
        CHECK(b.dungeonName == "The sunken vault");
    }
    {
        const auto b = BuildShip(boot, 8);
        CHECK(b.infoLine == "Dungeon 1 of 4  \xC2\xB7  Depth 9  \xC2\xB7  Cycle 1");
        CHECK(Contains(b.danger, " Cycle 1: enemies +10% health, +10% attack."));
    }
    {
        InstallShipRunAt(boot, 0);
        PinFork(boot.registry, true, 2, 1, "affix.thorned", -1);
        const auto b = game::BuildRunBriefing(boot.registry);
        CHECK(b.has_value());
        CHECK(b->danger == plainDanger);
        CHECK(!Contains(b->danger, "Elite"));
        CHECK(b->eliteLine ==
              "Elite option (Thorned): Melee skills cost the attacker 2 HP per enemy hit (never below 1 HP).");
        CHECK(b->eliteStakes == "Elite: enemies +20% max HP; a win offers a rune of Uncommon or better.");
        PinFork(boot.registry, true, 2, 1, "affix.thorned", 0);
        const auto decided = game::BuildRunBriefing(boot.registry);
        CHECK(decided.has_value());
        CHECK(decided->eliteLine.empty() && decided->eliteStakes.empty());
    }
}

void BriefingExpeditionRow() {
    FBoot boot(UniquePath("wp50-expedition"), true);
    const auto b = BuildShip(boot, 1);
    const auto document = ReadJsonFile("assets/data/dungeon.json");
    const auto& dungeons = document["themes"][0]["dungeons"];
    CHECK(b.expedition.size() == 4 && dungeons.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) CHECK(b.expedition[i].name == dungeons[i]["name"].get<std::string>());
    CHECK(b.expedition[0].cleared && !b.expedition[0].current);
    CHECK(!b.expedition[1].cleared && b.expedition[1].current);
    CHECK(!b.expedition[2].cleared && !b.expedition[2].current);
    CHECK(b.expedition[2].bossMark == "Mid boss");
    CHECK(b.expedition[3].bossMark == "Final boss");
    CHECK(b.expedition[0].bossMark.empty() && b.expedition[1].bossMark.empty());
}

game::FEncounterDef MakeEncounter(const char* id, int depth, std::vector<std::string> tags,
                                  std::vector<std::vector<const char*>> slots, std::vector<const char*> fill) {
    game::FEncounterDef def;
    def.id = id;
    def.introducedAtDepth = depth;
    def.tags = std::move(tags);
    for (const auto& pool : slots) {
        game::FEncounterSlot slot;
        slot.role = "melee";
        for (const char* unit : pool) slot.units.push_back({unit, 1});
        def.slots.push_back(std::move(slot));
    }
    for (const char* unit : fill) def.fill.push_back({unit, 1});
    return def;
}

game::FRunRoomSlot Room(const char* type, const char* boss = "", const char* encounter = "") {
    game::FRunRoomSlot room;
    room.type = type;
    room.boss = boss;
    room.encounter = encounter;
    return room;
}

void DungeonThreatUnitsRule() {
    game::FEncounterContent table;
    table.encounters.push_back(MakeEncounter("A", 1, {"t1"}, {{"u1", "u2"}, {"u3"}}, {"u1", "u4"}));
    table.encounters.push_back(MakeEncounter("B", 3, {}, {{"u5"}}, {}));
    table.encounters.push_back(MakeEncounter("C", 1, {"t2"}, {{"u6"}}, {}));
    game::FRunDungeonDef dungeon;
    dungeon.rooms = {Room("battle"), Room("event"), Room("battle"), Room("battle", "mid")};
    using Ids = std::vector<std::string>;
    CHECK(game::DungeonThreatUnits(table, dungeon, "t1", 1) == (Ids{"u1", "u2", "u3", "u4"}));
    CHECK(game::DungeonThreatUnits(table, dungeon, "t1", 3) == (Ids{"u1", "u2", "u3", "u4", "u5"}));
    CHECK(game::DungeonThreatUnits(table, dungeon, "t2", 1) == (Ids{"u6"}));

    game::FRunDungeonDef named;
    named.rooms = {Room("battle"), Room("event"), Room("battle", "", "C"), Room("battle", "mid")};
    CHECK(game::DungeonThreatUnits(table, named, "t1", 1) == (Ids{"u1", "u2", "u3", "u4", "u6"}));

    game::FRunDungeonDef unknownNamed;
    unknownNamed.rooms = {Room("battle", "", "Z"), Room("battle", "mid")};
    CHECK(game::DungeonThreatUnits(table, unknownNamed, "t1", 1).empty());

    game::FRunDungeonDef bossOnly;
    bossOnly.rooms = {Room("battle", "final")};
    CHECK(game::DungeonThreatUnits(table, bossOnly, "t1", 1).empty());

    CHECK(game::DungeonThreatUnits(game::FEncounterContent{}, dungeon, "t1", 1).empty());
}

/// The rule of DungeonThreatUnits restated over the shipped JSON (F10): never a hard-coded roster.
std::vector<std::string> ExpectedThreats(const std::string& themeId, int depth, int dungeonIndex,
                                         const nlohmann::json& dungeonDoc, const nlohmann::json& encounterDoc) {
    std::vector<std::string> ids;
    const auto add = [&ids](const std::string& id) {
        if (std::ranges::find(ids, id) == ids.end()) ids.push_back(id);
    };
    const nlohmann::json* theme = nullptr;
    for (const auto& candidate : dungeonDoc["themes"]) {
        if (candidate["id"].get<std::string>() == themeId) theme = &candidate;
    }
    CHECK(theme != nullptr);
    const auto& dungeon = (*theme)["dungeons"][static_cast<std::size_t>(dungeonIndex)];
    const auto addEncounter = [&](const nlohmann::json& encounter) {
        if (encounter.contains("slots")) {
            for (const auto& slot : encounter["slots"]) {
                if (!slot.contains("units")) continue;
                for (const auto& unit : slot["units"]) add(unit.value("unit", std::string{}));
            }
        }
        if (encounter.contains("fill")) {
            const auto& fill = encounter["fill"];
            if (fill.is_array() || fill.contains("units")) {
                const auto& rows = fill.is_array() ? fill : fill["units"];
                for (const auto& unit : rows) add(unit.value("unit", std::string{}));
            }
        }
    };
    bool hasBoss = false;
    for (const auto& room : dungeon["rooms"]) {
        if (room.value("type", "") != "battle") continue;
        if (!room.value("boss", std::string{}).empty()) {
            hasBoss = true;
            continue;
        }
        const std::string named = room.value("encounter", std::string{});
        for (const auto& encounter : encounterDoc["encounters"]) {
            if (!named.empty()) {
                if (encounter.value("id", std::string{}) == named) addEncounter(encounter);
                continue;
            }
            if (encounter.value("introduced_at_depth", 1) > depth) continue;
            const auto tags = encounter.value("tags", std::vector<std::string>{});
            if (!tags.empty() && std::ranges::find(tags, themeId) == tags.end()) continue;
            addEncounter(encounter);
        }
    }
    if (hasBoss) {
        for (const auto& enemy : dungeonDoc["generation"]["enemies"]) {
            if (enemy.contains("unit") && enemy["unit"].is_string()) add(enemy["unit"].get<std::string>());
        }
    }
    return ids;
}

void BriefingThreatsAndBoss() {
    FBoot boot(UniquePath("wp50-threats"), true);
    const auto dungeonDoc = ReadJsonFile("assets/data/dungeon.json");
    const auto encounterDoc = ReadJsonFile("assets/data/encounters.json");
    for (const int index : {0, 2, 4, 6}) {
        const auto b = BuildShip(boot, index);
        const std::string themeId = dungeonDoc["themes"][static_cast<std::size_t>(index / 4)]["id"].get<std::string>();
        const auto expected = ExpectedThreats(themeId, index + 1, index % 4, dungeonDoc, encounterDoc);
        std::vector<std::string> actual;
        for (const auto& unit : b.threats) actual.push_back(unit.unitId);
        CHECK(actual == expected);
        auto sorted = actual;
        std::ranges::sort(sorted);
        CHECK(std::ranges::adjacent_find(sorted) == sorted.end());
        const auto* content = boot.registry.ctx().find<game::gameplay::FContentRegistry>();
        CHECK(content != nullptr);
        for (const auto& unit : b.threats) {
            CHECK(content->units.find(unit.unitId) != nullptr);
            CHECK(!unit.name.empty() && !unit.sprite.empty());
        }
        if (index == 0) {
            CHECK(std::ranges::find(actual, "enemy.imp") != actual.end());
            CHECK(!b.boss.has_value());
        }
        if (index == 4) CHECK(std::ranges::find(actual, "enemy.ash_priest") != actual.end());
        if (index == 6) {
            for (const auto& enemy : dungeonDoc["generation"]["enemies"]) {
                CHECK(std::ranges::find(actual, enemy["unit"].get<std::string>()) != actual.end());
            }
        }
    }
    const auto* content = boot.registry.ctx().find<game::gameplay::FContentRegistry>();
    {
        const auto b = BuildShip(boot, 2);
        CHECK(b.boss.has_value());
        CHECK(b.boss->name == "Stone Guardian" && b.boss->mark == "MID BOSS" && b.boss->note == "Guard stance");
        CHECK(b.boss->gateLine == "Cannot drop below 50% health while its 3 Guardian hearts stand.");
        CHECK(b.boss->sprite == content->units.find("enemy.guardian")->spriteId);
    }
    {
        const auto b = BuildShip(boot, 3);
        CHECK(b.boss.has_value());
        CHECK(b.boss->name == "Cinder Warden" && b.boss->mark == "FINAL BOSS" && b.boss->note == "Kindling");
        CHECK(b.boss->gateLine.empty());
    }
    {
        const auto b = BuildShip(boot, 7);
        CHECK(b.boss.has_value());
        CHECK(b.boss->gateLine == "Cannot drop below 50% health while its 3 Ash chanters stand.");
    }
}

void BriefingPartyMatchesRun() {
    FBoot boot(UniquePath("wp50-party"), true);
    const auto b = BuildShip(boot, 0);
    const auto& run = boot.registry.ctx().get<game::FDungeonRun>();
    CHECK(b.party.size() == run.party.size() && !b.party.empty());
    for (std::size_t i = 0; i < run.party.size(); ++i) {
        const auto& member = run.party[i];
        const auto* unit = game::ResolveRunMemberUnit(boot.registry, member.id);
        CHECK(unit != nullptr);
        const auto split = member.displayName.find(" / ");
        const bool hasSplit = split != std::string::npos;
        CHECK(b.party[i].name == (hasSplit ? member.displayName.substr(0, split) : member.displayName));
        CHECK(b.party[i].name.find(" / ") == std::string::npos);
        CHECK(b.party[i].level == member.level);
        CHECK(b.party[i].health == game::DungeonEffectiveHealth(boot.registry, member));
        CHECK(b.party[i].maxHealth == game::DungeonMemberMaxHealth(boot.registry, member));
        CHECK(b.party[i].role == (hasSplit ? member.displayName.substr(split + 3) : unit->displayName));
        CHECK(b.party[i].sprite == unit->spriteId);
    }
    CHECK(b.party[0].name == "Alden" && b.party[0].role == "Warrior");
}

// ---------------------------------------------------------------------------------------------
// F12-F14: layout, drawing, art
// ---------------------------------------------------------------------------------------------

bool Inside(const game::ui::FRect& inner, const game::ui::FRect& outer) {
    constexpr float eps = 0.01f;
    return inner.x >= outer.x - eps && inner.y >= outer.y - eps && inner.right() <= outer.right() + eps &&
           inner.bottom() <= outer.bottom() + eps;
}

void BriefingLayoutFits() {
    const std::vector<std::pair<float, float>> windows{{1100.f, 740.f}, {1160.f, 800.f}, {1280.f, 800.f}, {1920.f, 1080.f}};
    for (const auto& [w, h] : windows) {
        for (const float fontScale : {1.0f, 1.25f}) {
            const auto l = game::ComputeBriefingLayout(w, h, fontScale);
            CHECK(Inside(l.panel, game::ui::FRect{0.f, 0.f, w, h}));
            const game::ui::FRect inner{l.panel.x + 22.f, l.panel.y + 18.f, l.panel.w - 44.f, l.panel.h - 36.f};
            for (const auto* band : {&l.banner, &l.headerText, &l.expedition, &l.objective, &l.route, &l.threats,
                                     &l.party, &l.reward, &l.button}) {
                CHECK(Inside(*band, inner));
            }
            CHECK(l.objective.y >= std::max(l.banner.bottom(), l.expedition.bottom()) - 0.01f);
            CHECK(l.route.y >= l.objective.bottom() - 0.01f);
            CHECK(l.threats.y >= l.route.bottom() - 0.01f);
            CHECK(l.reward.y >= l.threats.bottom() - 0.01f);
            CHECK(l.button.y >= l.reward.bottom() - 0.01f);
            CHECK(l.threats.w >= 8.f * 76.f * l.u);
            CHECK(l.route.w / 10.f >= 48.f * static_cast<float>(l.k));
            CHECK(l.expedition.w / 4.f >= 120.f);
        }
    }
}

void BriefingViewDrawsAndDegrades() {
    FViewFixture fixture(UniquePath("wp50-view"));
    auto& registry = fixture.registry;
    game::StartRun(registry, kSeed, true);
    const auto built = game::BuildRunBriefing(registry);
    CHECK(built.has_value());
    const game::FRunBriefing briefing = *built;

    const std::string log = CaptureLog("wp50-view", [&] { game::DrawBriefingScreen(registry, briefing); });
    CHECK(Contains(log, briefing.dungeonName));
    CHECK(Contains(log, briefing.enterLabel));

    CHECK(!game::FlowPicture(registry, "art/ui/wp50_missing_picture").valid());
    CHECK(!game::FlowPicture(registry, "").valid());
    const auto icon = game::FlowPicture(registry, "art/ui/node_battle");
    CHECK(icon.valid() && icon.size.x == 48.f && icon.size.y == 48.f);

    game::FRunBriefing degraded = briefing;
    degraded.themeId = "theme.wp50_missing";
    for (auto& unit : degraded.threats) unit.sprite = "art/wp50_missing";
    for (auto& member : degraded.party) member.sprite = "art/wp50_missing";
    const std::string degradedLog = CaptureLog("wp50-view-degraded", [&] { game::DrawBriefingScreen(registry, degraded); });
    CHECK(Contains(degradedLog, degraded.dungeonName));
    CHECK(Contains(degradedLog, degraded.enterLabel));

    game::FRunBriefing crowded = briefing;
    crowded.threats.clear();
    for (int i = 1; i <= 12; ++i) crowded.threats.push_back({"t" + std::to_string(i), "T" + std::to_string(i), ""});
    const auto layout = game::ComputeBriefingLayout(1280.f, 800.f, 1.f);
    const int tiles = std::max(1, static_cast<int>(std::floor(layout.threats.w / (76.f * layout.u))));
    const std::string crowdedLog = CaptureLog("wp50-view-crowded", [&] { game::DrawBriefingScreen(registry, crowded); });
    CHECK(Contains(crowdedLog, "+" + std::to_string(12 - tiles + 1) + " more"));
    CHECK(!Contains(crowdedLog, "T12"));
}

std::pair<std::uint32_t, std::uint32_t> PngSize(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    unsigned char header[24]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    CHECK(file.gcount() == 24);
    const unsigned char signature[8]{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(std::equal(std::begin(signature), std::end(signature), std::begin(header)));
    const auto be32 = [&](int at) {
        return (static_cast<std::uint32_t>(header[at]) << 24) | (static_cast<std::uint32_t>(header[at + 1]) << 16) |
               (static_cast<std::uint32_t>(header[at + 2]) << 8) | static_cast<std::uint32_t>(header[at + 3]);
    };
    return {be32(16), be32(20)};
}

void FlowArtFilesPresent() {
    for (const std::string_view id : game::kFlowArtIds) {
        const auto [w, h] = PngSize("assets/" + std::string(id) + ".png");
        if (id.starts_with("art/ui/node_")) {
            CHECK(w == 48 && h == 48);
        } else if (id.starts_with("art/ui/theme_")) {
            CHECK(w == 384 && h == 160);
        } else {
            CHECK(id == game::kBossPlateId);
            CHECK(w == 512 && h == 96);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// F15-F16: autopilot
// ---------------------------------------------------------------------------------------------

void AutopilotEntersBriefing() {
    FBoot boot(UniquePath("wp50-pilot"), true);
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    const auto policy = game::dev::ParseAutopilotPolicy(game::dev::kDefaultAutopilotPolicy);
    CHECK(policy.has_value());
    game::dev::FRunAutopilot pilot{*policy};
    CHECK(pilot.tick(registry, "ember.briefing") == game::dev::EAutopilotStatus::Running);
    CHECK(registry.ctx().contains<game::FBriefingCommand>());
    CHECK(registry.ctx().get<game::FBriefingCommand>().value == game::EBriefingCommand::Enter);
    registry.ctx().erase<game::FBriefingCommand>();
    CHECK(pilot.tick(registry, "ember.briefing") == game::dev::EAutopilotStatus::Running);
    CHECK(!registry.ctx().contains<game::FBriefingCommand>());

    registry.ctx().insert_or_assign<game::FBriefingCommand>(game::FBriefingCommand{game::EBriefingCommand::Enter});
    FEmberBriefingState state;
    state.OnEnter(registry);
    registry.ctx().insert_or_assign<game::FBriefingCommand>(game::FBriefingCommand{game::EBriefingCommand::Enter});
    CHECK((state.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, "ember.battle"}));
}

void AutopilotDriveThroughBriefings() {
    FBoot boot(UniquePath("wp50-drive"), true);
    auto& registry = boot.registry;
    const auto policy = game::dev::ParseAutopilotPolicy(game::dev::kDefaultAutopilotPolicy);
    CHECK(policy.has_value());
    game::dev::FRunAutopilot pilot{*policy};

    std::string top = "ember.title";
    std::string previous;
    FEmberTitleState title;
    std::unique_ptr<FEmberPreparationState> prepare = std::make_unique<FEmberPreparationState>();
    std::unique_ptr<FEmberBriefingState> briefing;
    int briefingVisits = 0;
    bool reachedD1R1 = false;
    bool reachedD2 = false;

    const auto enter = [&](const FStateTransition& transition) {
        if (transition.kind != EStateChangeKind::Root && transition.kind != EStateChangeKind::Push &&
            transition.kind != EStateChangeKind::ReplaceTop) {
            return;
        }
        previous = top;
        top = transition.targetId;
        if (top == "ember.prepare") prepare = std::make_unique<FEmberPreparationState>();
        if (top == "ember.briefing") {
            briefing = std::make_unique<FEmberBriefingState>();
            briefing->OnEnter(registry);
            ++briefingVisits;
        }
        if (top == "ember.battle") {
            const auto& run = registry.ctx().get<game::FDungeonRun>();
            if (run.room == 0) CHECK(previous == "ember.briefing");
        }
    };

    for (int step = 0; step < 80; ++step) {
        const auto status = pilot.tick(registry, top);
        CHECK(status != game::dev::EAutopilotStatus::Stuck);
        if (const auto* run = registry.ctx().find<game::FDungeonRun>(); run != nullptr && top == "ember.battle") {
            if (run->depth == 1 && run->room == 1) reachedD1R1 = true;
            if (run->depth == 2) {
                reachedD2 = true;
                break;
            }
        }
        FStateTransition transition;
        if (top == "ember.title") {
            transition = title.Update(registry, 0.f);
        } else if (top == "ember.prepare") {
            transition = prepare->Update(registry, 0.f);
        } else if (top == "ember.briefing") {
            transition = briefing->Update(registry, 0.f);
        } else if (top == "ember.battle") {
            FEmberBattleState battle;
            battle.OnEnter(registry);
            const auto result = game::ResolveRoomVictory(registry, true);
            CHECK(result.has_value());
            battle.OnExit(registry);
            transition = game::ToStateTransition(*result, "ember.battle");
        } else {
            FEmberModalHostState host(top, EHostMode::RunPhase);
            transition = host.Update(registry, 0.f);
        }
        enter(transition);
    }
    CHECK(reachedD1R1);
    CHECK(reachedD2);
    CHECK(briefingVisits == 2);
    CHECK(top == "ember.battle");
}

// ---------------------------------------------------------------------------------------------
// F17-F19: boss introduction
// ---------------------------------------------------------------------------------------------

void BossIntroOnlyForBossBattles() {
    {
        FBoot boot(UniquePath("wp50-boss-model"), true);
        InstallShipRunAt(boot, 2);
        auto& run = boot.registry.ctx().get<game::FDungeonRun>();
        run.room = 0;
        CHECK(!game::MakeBossIntro(boot.registry).has_value());
        run.room = 2;
        const auto card = game::MakeBossIntro(boot.registry);
        CHECK(card.has_value() && card->name == "Stone Guardian" && card->note == "Guard stance");
        game::BeginBossIntro(boot.registry);
        CHECK(boot.registry.ctx().contains<game::FBossIntroState>());
        game::FSettings settings{};
        settings.skipAnimations = true;
        boot.registry.ctx().insert_or_assign<game::FSettings>(game::FSettings{settings});
        game::BeginBossIntro(boot.registry);
        CHECK(!boot.registry.ctx().contains<game::FBossIntroState>());
    }
    {
        FBoot boot(UniquePath("wp50-boss-model-3"), true);
        InstallShipRunAt(boot, 3);
        boot.registry.ctx().get<game::FDungeonRun>().room = 2;
        const auto card = game::MakeBossIntro(boot.registry);
        CHECK(card.has_value() && card->name == "Cinder Warden" && card->note == "Kindling");
    }
    // The battle hooks: a boss room fought, the same room continued at its reward step, and a plain room.
    const auto enterBattle = [](int index, int room, game::EDungeonPhase phase) {
        FBoot boot(UniquePath("wp50-boss-hook"), true);
        InstallShipRunAt(boot, index);
        auto& run = boot.registry.ctx().get<game::FDungeonRun>();
        run.room = room;
        run.phase = phase;
        FEmberBattleState battle;
        battle.OnEnter(boot.registry);
        const bool hasCard = boot.registry.ctx().contains<game::FBossIntroState>();
        battle.OnExit(boot.registry);
        CHECK(!boot.registry.ctx().contains<game::FBossIntroState>());
        return hasCard;
    };
    CHECK(enterBattle(2, 2, game::EDungeonPhase::Battle));
    CHECK(!enterBattle(2, 2, game::EDungeonPhase::Reward));
    CHECK(!enterBattle(2, 0, game::EDungeonPhase::Battle));
}

void BossIntroTimingAndDismiss() {
    const game::FBossIntroTiming timing{2.0f, 0.6f};
    game::FBossIntroState card{"Stone Guardian", "Guard stance"};
    game::AdvanceBossIntro(card, timing, 1.0f, false, false);
    CHECK(!card.done && std::abs(game::BossIntroAlpha(card, timing) - 1.f) < 0.001f);
    game::AdvanceBossIntro(card, timing, 1.3f, false, false);
    CHECK(!card.done && std::abs(game::BossIntroAlpha(card, timing) - 0.5f) < 0.01f);
    game::AdvanceBossIntro(card, timing, 0.31f, false, false);
    CHECK(card.done && game::BossIntroAlpha(card, timing) == 0.f);

    game::FBossIntroState dismissed{"X", "Y"};
    game::AdvanceBossIntro(dismissed, timing, 0.f, true, false);
    CHECK(dismissed.done);
    game::FBossIntroState skipped{"X", "Y"};
    game::AdvanceBossIntro(skipped, timing, 0.f, false, true);
    CHECK(skipped.done);
    game::FBossIntroState negative{"X", "Y"};
    game::AdvanceBossIntro(negative, timing, -1.f, false, false);
    CHECK(negative.elapsed == 0.f && !negative.done);

    // Never gating: a card in the context changes neither the presentation gate nor the battle's transition.
    FBattle fixture(UniquePath("wp50-boss-gate"));
    auto& registry = fixture.registry;
    const bool settledWithout = game::IsPresentationSettled(registry);
    const auto updateWithout = fixture.battle.Update(registry, 0.016f);
    registry.ctx().insert_or_assign<game::FBossIntroState>(game::FBossIntroState{"X", "Y"});
    CHECK(game::IsPresentationSettled(registry) == settledWithout);
    CHECK(fixture.battle.Update(registry, 0.016f) == updateWithout);
    game::EndBossIntro(registry);
    CHECK(!registry.ctx().contains<game::FBossIntroState>());

    // A click ends the card at once.
    game::FInputState::Initialize(registry);
    registry.ctx().insert_or_assign<game::FBossIntroState>(game::FBossIntroState{"X", "Y"});
    registry.ctx().get<game::FInputState>().mouseLeftPressed = true;
    game::TickBossIntro(registry, 0.016f);
    CHECK(!registry.ctx().contains<game::FBossIntroState>());
}

void BossIntroTimingFromData() {
    {
        FBoot boot(UniquePath("wp50-timing"), true);
        const auto& timing = game::BossIntroTiming(boot.registry);
        CHECK(timing.holdSeconds == 2.0f && timing.fadeSeconds == 0.6f);
    }
    {
        FFlowBoot boot("wp50-flow-35", [](const std::filesystem::path& dir) {
            WriteText(dir / "flow.json",
                      R"({"schema_version":1,"boss_intro":{"hold_seconds":3.5,"fade_seconds":0.6}})");
        });
        const auto& timing = game::BossIntroTiming(boot.registry);
        CHECK(timing.holdSeconds == 3.5f && timing.fadeSeconds == 0.6f);
    }
    {
        FFlowBoot boot("wp50-flow-99", [](const std::filesystem::path& dir) {
            WriteText(dir / "flow.json",
                      R"({"schema_version":1,"boss_intro":{"hold_seconds":99,"fade_seconds":0.4}})");
        });
        const auto& timing = game::BossIntroTiming(boot.registry);
        CHECK(timing.holdSeconds == 2.0f && timing.fadeSeconds == 0.6f);
    }
    {
        FFlowBoot boot("wp50-flow-none", [](const std::filesystem::path& dir) {
            WriteText(dir / "flow.json", R"({"schema_version":1})");
        });
        const auto& timing = game::BossIntroTiming(boot.registry);
        CHECK(timing.holdSeconds == 2.0f && timing.fadeSeconds == 0.6f);
    }
}

// ---------------------------------------------------------------------------------------------
// F20-F21: settings from the battle pause
// ---------------------------------------------------------------------------------------------

void PauseConfirmOffersSettings() {
    FViewFixture fixture(UniquePath("wp50-pause"));
    auto& registry = fixture.registry;
    game::StartRun(registry, kSeed, true);
    auto& ui = registry.ctx().get<game::FDungeonUiState>();
    ui.confirmAbandon = true;
    const std::string withSettings = CaptureLog("wp50-pause", [&] { game::DrawAbandonConfirm(registry, true, false, true); });
    CHECK(Contains(withSettings, "Settings"));
    CHECK(Contains(withSettings, "Keep exploring"));
    CHECK(Contains(withSettings, "Save and leave"));
    CHECK(Contains(withSettings, "Abandon expedition (checkpoint kept)"));
    ui.confirmAbandon = true;
    const std::string camp = CaptureLog("wp50-pause-camp", [&] { game::DrawAbandonConfirm(registry, true, true); });
    CHECK(!Contains(camp, "Settings"));
    CHECK(Contains(camp, "Keep exploring"));
}

void SettingsFromPauseRoundTrip() {
    FBattle fixture(UniquePath("wp50-settings"), true, false);
    auto& r = fixture.registry;
    r.ctx().emplace<game::FDungeonUiState>();
    game::RegisterModalViews(r);
    game::RegisterTitleMenuItems(r);
    const auto catalog = game::FStateCatalog::MakeEmber();
    game::GameStateMachine machine;
    machine.pushState("ember.battle", std::make_unique<FEmberBattleState>(), r);
    const auto rngBefore = r.ctx().get<game::FCombatRuntime>().rng.state;

    r.ctx().get<game::FDungeonUiState>().settingsRequested = true;
    const auto push = machine.update(r, 0.016f);
    CHECK((push == FStateTransition{EStateChangeKind::Push, "ember.settings"}));
    CHECK(machine.apply(push, catalog, r) == game::EStateApplyResult::Applied);
    CHECK(machine.stack().size() == 2);
    CHECK(machine.stack().back().id == "ember.settings");
    CHECK(!r.ctx().get<game::FDungeonUiState>().settingsRequested);
    CHECK((machine.update(r, 0.016f) == FStateTransition{}));

    game::RequestModalViewClose(r);
    const auto pop = machine.update(r, 0.016f);
    CHECK((pop == FStateTransition{EStateChangeKind::Pop, {}}));
    CHECK(machine.apply(pop, catalog, r) == game::EStateApplyResult::Applied);
    CHECK(machine.stack().size() == 1);
    CHECK(machine.stack().back().id == "ember.battle");
    CHECK(r.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Battle);
    CHECK(r.ctx().get<game::FCombatRuntime>().rng.state == rngBefore);

    r.ctx().get<game::FDungeonRun>().phase = game::EDungeonPhase::Reward;
    r.ctx().get<game::FDungeonUiState>().settingsRequested = true;
    const auto none = machine.update(r, 0.016f);
    CHECK(none.kind != EStateChangeKind::Push);
    CHECK(!r.ctx().get<game::FDungeonUiState>().settingsRequested);
    machine.unwindAll(r);
}

// ---------------------------------------------------------------------------------------------
// F22: never a blank screen
// ---------------------------------------------------------------------------------------------

void BriefingWithoutModelEntersBattle() {
    FBoot boot(UniquePath("wp50-nomodel"), true);
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    registry.ctx().erase<game::FRunSequence>();
    const auto before = registry.ctx().get<game::FDungeonRun>();
    FEmberBriefingState state;
    state.OnEnter(registry);
    FLogCapture capture;
    CHECK((state.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, "ember.battle"}));
    const std::string text = capture.text();
    const auto first = text.find("The briefing could not be built");
    CHECK(first != std::string::npos);
    CHECK(text.find("The briefing could not be built", first + 1) == std::string::npos);
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.depth == before.depth && run.room == before.room && run.phase == before.phase);
}

} // namespace

int main() {
    return game_test::RunTests("wp50 flow", {
        {"PrepareStartRootsBriefing", PrepareStartRootsBriefing},
        {"CampDescendRootsBriefing", CampDescendRootsBriefing},
        {"BriefingEntersBattleOnce", BriefingEntersBattleOnce},
        {"ContinueSkipsBriefing", ContinueSkipsBriefing},
        {"CatalogHasBriefing", CatalogHasBriefing},
        {"BriefingRoute", BriefingRoute},
        {"BriefingHeaderLines", BriefingHeaderLines},
        {"BriefingExpeditionRow", BriefingExpeditionRow},
        {"DungeonThreatUnitsRule", DungeonThreatUnitsRule},
        {"BriefingThreatsAndBoss", BriefingThreatsAndBoss},
        {"BriefingPartyMatchesRun", BriefingPartyMatchesRun},
        {"BriefingLayoutFits", BriefingLayoutFits},
        {"BriefingViewDrawsAndDegrades", BriefingViewDrawsAndDegrades},
        {"FlowArtFilesPresent", FlowArtFilesPresent},
        {"AutopilotEntersBriefing", AutopilotEntersBriefing},
        {"AutopilotDriveThroughBriefings", AutopilotDriveThroughBriefings},
        {"BossIntroOnlyForBossBattles", BossIntroOnlyForBossBattles},
        {"BossIntroTimingAndDismiss", BossIntroTimingAndDismiss},
        {"BossIntroTimingFromData", BossIntroTimingFromData},
        {"PauseConfirmOffersSettings", PauseConfirmOffersSettings},
        {"SettingsFromPauseRoundTrip", SettingsFromPauseRoundTrip},
        {"BriefingWithoutModelEntersBattle", BriefingWithoutModelEntersBattle},
    });
}
