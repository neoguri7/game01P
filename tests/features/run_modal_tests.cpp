// WP-02b: the run-flow -> app-state map, the modal hosts, the title/prepare/runEnd contract and the views.
//
// Every test asserts the design intent: the exact FStateTransition, the stack depth, the run state, the
// save on disk or the event log. The modal views are rendered headless; the buttons are driven by the
// same run intents the view posts, because a headless ImGui cannot click.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <SDL3/SDL.h>

#include "core/Engine.h"
#include "core/EngineState.h"
#include "core/InputState.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "core/SystemManager.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonViews.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FPreparationView.h"
#include "presentation/FTitleMenuView.h"
#include "states/FBaseState.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberBriefingState.h"
#include "states/FEmberHubState.h"
#include "states/FEmberMenuRequest.h"
#include "states/FEmberModalHostState.h"
#include "states/FEmberPreparationState.h"
#include "states/FEmberRunEndState.h"
#include "states/FEmberTitleState.h"
#include "states/FStateCatalog.h"
#include "states/FRunFlowStateMap.h"
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

std::string ReadBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

nlohmann::json ReadJsonFile(const std::string& path) {
    std::ifstream file(path);
    CHECK(file.good());
    nlohmann::json document;
    file >> document;
    return document;
}

// --- a booted registry with its own checkpoint and save slot -----------------------------------
// `prepPools` boots the SHIPPED content (the real 4-slot unit pools and the shipped unlock gates): the
// preparation screen's default draft is only startable there — the frozen fixture still carries the
// pre-WP-22 3-skill pools, and splicing the shipped tables under it cannot boot (the shipped rows
// cross-reference content the fixture snapshot does not carry). The other cases keep the fixture.
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

// --- a battle with a real FEmberBattleState (scene, combat setup, turn runtime) ----------------
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

// --- a view fixture: ImGui + software renderer + dungeon art + the UI registry -----------------
struct FViewFixture {
    // Destruction order: the ImGui context, then the registry (which frees its textures), then the
    // renderer and surface. The members are declared in that order.
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

// A recording state for the stack tests: its Render and lifecycle are logged; no gameplay.
class FRecordingState final : public FBaseState {
public:
    FRecordingState(std::string name, std::vector<std::string_view> required = {},
                    std::vector<std::string_view> renderSystems = {}, bool keepVisible = false)
        : stateName(std::move(name)), requiredSystems(std::move(required)),
          renderSystems(std::move(renderSystems)), visibleUnderModals(keepVisible) {}

    FStateTransition Update(entt::registry&, float) override { return {}; }
    void Render(entt::registry&) override { ++renderCount; }
    [[nodiscard]] std::string_view GetName() const noexcept override { return stateName; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return requiredSystems; }
    [[nodiscard]] bool KeepVisibleUnderModals() const override { return visibleUnderModals; }
    [[nodiscard]] std::span<const std::string_view> RequiredRenderSystems() const noexcept override { return renderSystems; }

    int renderCount{0};

private:
    std::string stateName;
    std::vector<std::string_view> requiredSystems;
    std::vector<std::string_view> renderSystems;
    bool visibleUnderModals;
};

class FCountingSystem final : public game::ecs::ISystem {
public:
    FCountingSystem(std::string name) : systemName(std::move(name)) {}
    void update(entt::registry&, float) override {}
    void render(entt::registry&) override {}
    [[nodiscard]] std::string name() const override { return systemName; }

private:
    std::string systemName;
};

// --- the state map ------------------------------------------------------------------------------

void RunFlowStateMapTable() {
    const auto battle = game::ToStateTransition({game::ERunExit::ShowModal, std::string(game::kRunModalReward)}, "ember.battle");
    CHECK((battle == FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));

    for (const auto top : {game::kRunModalReward, game::kRunModalEvent, game::kRunModalCamp}) {
        const auto next = game::ToStateTransition({game::ERunExit::ShowModal, std::string(game::kRunModalEvent)}, top);
        CHECK((next == FStateTransition{EStateChangeKind::ReplaceTop, std::string(game::kRunModalEvent)}));
    }

    const auto nextRoom = game::ToStateTransition({game::ERunExit::NextRoom, {}}, "ember.event");
    CHECK((nextRoom == FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)}));

    for (const auto exit : {game::ERunExit::LeaveRun, game::ERunExit::ToTitle}) {
        const auto title = game::ToStateTransition({exit, {}}, "ember.battle");
        CHECK((title == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
    }
    const auto prepare = game::ToStateTransition({game::ERunExit::ToPrepare, {}}, "ember.runEnd");
    CHECK((prepare == FStateTransition{EStateChangeKind::Root, std::string(kEmberPrepareId)}));

    CHECK((game::ToStateTransition({game::ERunExit::None, {}}, "ember.battle") == FStateTransition{}));

    // Unknown top with ShowModal is a wiring error: None, no invented state.
    const auto unknown = game::ToStateTransition({game::ERunExit::ShowModal, std::string(game::kRunModalReward)}, "ember.hub");
    CHECK((unknown == FStateTransition{}));
    // A ShowModal without a modal id is also a wiring error.
    const auto noId = game::ToStateTransition({game::ERunExit::ShowModal, {}}, "ember.battle");
    CHECK((noId == FStateTransition{}));
}

// --- the modal hosts ----------------------------------------------------------------------------

void RunModalHasNoPop() {
    FBoot boot(UniquePath("modal-no-pop"));
    game::StartRun(boot.registry, kSeed, true);
    for (const auto id : {game::kRunModalReward, game::kRunModalEvent, game::kRunModalCamp}) {
        FEmberModalHostState host(std::string(id), EHostMode::RunPhase);
        const auto transition = host.Update(boot.registry, 0.f);
        CHECK((transition == FStateTransition{}));
        CHECK(transition.kind != EStateChangeKind::Pop);
    }
}

void RewardToCampReplacesTop() {
    FBoot boot(UniquePath("modal-camp"));
    game::StartRun(boot.registry, kSeed, true);
    auto& run = boot.registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Reward;
    run.room = 2;
    FEmberModalHostState host(std::string(game::kRunModalReward), EHostMode::RunPhase);
    game::PostRunIntent(boot.registry, game::ERunIntent::ContinueReward);
    const auto transition = host.Update(boot.registry, 0.f);
    CHECK((transition == FStateTransition{EStateChangeKind::ReplaceTop, std::string(game::kRunModalCamp)}));
    CHECK(run.phase == game::EDungeonPhase::Camp);
    CHECK(run.cleared == 1);
}

void DefeatPushesRunEndOnce() {
    FBattle fixture(UniquePath("modal-defeat"));
    auto& registry = fixture.registry;
    auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    outcome.resolved = true;
    outcome.victory = false;
    CHECK((fixture.battle.Update(registry, 0) == FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalRunEnd)}));
    CHECK((fixture.battle.Update(registry, 0) == FStateTransition{}));
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Defeat);
    CHECK(!registry.ctx().get<game::FDungeonSave>().exists);
    CHECK(!std::filesystem::exists(fixture.runSave));
}

void ContinueIntoSavedPhasePushesModalOnce() {
    FBattle fixture(UniquePath("modal-continue"));
    auto& registry = fixture.registry;
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(registry));
    fixture.battle.OnExit(registry);
    fixture.battle.OnEnter(registry);
    CHECK((fixture.battle.Update(registry, 0) == FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
    CHECK((fixture.battle.Update(registry, 0) == FStateTransition{}));
}

void ContinueDoesNotRefireRoomClear() {
    FBattle fixture(UniquePath("modal-no-refire"));
    auto& registry = fixture.registry;
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    const auto& saved = registry.ctx().get<game::FDungeonRun>();
    const int materials = saved.materials;
    const auto runes = saved.runes.size();
    const auto experience = saved.party.front().experience;
    registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(registry));
    fixture.battle.OnExit(registry);
    fixture.battle.OnEnter(registry);
    CHECK((fixture.battle.Update(registry, 0) == FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
    CHECK((fixture.battle.Update(registry, 0) == FStateTransition{}));
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.materials == materials && run.runes.size() == runes && run.party.front().experience == experience);
}

void NextRoomUnwindsToDepthOne() {
    FBoot boot(UniquePath("modal-next-room"));
    game::StartRun(boot.registry, kSeed, true);
    auto& run = boot.registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Event;

    game::GameStateMachine machine;
    game::FStateCatalog catalog;
    auto log = std::make_shared<int>(0);
    CHECK(catalog.add("ember.battle", [log] {
        return std::unique_ptr<FBaseState>(std::make_unique<FRecordingState>("Battle"));
    }));
    machine.pushState("ember.battle", catalog.create("ember.battle"), boot.registry);
    auto host = std::make_unique<FEmberModalHostState>(std::string(game::kRunModalEvent), EHostMode::RunPhase);
    machine.pushState("ember.event", std::move(host), boot.registry);
    CHECK(machine.stack().size() == 2);

    game::PostRunIntent(boot.registry, game::ERunIntent::ChooseEventLoot);
    const auto transition = machine.update(boot.registry, 0.f);
    CHECK((transition == FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)}));
    CHECK(machine.apply(transition, catalog, boot.registry) == game::EStateApplyResult::Applied);
    CHECK(machine.stack().size() == 1);
    CHECK(machine.stack()[0].id == "ember.battle");
}

void ModalHostsShareOneClass() {
    const auto catalog = game::FStateCatalog::MakeEmber();
    for (const auto id : {game::kRunModalReward, game::kRunModalEvent, game::kRunModalCamp,
                          kEmberSettingsId, kEmberCompendiumId}) {
        const auto state = catalog.create(id);
        CHECK(state != nullptr);
        CHECK(dynamic_cast<FEmberModalHostState*>(state.get()) != nullptr);
    }
}

void StubSettingsPopsToTitle() {
    FBoot boot(UniquePath("modal-settings"));
    FEmberModalHostState host(std::string(kEmberSettingsId), EHostMode::Menu);
    const auto transition = host.Update(boot.registry, 0.f);
    CHECK((transition == FStateTransition{EStateChangeKind::Pop, {}}));
}

void UnknownEmberIdFailsLoudly() {
    const auto catalog = game::FStateCatalog::MakeEmber();
    CHECK(catalog.create("ember.typo") == nullptr);
    CHECK(catalog.create("ember.reward") != nullptr);
}

// --- title -> prepare -> battle ----------------------------------------------------------------

void TitleNewGoesThroughPrepareOnce() {
    FBoot boot(UniquePath("title-prepare"), true);
    auto& registry = boot.registry;
    registry.ctx().get<game::FMenuRequest>().value = game::EMenuRequest::Start;
    registry.ctx().get<game::FMenuRequest>().fresh = false;
    const auto next = ResolveEmberMenuRequest(registry);
    CHECK(next.has_value() && (*next == FStateTransition{EStateChangeKind::Root, std::string(kEmberPrepareId)}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());  // the title never starts a run
    CHECK(registry.ctx().contains<game::FRunStartRequest>());
    CHECK(registry.ctx().get<game::FRunStartRequest>().fresh == false);

    // WP-21b: the run starts when the player confirms on the hub, not on entering it — the command is the
    // view's Start click (the headless seam is the same ctx object).
    registry.ctx().insert_or_assign<game::FPreparationCommand>(game::FPreparationCommand{game::EPreparationCommand::Start});
    FEmberPreparationState prepare;
    const auto battle = prepare.Update(registry, 0.f);
    CHECK((battle == FStateTransition{EStateChangeKind::Root, std::string(kEmberBriefingId)}));
    CHECK(registry.ctx().contains<game::FDungeonRun>());
    CHECK(!registry.ctx().contains<game::FRunStartRequest>());  // consumed once
}

void PrepareStartsExactlyOneRun() {
    FBoot boot(UniquePath("prepare-once"), true);
    auto& registry = boot.registry;
    registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{true, -1});
    FEmberPreparationState prepare;
    // The first update builds the default draft and consumes the carrier; without a confirm nothing starts.
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    CHECK(!registry.ctx().contains<game::FRunStartRequest>());
    registry.ctx().insert_or_assign<game::FPreparationCommand>(game::FPreparationCommand{game::EPreparationCommand::Start});
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberBriefingId)}));
    auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.depth == 1 && run.room == 0);
    // A second update must not start a second run; it refuses and returns to the title. The marker proves
    // it: a reinstall would reset the materials.
    run.materials = 777;
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
    CHECK(registry.ctx().get<game::FDungeonRun>().depth == 1);
    CHECK(registry.ctx().get<game::FDungeonRun>().materials == 777);
}

void RetryUsesCheckpointNotFresh() {
    FBoot boot(UniquePath("retry-checkpoint"), true);
    auto& registry = boot.registry;
    registry.ctx().get<game::FDungeonCheckpoint>().cleared = 1;
    game::StartRun(registry, kSeed, false);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.depth == 2);
    run.phase = game::EDungeonPhase::Defeat;

    FEmberRunEndState runEnd;
    game::PostRunIntent(registry, game::ERunIntent::RetryAfterDefeat);
    CHECK((runEnd.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberPrepareId)}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    CHECK(registry.ctx().get<game::FRunStartRequest>().fresh == false);

    // WP-21b: the retry run starts when the player confirms on the hub.
    registry.ctx().insert_or_assign<game::FPreparationCommand>(game::FPreparationCommand{game::EPreparationCommand::Start});
    FEmberPreparationState prepare;
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberBriefingId)}));
    CHECK(registry.ctx().get<game::FDungeonRun>().depth == 2);
}

void GAME01P_STARTBattleStillStarts() {
    // The auto-start in OnEnter is the GAME01P_START=battle path: no run, no prepare, the battle creates one.
    FBattle fixture(UniquePath("battle-auto"), false, true);
    CHECK(fixture.registry.ctx().contains<game::FDungeonRun>());
    CHECK(fixture.registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Battle);
}

void TitleStartDoesNotDoubleStart() {
    FBoot boot(UniquePath("title-start-once"));
    auto& registry = boot.registry;
    registry.ctx().get<game::FMenuRequest>().value = game::EMenuRequest::Start;
    registry.ctx().get<game::FMenuRequest>().fresh = true;
    const auto next = ResolveEmberMenuRequest(registry);
    CHECK(next.has_value() && (*next == FStateTransition{EStateChangeKind::Root, std::string(kEmberPrepareId)}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    CHECK(registry.ctx().get<game::FRunStartRequest>().fresh);
    CHECK(registry.ctx().get<game::FMenuRequest>().value == game::EMenuRequest::None);
}

void ContinueLoadsThenRootsBattle() {
    FBoot boot(UniquePath("continue-load"));
    game::StartRun(boot.registry, kSeed, true);
    boot.registry.ctx().erase<game::FDungeonRun>();
    boot.registry.ctx().get<game::FMenuRequest>().value = game::EMenuRequest::Continue;
    const auto next = ResolveEmberMenuRequest(boot.registry);
    CHECK(next.has_value() && (*next == FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)}));
    CHECK(boot.registry.ctx().contains<game::FDungeonRun>());
    CHECK(boot.registry.ctx().get<game::FMenuRequest>().value == game::EMenuRequest::None);
    // The save that was continued is the run's own save: its phase is still the battle phase.
    CHECK(ReadJsonFile(boot.runSave)["run"]["phase"] == "battle");
}

void ContinueHiddenWithoutSave() {
    FViewFixture fixture(UniquePath("continue-hidden"));
    const auto& registry = fixture.registry;
    const auto& menu = registry.ctx().get<game::FTitleMenuRegistry>();
    const auto found = std::ranges::find_if(menu.items, [](const game::FTitleMenuItem& item) {
        return item.id == "title.continue";
    });
    CHECK(found != menu.items.end());
    CHECK(!found->visible);  // no save exists
    CHECK(!registry.ctx().get<game::FDungeonSave>().exists);
}

// --- views and composition ---------------------------------------------------------------------

void ViewsRegisteredFromCompositionRoot() {
    FViewFixture fixture(UniquePath("views-registered"));
    for (const auto id : {game::kRunModalReward, game::kRunModalEvent, game::kRunModalCamp, game::kRunModalRunEnd}) {
        CHECK(game::FindModalView(fixture.registry, id) != nullptr);
    }
}

void HubShowsTitleMenuOrNeverEntered() {
    FViewFixture fixture(UniquePath("hub-title"));
    game::StartRun(fixture.registry, kSeed, true);
    const auto* view = game::FindModalView(fixture.registry, game::kRunModalReward);
    CHECK(view != nullptr);
    ImGui::NewFrame();
    ImGui::LogToFile(0, (std::filesystem::path("out") / "hub-title-ui.log").string().c_str());
    FEmberHubState hub;
    hub.Render(fixture.registry);
    ImGui::LogFinish();
    ImGui::Render();
    std::ifstream capture("out/hub-title-ui.log");
    CHECK(capture.good());
    const std::string log{std::istreambuf_iterator<char>(capture), std::istreambuf_iterator<char>()};
    CHECK(log.find("CONTINUE") != std::string::npos);
    CHECK(log.find("BEGIN") != std::string::npos);
    CHECK(log.find("QUIT") != std::string::npos);
}

void MenuViewsRegisterInTitleAnchor() {
    std::ifstream file("src/presentation/FTitleMenuView.cpp");
    CHECK(file.good());
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(text.find("[WP-24a]") != std::string::npos);
    CHECK(text.find("[WP-24b]") != std::string::npos);
    CHECK(text.find("RegisterTitleMenuItems") != std::string::npos);
}

void ModalRenderOnlyWhenTop() {
    entt::registry registry;
    registry.ctx().emplace<int>(0);
    registry.ctx().emplace<game::FModalViewRegistry>();
    registry.ctx().get<game::FModalViewRegistry>().views.push_back(
        {std::string(game::kRunModalReward), [](entt::registry& r) { ++r.ctx().get<int>(); }});

    game::GameStateMachine machine;
    machine.pushState("fake.battle", std::make_unique<FRecordingState>("Battle", std::vector<std::string_view>{},
        std::vector<std::string_view>{}, true), registry);
    auto host = std::make_unique<FEmberModalHostState>(std::string(game::kRunModalReward), EHostMode::RunPhase);
    machine.pushState("ember.reward", std::move(host), registry);
    machine.renderStack(registry);
    CHECK(registry.ctx().get<int>() == 1);
    CHECK(machine.stack().size() == 2);

    (void)machine.popState(registry);
    machine.renderStack(registry);
    CHECK(registry.ctx().get<int>() == 1);  // the host is gone: its Render did not run
}

void ModalBlocksHudClicks() {
    game::SystemManager systems;
    systems.addSystem(std::make_unique<FCountingSystem>("EmberCombatHudSystem"));
    systems.addSystem(std::make_unique<FCountingSystem>("EmberGridRenderSystem"));
    systems.addSystem(std::make_unique<FCountingSystem>("BattleAnimationSystem"));

    entt::registry registry;
    game::GameStateMachine stack;
    stack.pushState("ember.battle", std::make_unique<FRecordingState>("Battle",
        std::vector<std::string_view>{"EmberCombatHudSystem"},
        std::vector<std::string_view>{"EmberCombatHudSystem", "EmberGridRenderSystem", "BattleAnimationSystem"}, true), registry);
    stack.pushState("ember.reward", std::make_unique<FEmberModalHostState>(std::string(game::kRunModalReward), EHostMode::RunPhase), registry);
    const auto missing = game::ApplyStackSystemPolicy(stack, systems);
    CHECK(missing.empty());
    CHECK(!systems.isEnabled("EmberCombatHudSystem"));
    CHECK(systems.isRenderEnabled("EmberCombatHudSystem"));

    // The popup is a BeginPopupModal in every view: that is what blocks HUD clicks.
    for (const char* path : {"src/presentation/FDungeonRewardView.cpp", "src/presentation/FDungeonEventView.cpp",
                             "src/presentation/FDungeonCampView.cpp", "src/presentation/FDungeonDefeatView.cpp"}) {
        std::ifstream file(path);
        CHECK(file.good());
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        CHECK(text.find("BeginPopupModal") != std::string::npos);
    }
}

void ViewsPostIntentHostApplies() {
    FBoot boot(UniquePath("view-intent"));
    game::StartRun(boot.registry, kSeed, true);
    auto& run = boot.registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Reward;
    // A view posts the intent; the host applies it.
    game::PostRunIntent(boot.registry, game::ERunIntent::ContinueReward);
    FEmberModalHostState host(std::string(game::kRunModalReward), EHostMode::RunPhase);
    CHECK((host.Update(boot.registry, 0.f) == FStateTransition{EStateChangeKind::ReplaceTop, std::string(game::kRunModalEvent)}));
    CHECK(run.phase == game::EDungeonPhase::Event);
}

void PostRunIntentLastWins() {
    FBoot boot(UniquePath("intent-last"));
    game::PostRunIntent(boot.registry, game::ERunIntent::ContinueReward);
    game::PostRunIntent(boot.registry, game::ERunIntent::ChooseEventLoot);
    CHECK(boot.registry.ctx().get<game::FRunIntentRequest>().intent == game::ERunIntent::ChooseEventLoot);
}

void ViewsDoNotChangePhase() {
    FViewFixture fixture(UniquePath("views-no-phase"));
    game::StartRun(fixture.registry, kSeed, true);
    auto& run = fixture.registry.ctx().get<game::FDungeonRun>();
    const std::string before = ReadBytes(fixture.runSave);
    for (const auto phase : {game::EDungeonPhase::Reward, game::EDungeonPhase::Event,
                             game::EDungeonPhase::Camp, game::EDungeonPhase::Defeat}) {
        run.phase = phase;
        const auto* view = game::FindModalView(fixture.registry, game::RunModalIdForPhase(phase));
        CHECK(view != nullptr);
        ImGui::NewFrame();
        view->render(fixture.registry);
        ImGui::Render();
        CHECK(run.phase == phase);
        CHECK(ReadBytes(fixture.runSave) == before);
    }
}

void LeaveKeepsRunWhenSaveFails() {
    FBattle fixture(UniquePath("leave-fail"));
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.leaveRequested = true;
    const std::uint32_t seed = run.seed;
    const int depth = run.depth;
    // The save path is a directory, so the write fails. The run must stay and the request must be cleared.
    registry.ctx().get<game::FDungeonSave>().path = "out";
    CHECK((fixture.battle.Update(registry, 0) == FStateTransition{}));
    CHECK(registry.ctx().contains<game::FDungeonRun>());
    CHECK(!run.leaveRequested);
    CHECK(!run.message.empty());
    CHECK(run.seed == seed && run.depth == depth);
}

void AbandonLabelAndConfirm() {
    FViewFixture fixture(UniquePath("abandon-confirm"));
    game::StartRun(fixture.registry, kSeed, true);
    auto& ui = fixture.registry.ctx().get<game::FDungeonUiState>();
    ui.confirmAbandon = true;
    ImGui::NewFrame();
    ImGui::LogToFile(0, (std::filesystem::path("out") / "abandon-confirm-ui.log").string().c_str());
    game::DrawAbandonConfirm(fixture.registry, true, false);
    ImGui::LogFinish();
    ImGui::Render();
    std::ifstream capture("out/abandon-confirm-ui.log");
    CHECK(capture.good());
    const std::string log{std::istreambuf_iterator<char>(capture), std::istreambuf_iterator<char>()};
    CHECK(log.find("Abandon expedition (checkpoint kept)") != std::string::npos);
    CHECK(log.find("run EXP, nodes, runes and materials") != std::string::npos);
    CHECK(log.find("cleared-dungeon checkpoint remains") != std::string::npos);
}

void BattleConfirmOnlyInBattlePhase() {
    FViewFixture fixture(UniquePath("battle-confirm"));
    auto& registry = fixture.registry;
    game::StartRun(registry, kSeed, true);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    auto& ui = registry.ctx().get<game::FDungeonUiState>();
    FEmberBattleState battle;
    battle.OnEnter(registry);

    // A camp host is above the battle: the battle must not open the shared confirm (the camp owns it), and
    // Cancel must not set a battle flag while the battle is paused.
    run.phase = game::EDungeonPhase::Camp;
    CHECK(!game::BattleOwnsScreen(registry));
    game::FInputState::Initialize(registry);
    auto& input = registry.ctx().get<game::FInputState>();
    ui.confirmAbandon = false;
    SDL_Event cancel{};
    cancel.type = SDL_EVENT_KEY_DOWN;
    cancel.key.scancode = SDL_SCANCODE_ESCAPE;
    input.beginFrame(false, false);
    input.processEvent(cancel);
    CHECK((battle.Update(registry, 0.f) == FStateTransition{}));
    CHECK(!ui.confirmAbandon);
    ui.confirmAbandon = true;
    ImGui::NewFrame();
    battle.Render(registry);
    CHECK(!ImGui::IsPopupOpen("Abandon expedition?"));
    ImGui::Render();
    CHECK(!run.leaveRequested && !run.abandonRequested);

    // Back in the battle phase the battle owns the screen: Cancel opens the confirm and its Render draws it.
    run.phase = game::EDungeonPhase::Battle;
    CHECK(game::BattleOwnsScreen(registry));
    ui.confirmAbandon = false;
    input.beginFrame(false, false);
    input.processEvent(cancel);
    CHECK((battle.Update(registry, 0.f) == FStateTransition{}));
    CHECK(ui.confirmAbandon);
    ImGui::NewFrame();
    battle.Render(registry);
    CHECK(ImGui::IsPopupOpen("Abandon expedition?"));
    ImGui::Render();
    CHECK(!run.leaveRequested && !run.abandonRequested);
}

void RunEndRetryStartsExactlyOneRun() {
    FBoot boot(UniquePath("runend-retry"), true);
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Defeat;
    FEmberRunEndState runEnd;
    game::PostRunIntent(registry, game::ERunIntent::RetryAfterDefeat);
    CHECK((runEnd.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberPrepareId)}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    // A second update without a fresh intent has no run to act on: it hands back to the title, it never
    // starts another prepare or a second run.
    CHECK((runEnd.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));

    registry.ctx().insert_or_assign<game::FPreparationCommand>(game::FPreparationCommand{game::EPreparationCommand::Start});
    FEmberPreparationState prepare;
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberBriefingId)}));
    CHECK(registry.ctx().contains<game::FDungeonRun>());
    // The second update refuses (a run already exists): it never starts a second run.
    registry.ctx().get<game::FDungeonRun>().materials = 777;
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
    CHECK(registry.ctx().get<game::FDungeonRun>().materials == 777);
}

// --- WP-46: run completion --------------------------------------------------------------------

/// WP-46 §7 recipe: the camp of dungeon `index` (0-based), reached through ContinueReward.
void CampAfterDungeon(entt::registry& registry, int index) {
    registry.ctx().get<game::FDungeonCheckpoint>().cleared = index;
    game::InstallDungeonRunAt(registry, kSeed, index);
    const auto* sequence = game::FindRunSequence(registry);
    CHECK(sequence != nullptr);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Reward;
    run.room = sequence->BattleCountAt(sequence->PositionOf(index)) - 1;  // a battle ordinal, as the save validates
    if (auto* rewards = registry.ctx().find<game::FRewards>()) rewards->offer.active = false;
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Camp);
}

void CampAndRunEndAcceptVictoryIntents() {
    {
        FBoot boot(UniquePath("modal-victory"));
        auto& registry = boot.registry;
        const int last = static_cast<int>(game::FindRunSequence(registry)->themes[0].dungeons.size()) - 1;
        CampAfterDungeon(registry, last);
        FEmberModalHostState host(std::string(game::kRunModalCamp), EHostMode::RunPhase);
        game::PostRunIntent(registry, game::ERunIntent::ReturnInTriumph);
        CHECK((host.Update(registry, 0.f) ==
               FStateTransition{EStateChangeKind::ReplaceTop, std::string(game::kRunModalRunEnd)}));
        CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Victory);

        FEmberRunEndState runEnd;
        game::PostRunIntent(registry, game::ERunIntent::RetryAfterDefeat);
        CHECK((runEnd.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberPrepareId)}));
        const auto* start = registry.ctx().find<game::FRunStartRequest>();
        CHECK(start != nullptr && !start->fresh);
    }
    {
        FBoot boot(UniquePath("modal-camp-leave"));
        auto& registry = boot.registry;
        CampAfterDungeon(registry, 0);
        FEmberModalHostState host(std::string(game::kRunModalCamp), EHostMode::RunPhase);
        game::PostRunIntent(registry, game::ERunIntent::LeaveRun);
        CHECK((host.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
        CHECK(!registry.ctx().contains<game::FDungeonRun>());
        CHECK(std::filesystem::exists(boot.runSave));
        CHECK(ReadJsonFile(boot.runSave)["run"]["phase"] == "camp");
    }
}

} // namespace

int main() {
    return game_test::RunTests("run modal", {
        {"RunFlowStateMapTable", RunFlowStateMapTable},
        {"RunModalHasNoPop", RunModalHasNoPop},
        {"RewardToCampReplacesTop", RewardToCampReplacesTop},
        {"DefeatPushesRunEndOnce", DefeatPushesRunEndOnce},
        {"ContinueIntoSavedPhasePushesModalOnce", ContinueIntoSavedPhasePushesModalOnce},
        {"NextRoomUnwindsToDepthOne", NextRoomUnwindsToDepthOne},
        {"ModalHostsShareOneClass", ModalHostsShareOneClass},
        {"StubSettingsPopsToTitle", StubSettingsPopsToTitle},
        {"UnknownEmberIdFailsLoudly", UnknownEmberIdFailsLoudly},
        {"TitleNewGoesThroughPrepareOnce", TitleNewGoesThroughPrepareOnce},
        {"PrepareStartsExactlyOneRun", PrepareStartsExactlyOneRun},
        {"RetryUsesCheckpointNotFresh", RetryUsesCheckpointNotFresh},
        {"GAME01P_STARTBattleStillStarts", GAME01P_STARTBattleStillStarts},
        {"TitleStartDoesNotDoubleStart", TitleStartDoesNotDoubleStart},
        {"ContinueLoadsThenRootsBattle", ContinueLoadsThenRootsBattle},
        {"ContinueHiddenWithoutSave", ContinueHiddenWithoutSave},
        {"ViewsRegisteredFromCompositionRoot", ViewsRegisteredFromCompositionRoot},
        {"ModalRenderOnlyWhenTop", ModalRenderOnlyWhenTop},
        {"HubShowsTitleMenuOrNeverEntered", HubShowsTitleMenuOrNeverEntered},
        {"MenuViewsRegisterInTitleAnchor", MenuViewsRegisterInTitleAnchor},
        {"ModalBlocksHudClicks", ModalBlocksHudClicks},
        {"ViewsPostIntentHostApplies", ViewsPostIntentHostApplies},
        {"PostRunIntentLastWins", PostRunIntentLastWins},
        {"ViewsDoNotChangePhase", ViewsDoNotChangePhase},
        {"LeaveKeepsRunWhenSaveFails", LeaveKeepsRunWhenSaveFails},
        {"AbandonLabelAndConfirm", AbandonLabelAndConfirm},
        {"BattleConfirmOnlyInBattlePhase", BattleConfirmOnlyInBattlePhase},
        {"ContinueDoesNotRefireRoomClear", ContinueDoesNotRefireRoomClear},
        {"RunEndRetryStartsExactlyOneRun", RunEndRetryStartsExactlyOneRun},
        {"CampAndRunEndAcceptVictoryIntents", CampAndRunEndAcceptVictoryIntents},
    });
}
