// App-flow state stack v2 (WP-01): transition kinds, stack API, catalog injection, the modal visibility
// policy (a paused battle keeps rendering while its simulation stops), and the state -> transition mapping
// tables migrated from the closed legacy enum.
//
// Fakes record every lifecycle/render call into a shared log, so the assertions pin design intent
// (order, object identity, call counts) instead of "did not crash".
//
// The Engine tests (no state before services, shutdown unwind, boot paths) run headless through SDL's dummy
// video/audio drivers. If that harness ever stops working the fallback seam is GameStateMachine itself:
// Engine::shutdown is exactly one unwindAll() call and Engine::run() refuses an empty stack.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "core/Engine.h"
#include "core/InputState.h"
#include "core/SystemManager.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/run/FPrototypeRun.h"
#include "states/FBattleState.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberHubState.h"
#include "states/FEmberMenuRequest.h"
#include "states/FEmberTitleState.h"
#include "states/FHubState.h"
#include "states/FStateCatalog.h"
#include "states/FTitleState.h"
#include "states/GameStateMachine.h"

namespace {

struct FCallLog {
    std::vector<std::string> calls;

    void add(const std::string& entry) { calls.push_back(entry); }
    [[nodiscard]] std::size_t count(const std::string& entry) const {
        return static_cast<std::size_t>(std::ranges::count(calls, entry));
    }
};

class FRecordingState final : public FBaseState {
public:
    FRecordingState(std::string name, std::shared_ptr<FCallLog> log,
                    std::vector<std::string_view> required = {},
                    std::vector<std::string_view> renderSystems = {},
                    bool keepVisible = false)
        : stateName(std::move(name)), callLog(std::move(log)),
          requiredSystems(std::move(required)), renderSystems(std::move(renderSystems)),
          visibleUnderModals(keepVisible) {}

    FStateTransition Update(entt::registry&, float) override {
        callLog->add(stateName + ".update");
        return {};
    }
    void Render(entt::registry&) override { callLog->add(stateName + ".render"); }
    [[nodiscard]] std::string_view GetName() const noexcept override { return stateName; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return requiredSystems; }
    [[nodiscard]] bool KeepVisibleUnderModals() const override { return visibleUnderModals; }
    [[nodiscard]] std::span<const std::string_view> RequiredRenderSystems() const noexcept override { return renderSystems; }

    void OnEnter(entt::registry&) override { callLog->add(stateName + ".enter"); }
    void OnExit(entt::registry&) override { callLog->add(stateName + ".exit"); }
    void OnPause(entt::registry&) override { callLog->add(stateName + ".pause"); }
    void OnResume(entt::registry&) override { callLog->add(stateName + ".resume"); }

private:
    std::string stateName;
    std::shared_ptr<FCallLog> callLog;
    std::vector<std::string_view> requiredSystems;
    std::vector<std::string_view> renderSystems;
    bool visibleUnderModals;
};

struct FSystemCounts {
    int updates{0};
    int renders{0};
};

class FCountingSystem final : public game::ecs::ISystem {
public:
    FCountingSystem(std::string name, std::shared_ptr<FSystemCounts> counts)
        : systemName(std::move(name)), systemCounts(std::move(counts)) {}

    void update(entt::registry&, float) override { ++systemCounts->updates; }
    void render(entt::registry&) override { ++systemCounts->renders; }
    [[nodiscard]] std::string name() const override { return systemName; }

private:
    std::string systemName;
    std::shared_ptr<FSystemCounts> systemCounts;
};

[[nodiscard]] std::unique_ptr<FBaseState> Recording(std::string name, std::shared_ptr<FCallLog> log,
                                                    std::vector<std::string_view> required = {},
                                                    std::vector<std::string_view> renderSystems = {},
                                                    bool keepVisible = false) {
    return std::make_unique<FRecordingState>(std::move(name), std::move(log), std::move(required),
                                             std::move(renderSystems), keepVisible);
}

/// A windowless engine: SDL dummy video/audio and the software renderer, the same drivers the ctest
/// smoke tests use. False means the harness cannot run in this environment.
[[nodiscard]] bool InitializeHeadless(game::Engine& engine) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    return engine.initialize("state-machine-tests", 320, 200);
}

/// One key-down through the real input path (what Engine::processInput feeds the states).
void PressKey(entt::registry& registry, SDL_Scancode scancode) {
    auto* input = registry.ctx().find<game::FInputState>();
    CHECK(input != nullptr);
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    (void)input->processEvent(event);
}

[[nodiscard]] std::string Describe(const FStateTransition& transition) {
    return "kind " + std::to_string(static_cast<int>(transition.kind)) + " id '" + transition.targetId + "'";
}

/// Compare one mapping-table row; the failure names the state and its return point, not just the expression.
void CheckRow(const char* label, const char* source, const FStateTransition& actual, const FStateTransition& expected) {
    if (actual == expected) return;
    throw std::runtime_error(std::string("mapping row '") + label + "' (" + source + ") returned " + Describe(actual) +
                             ", expected " + Describe(expected));
}

/// A mapping-table row: one state return point and the transition WP-01 §4 assigns to it.
struct FMappingRow {
    const char* label;
    const char* source;
    FStateTransition actual;
    FStateTransition expected;
};

/// GAME01P_START is read through SDL_getenv, which reads SDL's own environment snapshot — so the tests write
/// through the same API instead of the CRT's _putenv_s.
void SetEnvironmentVariable(const char* name, const char* value) {
    (void)SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
}

void UnsetEnvironmentVariable(const char* name) {
    (void)SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name);
}

/// Unsets on construction and restores the unset state on scope exit, so a thrown CHECK cannot leak GAME01P_START
/// into a later test (the tests share one process and one SDL environment).
struct FScopedEnvironmentVariable {
    std::string name;

    explicit FScopedEnvironmentVariable(std::string variable) : name(std::move(variable)) {
        UnsetEnvironmentVariable(name.c_str());
    }
    ~FScopedEnvironmentVariable() { UnsetEnvironmentVariable(name.c_str()); }

    FScopedEnvironmentVariable(const FScopedEnvironmentVariable&) = delete;
    FScopedEnvironmentVariable& operator=(const FScopedEnvironmentVariable&) = delete;

    void set(const char* value) { SetEnvironmentVariable(name.c_str(), value); }
};

// ------------------------------------------------------------------------------------------------
// Tests
// ------------------------------------------------------------------------------------------------

/// Both shipped catalogs resolve every registered id and expose a contained start/debug-battle id.
void CatalogResolvesEveryRegisteredId() {
    const auto check = [](game::FStateCatalog catalog, std::initializer_list<std::string_view> expected) {
        const auto ids = catalog.ids();
        CHECK(ids.size() == expected.size());
        for (const auto id : expected) CHECK(catalog.contains(id));
        for (const auto& id : ids) CHECK(catalog.create(id) != nullptr);
        CHECK(catalog.contains(catalog.startId()));
        CHECK(catalog.contains(catalog.debugBattleId()));
        CHECK(catalog.create("no.such.state") == nullptr);

        std::vector<std::string> unique(ids.begin(), ids.end());
        std::ranges::sort(unique);
        CHECK(std::ranges::adjacent_find(unique) == unique.end());
    };

    check(game::FStateCatalog::MakeEmber(), {"ember.title", "ember.hub", "ember.battle"});
    check(game::FStateCatalog::MakeForest(), {"forest.title", "forest.hub", "forest.battle"});

    const auto ember = game::FStateCatalog::MakeEmber();
    CHECK(ember.startId() == "ember.title");
    CHECK(ember.debugBattleId() == "ember.battle");
    const auto forest = game::FStateCatalog::MakeForest();
    CHECK(forest.startId() == "forest.title");
    CHECK(forest.debugBattleId() == "forest.battle");
}

/// A modal above the battle: the battle's render system draws once per frame with its update off,
/// its simulation system is disabled, and the battle is paused (not exited).
void PausedBattleUpdatesNothing() {
    entt::registry registry;
    game::SystemManager systems;
    auto renderCounts = std::make_shared<FSystemCounts>();
    auto simulationCounts = std::make_shared<FSystemCounts>();
    systems.addSystem(std::make_unique<FCountingSystem>("R", renderCounts));
    systems.addSystem(std::make_unique<FCountingSystem>("S", simulationCounts));

    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    machine.pushState("battle", Recording("Battle", log, {"R", "S"}, {"R"}, true), registry);

    // Frame 1: the battle is active, so both systems update and render normally.
    CHECK(game::ApplyStackSystemPolicy(machine, systems).empty());
    (void)machine.update(registry, 0.016f);
    systems.updateEnabled(registry, 0.016f);
    systems.renderEnabled(registry);
    CHECK(simulationCounts->updates == 1);
    CHECK(renderCounts->updates == 1);
    CHECK(renderCounts->renders == 1);

    machine.pushState("modal", Recording("Modal", log), registry);
    CHECK(machine.stack().size() == 2);
    CHECK(log->count("Battle.pause") == 1);
    CHECK(log->count("Battle.exit") == 0);

    // Frame 2: one frame under the modal. R keeps rendering (update 0), S is fully off.
    CHECK(game::ApplyStackSystemPolicy(machine, systems).empty());
    CHECK(!systems.isEnabled("R"));
    CHECK(systems.isRenderEnabled("R"));
    CHECK(!systems.isEnabled("S"));
    CHECK(!systems.isRenderEnabled("S"));
    (void)machine.update(registry, 0.016f);
    systems.updateEnabled(registry, 0.016f);
    systems.renderEnabled(registry);
    CHECK(renderCounts->updates == 1);   // unchanged: no input/animation step while paused
    CHECK(renderCounts->renders == 2);   // one more draw
    CHECK(simulationCounts->updates == 1);
    CHECK(simulationCounts->renders == 1);  // fully disabled: it does not draw while paused
    CHECK(log->count("Battle.update") == 1); // state update is top-only: the paused battle never ticks
    CHECK(log->count("Modal.update") == 1);
}

/// renderStack draws bottom -> top, and only the top plus states that opt into modal visibility.
void RenderOrderBottomToTop() {
    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    machine.pushState("bottom", Recording("Bottom", log, {}, {}, true), registry);
    machine.pushState("middle", Recording("Middle", log, {}, {}, false), registry);
    machine.pushState("top", Recording("Top", log, {}, {}, false), registry);

    log->calls.clear();  // drop the enter entries: the assertion is about render order only
    machine.renderStack(registry);
    CHECK((log->calls == std::vector<std::string>{"Bottom.render", "Top.render"}));

    // The top always draws even when it does not ask for visibility.
    CHECK(log->count("Middle.render") == 0);
}

/// push -> pop returns to the same state object (history preserved), with OnResume exactly once.
void PushPopRestoresLowerStateIdentity() {
    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();

    auto lower = Recording("Lower", log);
    const FBaseState* lowerIdentity = lower.get();
    machine.pushState("lower", std::move(lower), registry);
    CHECK(machine.currentState() == lowerIdentity);

    machine.pushState("upper", Recording("Upper", log), registry);
    CHECK(machine.currentState() != lowerIdentity);

    CHECK(machine.popState(registry));
    CHECK(machine.currentState() == lowerIdentity);
    CHECK(machine.stack().size() == 1);
    CHECK(machine.stack()[0].id == "lower");
    CHECK(log->count("Lower.pause") == 1);
    CHECK(log->count("Lower.resume") == 1);
    CHECK(log->count("Upper.exit") == 1);
}

/// Root exits every state top -> bottom (before the new state enters), then leaves depth 1.
void RootUnwindsThenPushes() {
    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    game::FStateCatalog catalog;
    CHECK(catalog.add("fake.root", [log] { return Recording("Root", log); }));

    machine.pushState("a", Recording("A", log), registry);
    machine.pushState("b", Recording("B", log), registry);
    machine.pushState("c", Recording("C", log), registry);

    CHECK(machine.apply({EStateChangeKind::Root, "fake.root"}, catalog, registry) == game::EStateApplyResult::Applied);
    CHECK(machine.stack().size() == 1);
    CHECK(machine.stack()[0].id == "fake.root");
    CHECK(machine.currentState() != nullptr);

    const auto position = [&log](const std::string& entry) {
        const auto found = std::ranges::find(log->calls, entry);
        return found == log->calls.end() ? std::string::npos : static_cast<std::size_t>(found - log->calls.begin());
    };
    CHECK(log->count("A.exit") == 1);
    CHECK(log->count("B.exit") == 1);
    CHECK(log->count("C.exit") == 1);
    CHECK(position("C.exit") < position("B.exit"));
    CHECK(position("B.exit") < position("A.exit"));
    CHECK(position("A.exit") < position("Root.enter"));

    // Root with an empty target unwinds only.
    CHECK(machine.apply({EStateChangeKind::Root, {}}, catalog, registry) == game::EStateApplyResult::Applied);
    CHECK(machine.stack().empty());
    CHECK(log->count("Root.exit") == 1);
}

/// Modal-to-modal replacement keeps the depth and does not pause/resume the state below.
void ReplaceTopKeepsDepth() {
    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    game::FStateCatalog catalog;
    CHECK(catalog.add("fake.replacement", [log] { return Recording("Replacement", log); }));

    machine.pushState("a", Recording("A", log), registry);
    machine.pushState("b", Recording("B", log), registry);
    CHECK(machine.stack().size() == 2);

    CHECK(machine.apply({EStateChangeKind::ReplaceTop, "fake.replacement"}, catalog, registry) == game::EStateApplyResult::Applied);
    CHECK(machine.stack().size() == 2);
    CHECK(machine.stack()[0].id == "a");
    CHECK(machine.stack()[1].id == "fake.replacement");
    CHECK(log->count("B.exit") == 1);
    CHECK(log->count("A.exit") == 0);
    CHECK(log->count("A.pause") == 1);
    CHECK(log->count("A.resume") == 0);
}

/// An unknown or empty target fails before the stack is touched.
void UnknownIdLeavesStackIntact() {
    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    game::FStateCatalog catalog;
    CHECK(catalog.add("fake.known", [log] { return Recording("Known", log); }));

    machine.pushState("a", Recording("A", log), registry);
    machine.pushState("b", Recording("B", log), registry);
    const FBaseState* topIdentity = machine.currentState();
    const auto exitsBefore = log->count("A.exit") + log->count("B.exit");
    const auto pausesBefore = log->count("A.pause") + log->count("B.pause");

    CHECK(machine.apply({EStateChangeKind::ReplaceTop, "missing"}, catalog, registry) == game::EStateApplyResult::Failed);
    CHECK(machine.apply({EStateChangeKind::Push, "missing"}, catalog, registry) == game::EStateApplyResult::Failed);
    CHECK(machine.apply({EStateChangeKind::ReplaceTop, {}}, catalog, registry) == game::EStateApplyResult::Failed);

    CHECK(machine.stack().size() == 2);
    CHECK(machine.stack()[1].id == "b");
    CHECK(machine.currentState() == topIdentity);
    CHECK(log->count("A.exit") + log->count("B.exit") == exitsBefore);
    CHECK(log->count("A.pause") + log->count("B.pause") == pausesBefore);
}

/// Pop cannot remove the root: the stack must keep one state (invariant i/ii).
void PopOnRootFails() {
    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    game::FStateCatalog catalog;
    CHECK(catalog.add("fake.known", [log] { return Recording("Known", log); }));

    machine.pushState("root", Recording("Root", log), registry);
    CHECK(machine.apply({EStateChangeKind::Pop, {}}, catalog, registry) == game::EStateApplyResult::Failed);
    CHECK(machine.stack().size() == 1);
    CHECK(log->count("Root.exit") == 0);

    // Even on an empty stack the transition fails rather than inventing a state.
    machine.popState(registry);
    CHECK(machine.stack().empty());
    CHECK(machine.apply({EStateChangeKind::Pop, {}}, catalog, registry) == game::EStateApplyResult::Failed);
    CHECK(machine.stack().empty());
}

/// initialize() creates no state; run() refuses to boot without one (and without a catalog).
void NoStateBeforeServices() {
    {
        game::Engine engine;
        engine.setStateCatalog(game::FStateCatalog::MakeEmber());
        CHECK(InitializeHeadless(engine));
        CHECK(engine.getStateMachine().stack().empty());
        CHECK(!engine.run(0));
        engine.shutdown();
    }
    {
        game::Engine engine; // no catalog injected
        CHECK(InitializeHeadless(engine));
        CHECK(!engine.run(0));
        engine.shutdown();
    }
}

/// The start state is created exactly once, from the catalog's own id (never assembled in Engine).
void StartStateCreatedOnce() {
    FScopedEnvironmentVariable start("GAME01P_START");

    // Both shipped catalogs: the id selector is pure and the real engine boots on startId()/debugBattleId().
    for (const bool ember : {true, false}) {
        auto catalog = ember ? game::FStateCatalog::MakeEmber() : game::FStateCatalog::MakeForest();
        const std::string expectedStart(catalog.startId());
        const std::string expectedBattle(catalog.debugBattleId());
        CHECK(game::StartupStateId(catalog, false) == expectedStart);
        CHECK(game::StartupStateId(catalog, true) == expectedBattle);

        game::Engine engine;
        engine.setStateCatalog(std::move(catalog));
        CHECK(InitializeHeadless(engine));
        engine.applyStartupStateOverride(); // GAME01P_START unset => the normal start path

        const auto stack = engine.getStateMachine().stack();
        CHECK(stack.size() == 1);
        CHECK(stack[0].id == expectedStart);
        engine.shutdown();
    }

    // A fake catalog makes the boot path observable: factory call counts and OnEnter.
    // The factories capture `this`, so the FFakeBoot instance outlives the engine it booted.
    struct FFakeBoot {
        std::shared_ptr<FCallLog> log = std::make_shared<FCallLog>();
        int titleFactories{0};
        int battleFactories{0};
        game::FStateCatalog catalog;

        FFakeBoot() {
            (void)catalog.add("fake.title", [this] { ++titleFactories; return Recording("Title", log); });
            (void)catalog.add("fake.battle", [this] { ++battleFactories; return Recording("Battle", log); });
            catalog.setStartIds("fake.title", "fake.battle");
        }
    };

    // (a) Normal boot: title created + entered once, battle never.
    {
        FFakeBoot boot;
        game::Engine engine;
        engine.setStateCatalog(boot.catalog);
        CHECK(InitializeHeadless(engine));
        engine.applyStartupStateOverride();

        CHECK(engine.getStateMachine().stack().size() == 1);
        CHECK(engine.getStateMachine().stack()[0].id == boot.catalog.startId());
        CHECK(boot.titleFactories == 1);
        CHECK(boot.battleFactories == 0);
        CHECK(boot.log->count("Title.enter") == 1);

        // The override is a Root transition: a second call replaces the top, it never stacks a second state.
        engine.applyStartupStateOverride();
        CHECK(engine.getStateMachine().stack().size() == 1);
        CHECK(engine.getStateMachine().stack()[0].id == boot.catalog.startId());
        CHECK(boot.titleFactories == 2);
        CHECK(boot.log->count("Title.exit") == 1);
        CHECK(boot.log->count("Title.enter") == 2);
        engine.shutdown();
    }

    // (b) GAME01P_START=battle: the debug battle id is created + entered once, the title never.
    {
        FFakeBoot boot;
        game::Engine engine;
        engine.setStateCatalog(boot.catalog);
        CHECK(InitializeHeadless(engine));
        start.set("battle");
        engine.applyStartupStateOverride();

        CHECK(engine.getStateMachine().stack().size() == 1);
        CHECK(engine.getStateMachine().stack()[0].id == boot.catalog.debugBattleId());
        CHECK(boot.battleFactories == 1);
        CHECK(boot.titleFactories == 0);
        CHECK(boot.log->count("Battle.enter") == 1);
        engine.shutdown();
    }
    UnsetEnvironmentVariable("GAME01P_START");

    // Root creates the target once and enters it once.
    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    game::FStateCatalog catalog;
    int factoryCalls = 0;
    CHECK(catalog.add("fake.start", [&factoryCalls, log] {
        ++factoryCalls;
        return Recording("Start", log);
    }));
    CHECK(machine.apply({EStateChangeKind::Root, "fake.start"}, catalog, registry) == game::EStateApplyResult::Applied);
    CHECK(factoryCalls == 1);
    CHECK(log->count("Start.enter") == 1);
    CHECK(log->count("Start.exit") == 0);
    CHECK(machine.stack().size() == 1);
}

/// Quit is reported, never performed; shutdown unwinds the whole stack once, top -> bottom.
void ShutdownUnwindsWholeStack() {
    // Shipped catalog on a real engine: the stack empties, no state survives.
    {
        game::Engine engine;
        engine.setStateCatalog(game::FStateCatalog::MakeEmber());
        CHECK(InitializeHeadless(engine));
        engine.applyStartupStateOverride();
        CHECK(!engine.getStateMachine().stack().empty());
        engine.shutdown();
        CHECK(engine.getStateMachine().stack().empty());
    }

    // Fake catalog on a real engine: the engine itself exits the state exactly once, and neither a second
    // shutdown() nor the destructor unwinds it again.
    auto shutdownLog = std::make_shared<FCallLog>();
    {
        game::FStateCatalog catalog;
        CHECK(catalog.add("fake.only", [shutdownLog] { return Recording("Only", shutdownLog); }));
        catalog.setStartIds("fake.only", "fake.only");

        game::Engine engine;
        engine.setStateCatalog(std::move(catalog));
        CHECK(InitializeHeadless(engine));
        engine.applyStartupStateOverride();
        CHECK(engine.getStateMachine().stack().size() == 1);

        engine.shutdown();
        CHECK(engine.getStateMachine().stack().empty());
        CHECK(shutdownLog->count("Only.exit") == 1);

        engine.shutdown(); // idempotent: nothing left to unwind
        CHECK(shutdownLog->count("Only.exit") == 1);
    }
    CHECK(shutdownLog->count("Only.exit") == 1); // the destructor did not exit it a second time

    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    game::FStateCatalog catalog;
    machine.pushState("a", Recording("A", log), registry);
    machine.pushState("b", Recording("B", log), registry);
    machine.pushState("c", Recording("C", log), registry);

    // Quit is only reported: the stack and every state stay untouched (the Engine turns it into running=false).
    const std::vector<std::string> beforeQuit = log->calls;
    CHECK(machine.apply({EStateChangeKind::Quit, {}}, catalog, registry) == game::EStateApplyResult::Quit);
    CHECK(machine.stack().size() == 3);
    CHECK(log->calls == beforeQuit);

    // Only the shutdown unwind exits everything, top -> bottom, with no resume.
    machine.unwindAll(registry);

    CHECK(machine.stack().empty());
    CHECK(log->count("A.exit") == 1);
    CHECK(log->count("B.exit") == 1);
    CHECK(log->count("C.exit") == 1);
    CHECK(log->count("A.pause") == 1);
    CHECK(log->count("A.resume") == 0); // unwind does not resume anything

    const auto position = [&log](const std::string& entry) {
        const auto found = std::ranges::find(log->calls, entry);
        return found == log->calls.end() ? std::string::npos : static_cast<std::size_t>(found - log->calls.begin());
    };
    CHECK(position("C.exit") < position("B.exit"));
    CHECK(position("B.exit") < position("A.exit"));
}

/// forest: every reachable return point maps 1:1 to the WP-01 §4 table.
void ForestMappingTable() {
    // Rows deliberately NOT re-driven here (they are asserted elsewhere):
    //  - FBattleState resolved / abandon / cancel -> hub: `--forest --verify-prototype`
    //    ("battle result advances app flow") and PrototypeChecks' auto-combat path;
    //  - FHubState proceed -> battle: PrototypeChecks "event continuation reaches next battle" and
    //    "room flow enters next battle";
    //  - the ImGui buttons (Prepare party, Start run, Title): immediate mode, not drivable headless.
    const std::filesystem::path checkpoint = std::filesystem::path("out") / "state-machine-forest-checkpoint.json";
    auto loaded = game_test::LoadTestRegistry(TestAssetRoots(), checkpoint.string());
    CHECK(loaded.content);
    auto& registry = loaded.registry;

    auto run = game::FPrototypeRun::Load(registry.ctx().get<game::gameplay::FContentRegistry>());
    CHECK(run.has_value());
    CHECK(run->rooms.front().kind == game::EPrototypeRoom::Battle);
    auto& runState = registry.ctx().emplace<game::FPrototypeRun>(std::move(*run));
    game::FInputState::Initialize(registry);
    auto& input = registry.ctx().get<game::FInputState>();

    std::vector<FMappingRow> rows;

    FTitleState title;
    PressKey(registry, SDL_SCANCODE_RETURN);
    rows.push_back({"FTitleState Confirm", "FTitleState::Update", title.Update(registry, 0.f),
                    {EStateChangeKind::ReplaceTop, std::string(kForestHubId)}});
    input.beginFrame();

    // The prepared party starts a run whose first room is a battle, so the hub hands over without input.
    CHECK(runState.Start(registry.ctx().get<game::gameplay::FContentRegistry>()));
    CHECK(runState.CurrentRoom() != nullptr && runState.CurrentRoom()->kind == game::EPrototypeRoom::Battle);
    FHubState hub;
    hub.OnEnter(registry);
    rows.push_back({"FHubState in a battle room", "FHubState::Update room", hub.Update(registry, 0.f),
                    {EStateChangeKind::ReplaceTop, std::string(kForestBattleId)}});
    input.beginFrame();

    // Cancel leaves the battle room for the title and ends the run (behaviour unchanged).
    PressKey(registry, SDL_SCANCODE_ESCAPE);
    rows.push_back({"FHubState Cancel", "FHubState::Update back", hub.Update(registry, 0.f),
                    {EStateChangeKind::ReplaceTop, std::string(kForestTitleId)}});
    input.beginFrame();
    CHECK(!runState.active);

    // Update without OnEnter: the battle never became ready, so it bounces back to the hub.
    FBattleState battle;
    rows.push_back({"FBattleState not ready", "FBattleState::Update", battle.Update(registry, 0.f),
                    {EStateChangeKind::ReplaceTop, std::string(kForestHubId)}});

    for (const FMappingRow& row : rows) CheckRow(row.label, row.source, row.actual, row.expected);

    std::error_code error;
    std::filesystem::remove(checkpoint, error);
}

/// ember: every reachable return point maps 1:1 to the WP-01 §4 table.
void EmberMappingTable() {
    // Rows deliberately NOT re-driven here (asserted by dungeon_rules / DungeonTests.cpp):
    //  - FEmberBattleState leave / abandon -> ReplaceTop ember.hub, next room -> Root ember.battle;
    //  - ResolveEmberMenuRequest Continue -> ReplaceTop ember.battle;
    //  - the ImGui menu buttons are immediate mode and not drivable headless.
    entt::registry registry;
    game::FInputState::Initialize(registry);
    auto& input = registry.ctx().get<game::FInputState>();

    std::vector<FMappingRow> rows;

    FEmberTitleState title;
    PressKey(registry, SDL_SCANCODE_RETURN);
    rows.push_back({"FEmberTitleState Confirm", "FEmberTitleState::Update", title.Update(registry, 0.f),
                    {EStateChangeKind::ReplaceTop, std::string(kEmberHubId)}});
    input.beginFrame();

    FEmberHubState hub;
    PressKey(registry, SDL_SCANCODE_RETURN);
    rows.push_back({"FEmberHubState Confirm", "FEmberHubState::Update Confirm", hub.Update(registry, 0.f),
                    {EStateChangeKind::ReplaceTop, std::string(kEmberBattleId)}});
    input.beginFrame();

    PressKey(registry, SDL_SCANCODE_ESCAPE);
    rows.push_back({"FEmberHubState Cancel", "FEmberHubState::Update Cancel", hub.Update(registry, 0.f),
                    {EStateChangeKind::ReplaceTop, std::string(kEmberTitleId)}});
    input.beginFrame();

    // The shared menu request: Quit is a plain Quit transition (no target id).
    entt::registry menu;
    menu.ctx().emplace<game::FMenuRequest>().value = game::EMenuRequest::Quit;
    const auto quit = ResolveEmberMenuRequest(menu);
    CHECK(quit.has_value());
    rows.push_back({"ResolveEmberMenuRequest Quit", "FEmberMenuRequest.cpp Quit",
                    quit.value_or(FStateTransition{}), {EStateChangeKind::Quit, {}}});

    // Update without OnEnter: not ready -> hub.
    FEmberBattleState battle;
    rows.push_back({"FEmberBattleState not ready", "FEmberBattleState::Update", battle.Update(registry, 0.f),
                    {EStateChangeKind::ReplaceTop, std::string(kEmberHubId)}});

    for (const FMappingRow& row : rows) CheckRow(row.label, row.source, row.actual, row.expected);
}

/// A2: Root clears every battle frame before the new battle enters, so the old battle's ctx erase cannot
/// wipe the new battle's context.
void BattleUniquenessUnderRoot() {
    entt::registry registry;
    game::GameStateMachine machine;
    auto log = std::make_shared<FCallLog>();
    game::FStateCatalog catalog;
    int created = 0;
    CHECK(catalog.add("fake.battle", [&created, log] {
        return Recording("Battle" + std::to_string(++created), log);
    }));

    machine.pushState("fake.battle", catalog.create("fake.battle"), registry);
    machine.pushState("fake.modal", Recording("Modal", log), registry);
    CHECK(machine.stack().size() == 2);

    CHECK(machine.apply({EStateChangeKind::Root, "fake.battle"}, catalog, registry) == game::EStateApplyResult::Applied);
    CHECK(machine.stack().size() == 1);
    CHECK(machine.stack()[0].id == "fake.battle");

    const auto battleFrames = std::ranges::count_if(machine.stack(), [](const game::GameStateMachine::FStackEntry& entry) {
        return entry.id.ends_with(".battle");
    });
    CHECK(battleFrames == 1);
    CHECK(log->count("Battle1.exit") == 1);
    CHECK(log->count("Battle2.enter") == 1);

    const auto position = [&log](const std::string& entry) {
        const auto found = std::ranges::find(log->calls, entry);
        return found == log->calls.end() ? std::string::npos : static_cast<std::size_t>(found - log->calls.begin());
    };
    // The old battle's OnExit (ctx erase) finished before the new battle's OnEnter.
    CHECK(position("Battle1.exit") < position("Battle2.enter"));
}

} // namespace

int main() {
    return game_test::RunTests("state machine", {
        {"catalog resolves every registered id", CatalogResolvesEveryRegisteredId},
        {"paused battle updates nothing but keeps rendering", PausedBattleUpdatesNothing},
        {"render order is bottom to top", RenderOrderBottomToTop},
        {"push/pop restores the lower state identity", PushPopRestoresLowerStateIdentity},
        {"root unwinds then pushes", RootUnwindsThenPushes},
        {"replace top keeps depth", ReplaceTopKeepsDepth},
        {"unknown id leaves the stack intact", UnknownIdLeavesStackIntact},
        {"pop on root fails", PopOnRootFails},
        {"no state before services", NoStateBeforeServices},
        {"start state created once", StartStateCreatedOnce},
        {"shutdown unwinds the whole stack", ShutdownUnwindsWholeStack},
        {"forest mapping table", ForestMappingTable},
        {"ember mapping table", EmberMappingTable},
        {"battle uniqueness under root", BattleUniquenessUnderRoot},
    });
}
