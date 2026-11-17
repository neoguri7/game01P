// WP-05 battle-flow tests (05a + 05b). The unit (gameplay + ecs drivers) is exercised two ways:
//   * level predicate / fail-safe clock / gate counting / FEndTurn deferral / derived view / the TurnFlow
//     hold — on small hand-built registries, asserting exact values (gate.busy, deque contents, controller
//     snapshot tokens, AP/status/thinkDelay state, change-return of the compare-and-log step, hook counts);
//   * handoff and the stalemate chain — on a real FEmberBattleState battle (content + scene + turn runtime,
//     the run_modal_tests boot recipe), asserting the exact FStateTransition and the run/save state on disk.
// Grep tests pin the layer boundary (gameplay/ must not include ecs/systems/ or presentation/).

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "core/Engine.h"
#include "core/SystemManager.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/BattleAnimationSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/FBattleFlowSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FBattleFlow.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FBattleAnim.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberModalHostState.h"
#include "states/GameStateMachine.h"

namespace {

using game::EBattlePhase;
using game::EDungeonPhase;
using game::ETurnState;
using game::FPresentationGate;
using game::FPresentationWatch;
using game::FTurnRequest;
using game::FTurnToken;

constexpr std::uint32_t kSeed = 20261108;

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

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

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/// One animation frame on a registry with no battle contexts: UpdateBattleAnimations bails out early
/// (no FBattleEvents), the gate assignment still runs — which is exactly the count under test.
int BusyAfterAnimationFrame(entt::registry& registry, float deltaTime = 0.f) {
    game::ecs::BattleAnimationSystem animations;
    animations.update(registry, deltaTime);
    return registry.ctx().get<FPresentationGate>().busy;
}

/// Does a single unit's animation state hold the gate?
bool HoldsGate(const game::FUnitAnim& anim) {
    entt::registry registry;
    const entt::entity unit = registry.create();
    registry.emplace<game::FUnitAnim>(unit, anim);
    return BusyAfterAnimationFrame(registry) > 0;
}

[[nodiscard]] std::size_t CountEndTurnRequests(const std::deque<FTurnRequest>& requests) {
    return static_cast<std::size_t>(std::ranges::count_if(requests, [](const FTurnRequest& request) {
        return std::holds_alternative<game::FEndTurn>(request);
    }));
}

/// A minimal turn session: two units (fast party-side, slow), a gate context to switch the wait on and an
/// event bus so TurnSystem's flush has a target. No battle state — the deferral is a TurnSystem property.
struct FTurnFixture {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    entt::entity fast{entt::null};
    entt::entity slow{entt::null};

    FTurnFixture() {
        turns.onRegister(registry);
        registry.ctx().emplace<game::FEventBus>();
        registry.ctx().emplace<FPresentationGate>();
        fast = MakeParticipant(20);
        slow = MakeParticipant(10);
    }

    entt::entity MakeParticipant(std::int32_t speed) {
        const entt::entity unit = registry.create();
        registry.emplace<game::FTurnParticipant>(unit);
        registry.emplace<game::FSpeed>(unit, game::FSpeed{speed});
        return unit;
    }

    [[nodiscard]] game::FTurnSnapshot snapshot() {
        return registry.ctx().get<game::FTurnRuntime>().controller.snapshot();
    }

    [[nodiscard]] FTurnToken token() {
        const auto current = snapshot().currentTurn;
        CHECK(current.has_value());
        return *current;
    }

    [[nodiscard]] std::deque<FTurnRequest>& requests() {
        return registry.ctx().get<game::FTurnRuntime>().requests;
    }

    void SetGateBusy(int busy) { registry.ctx().get<FPresentationGate>().busy = busy; }

    void start(std::uint32_t seed = 7u) {
        requests().push_back(game::FStartTurnSession{seed});
        turns.update(registry, 0.f);
        CHECK(snapshot().state == ETurnState::Running);
    }

    [[nodiscard]] const std::vector<game::FTurnEnded>& endedEvents() {
        return registry.ctx().get<game::FEventBus>().frameEvents<game::FTurnEnded>();
    }
};

/// A real battle: the run_modal_tests boot recipe (content, dungeon, turn runtime, FEmberBattleState).
struct FBattleFixture {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;
    game::ecs::TurnSystem turns;
    FEmberBattleState battle;
    bool entered{false};

    explicit FBattleFixture(std::string path, bool startRun = true, bool enter = true) : checkpoint(std::move(path)) {
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

    ~FBattleFixture() {
        if (entered) battle.OnExit(registry);
        RemoveFiles(checkpoint);
        RemoveFiles(runSave);
    }

    void SetGateBusy(int busy) {
        if (!registry.ctx().contains<FPresentationGate>()) {
            registry.ctx().emplace<FPresentationGate>();
        }
        registry.ctx().get<FPresentationGate>().busy = busy;
    }
};

/// A saved-phase continue: the room was won, the run was saved, then reloaded at its Reward step and the
/// battle re-entered — the case the handoff must serve without re-resolving anything.
struct FContinuedBattle : public FBattleFixture {
    int materials{0};
    std::size_t runes{0};
    int experience{0};
    std::string saveBefore;

    explicit FContinuedBattle(std::string path) : FBattleFixture(std::move(path)) {
        auto& r = registry;
        CHECK(game::ResolveRoomVictory(r, true).has_value());
        const auto& won = r.ctx().get<game::FDungeonRun>();
        materials = won.materials;
        runes = won.runes.size();
        experience = won.party.front().experience;
        r.ctx().erase<game::FDungeonRun>();
        CHECK(game::ContinueDungeonRun(r));
        CHECK(r.ctx().get<game::FDungeonRun>().phase == EDungeonPhase::Reward);
        battle.OnExit(r);
        battle.OnEnter(r);
        saveBefore = ReadBytes(runSave);
    }
};

/// A system stub for the stack-policy test: registered under a name, never updates.
struct FNamedSystem final : game::ecs::ISystem {
    explicit FNamedSystem(std::string value) : nameValue(std::move(value)) {}
    void update(entt::registry&, float) override {}
    [[nodiscard]] std::string name() const override { return nameValue; }
    std::string nameValue;
};

// Compile-time probes for the "no stored phase" acceptance.
template<typename T>
concept HasPhaseField = requires(T value) { value.phase; };
template<typename T>
concept HasRoundNumberField = requires(T value) { value.roundNumber; };
template<typename T>
concept DerivableFromRegistry = requires(T& value) { game::DeriveBattlePhase(value); };

// ---------------------------------------------------------------------------------------------
// Predicate / clock
// ---------------------------------------------------------------------------------------------

void HeadlessSettlesImmediately() {
    entt::registry registry;
    CHECK(game::IsPresentationSettled(registry));  // no gate at all: nothing is being shown
    registry.ctx().emplace<FPresentationWatch>();
    CHECK(game::IsPresentationSettled(registry));  // a stale watch without a gate is still headless
}

void GateBusyBlocksSettle() {
    entt::registry registry;
    auto& gate = registry.ctx().emplace<FPresentationGate>();
    CHECK(game::IsPresentationSettled(registry));
    gate.busy = 1;
    CHECK(!game::IsPresentationSettled(registry));  // busy, no watch: not settled
    auto& watch = registry.ctx().emplace<FPresentationWatch>();
    CHECK(!game::IsPresentationSettled(registry));
    watch.timedOut = true;
    CHECK(game::IsPresentationSettled(registry));  // the fail-safe opens the gate
    gate.busy = 0;
    watch.timedOut = false;
    CHECK(game::IsPresentationSettled(registry));
}

void GateTimeoutForcesSettle() {
    entt::registry registry;
    auto& gate = registry.ctx().emplace<FPresentationGate>();
    auto& watch = registry.ctx().emplace<FPresentationWatch>();
    auto& log = registry.ctx().emplace<game::FCombatLog>();
    gate.busy = 1;

    for (int second = 0; second < 4; ++second) game::AdvancePresentationWatch(registry, 1.f);
    CHECK(watch.busySeconds == 4.f);
    CHECK(!watch.timedOut);  // strictly past the limit; 4.0 is not yet "stuck"
    CHECK(!game::IsPresentationSettled(registry));

    game::AdvancePresentationWatch(registry, 1.f);
    CHECK(watch.timedOut);
    CHECK(game::IsPresentationSettled(registry));  // the battle is allowed to continue
    CHECK(log.lines.size() == 1);                  // one warning line, not one per frame

    for (int second = 0; second < 3; ++second) game::AdvancePresentationWatch(registry, 1.f);
    CHECK(watch.timedOut && log.lines.size() == 1);

    gate.busy = 0;
    game::AdvancePresentationWatch(registry, 1.f);
    CHECK(watch.busySeconds == 0.f && !watch.timedOut);  // the streak ended: the clock and latch reset
    CHECK(game::IsPresentationSettled(registry));
}

void WatchFrozenUnderModal() {
    entt::registry registry;
    auto& gate = registry.ctx().emplace<FPresentationGate>();
    auto& watch = registry.ctx().emplace<FPresentationWatch>();
    gate.busy = 1;
    game::AdvancePresentationWatch(registry, 1.f);
    CHECK(watch.busySeconds == 1.f);

    // A modal above the battle disables the flow system (Engine::ApplyStackSystemPolicy), so the clock is
    // not advanced at all. Simulated here by simply not calling the advance.
    CHECK(watch.busySeconds == 1.f && !watch.timedOut);
    CHECK(!game::IsPresentationSettled(registry));
}

void TimeoutCountsConsecutiveBusy() {
    entt::registry registry;
    auto& gate = registry.ctx().emplace<FPresentationGate>();
    auto& watch = registry.ctx().emplace<FPresentationWatch>();
    gate.busy = 1;
    for (int second = 0; second < 3; ++second) game::AdvancePresentationWatch(registry, 1.f);
    CHECK(!watch.timedOut);

    gate.busy = 0;
    game::AdvancePresentationWatch(registry, 1.f);
    CHECK(watch.busySeconds == 0.f);  // the streak was broken...

    gate.busy = 1;
    for (int second = 0; second < 3; ++second) game::AdvancePresentationWatch(registry, 1.f);
    CHECK(watch.busySeconds == 3.f && !watch.timedOut);  // ...so the earlier three seconds do not count
    game::AdvancePresentationWatch(registry, 1.5f);
    CHECK(watch.timedOut);
}

// ---------------------------------------------------------------------------------------------
// Gate counting
// ---------------------------------------------------------------------------------------------

void GateCountsTimelineEffectsDeaths() {
    entt::registry registry;
    auto& timeline = registry.ctx().emplace<game::FBattleTimeline>();
    timeline.steps.push_back(game::FBattleTimeline::FStep{5.f, [](entt::registry&) {}});
    timeline.steps.push_back(game::FBattleTimeline::FStep{9.f, [](entt::registry&) {}});
    registry.ctx().emplace<game::FBattleEffects>().active.push_back(game::FActiveEffect{});
    const entt::entity unit = registry.create();
    game::FUnitAnim anim;
    anim.pendingDeaths = 2;
    registry.emplace<game::FUnitAnim>(unit, anim);

    CHECK(BusyAfterAnimationFrame(registry) == 2 + 1 + 1);  // steps + effects + the unit with a pending KO
    registry.ctx().get<game::FBattleTimeline>().steps.clear();
    CHECK(BusyAfterAnimationFrame(registry) == 1 + 1);
    registry.ctx().get<game::FBattleEffects>().active.clear();
    CHECK(BusyAfterAnimationFrame(registry) == 1);  // the KO still waits for its blow
    registry.get<game::FUnitAnim>(unit).pendingDeaths = 0;
    CHECK(BusyAfterAnimationFrame(registry) == 0);
}

void WalkHoldsGate() {
    game::FUnitAnim anim;
    anim.route.push_back({1.f, 1.f});
    CHECK(HoldsGate(anim));  // the walk is part of the acting turn
    anim.route.clear();
    CHECK(!HoldsGate(anim));  // idle, standing on its cell: no hold
}

void AttackSwingHoldsGate() {
    game::FUnitAnim anim;
    anim.state = game::EUnitAnim::Attack;
    anim.player.loop = false;  // attack clips are authored loop:false
    CHECK(HoldsGate(anim));    // the swing is still playing
    anim.player.finished = true;
    CHECK(!HoldsGate(anim));  // the swing finished: released
}

void HitFlinchDoesNotHold() {
    game::FUnitAnim anim;
    anim.state = game::EUnitAnim::Hit;
    anim.player.loop = false;
    CHECK(!HoldsGate(anim));  // a flinch is feedback, not an action the rules wait on
}

void DeathFinishedReleases() {
    game::FUnitAnim anim;
    anim.state = game::EUnitAnim::Death;
    anim.player.loop = false;
    CHECK(HoldsGate(anim));  // falling, not yet fallen
    anim.player.finished = true;
    CHECK(!HoldsGate(anim));  // the death clip ended: released
}

void LoopingClipNeverHoldsGate() {
    game::FUnitAnim attack;
    attack.state = game::EUnitAnim::Attack;
    attack.player.loop = true;
    CHECK(!HoldsGate(attack));  // game data must never be able to deadlock the gate

    game::FUnitAnim death;
    death.state = game::EUnitAnim::Death;
    death.player.loop = true;
    CHECK(!HoldsGate(death));
}

void StaleGateErasedOnExit() {
    FBattleFixture fixture(UniquePath("stale-gate"));
    auto& registry = fixture.registry;
    CHECK(!registry.ctx().contains<FPresentationGate>());  // OnEnter does not create the gate...
    CHECK(BusyAfterAnimationFrame(registry) == 0);
    CHECK(registry.ctx().contains<FPresentationGate>());  // ...the animation system does
    registry.ctx().get<FPresentationGate>().busy = 3;
    CHECK(registry.ctx().contains<FPresentationWatch>() && registry.ctx().contains<game::FBattleFlowState>());

    fixture.battle.OnExit(registry);
    fixture.entered = false;
    CHECK(!registry.ctx().contains<FPresentationGate>());
    CHECK(!registry.ctx().contains<FPresentationWatch>());
    CHECK(!registry.ctx().contains<game::FBattleFlowState>());

    fixture.battle.OnEnter(registry);  // a fresh battle starts from a zeroed watch and flow state
    fixture.entered = true;
    const auto& watch = registry.ctx().get<FPresentationWatch>();
    const auto& flow = registry.ctx().get<game::FBattleFlowState>();
    CHECK(watch.busySeconds == 0.f && !watch.timedOut);
    CHECK(!flow.handoffDone && !flow.roomClearedOnEnter && flow.lastLoggedPhase == EBattlePhase::Setup);
}

// ---------------------------------------------------------------------------------------------
// FEndTurn deferral (TurnSystem)
// ---------------------------------------------------------------------------------------------

void EndTurnDeferredWhileBusy() {
    FTurnFixture fixture;
    fixture.start();
    const FTurnToken token = fixture.token();
    fixture.SetGateBusy(1);
    fixture.requests().push_back(game::FEndTurn{token});

    fixture.turns.update(fixture.registry, 0.f);

    CHECK(fixture.requests().size() == 1);  // the request stays queued...
    CHECK(std::holds_alternative<game::FEndTurn>(fixture.requests().front()));
    CHECK(std::get<game::FEndTurn>(fixture.requests().front()).token == token);
    CHECK(fixture.snapshot().currentTurn == token);  // ...and the turn did not advance
    CHECK(!fixture.registry.ctx().get<game::FTurnRuntime>().lastError.has_value());
}

void StartStopProcessImmediately() {
    FTurnFixture fixture;
    fixture.SetGateBusy(1);
    fixture.requests().push_back(game::FStartTurnSession{7u});
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(fixture.snapshot().state == ETurnState::Running);  // Start is not deferred
    CHECK(fixture.snapshot().currentTurn.has_value());

    fixture.requests().push_back(game::FStopTurnSession{});
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(fixture.snapshot().state == ETurnState::Idle);  // Stop is not deferred
    CHECK(!fixture.snapshot().currentTurn.has_value());
}

void StopDisposesDeferredEndTurn() {
    FTurnFixture fixture;
    fixture.start();
    const FTurnToken token = fixture.token();
    fixture.SetGateBusy(1);
    fixture.requests().push_back(game::FEndTurn{token});
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(fixture.requests().size() == 1);

    fixture.requests().push_back(game::FStopTurnSession{});
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(fixture.snapshot().state == ETurnState::Idle);
    CHECK(fixture.requests().empty());  // stop() cleared the deque with the deferred request inside
}

void DuplicateEndTurnIsDropped() {
    FTurnFixture fixture;
    fixture.start();
    const FTurnToken token = fixture.token();
    fixture.SetGateBusy(1);
    fixture.requests().push_back(game::FEndTurn{token});
    fixture.requests().push_back(game::FEndTurn{token});  // the HUD was clicked twice

    fixture.turns.update(fixture.registry, 0.f);
    CHECK(fixture.requests().size() == 1);
    CHECK(std::get<game::FEndTurn>(fixture.requests().front()).token == token);

    FTurnToken other = token;
    ++other.turnSerial;  // a different turn is a different request, not a duplicate
    fixture.requests().push_back(game::FEndTurn{other});
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(fixture.requests().size() == 2);
}

void TurnAdvancesAfterSettle() {
    FTurnFixture fixture;
    fixture.start();
    const FTurnToken token = fixture.token();
    fixture.SetGateBusy(1);
    fixture.requests().push_back(game::FEndTurn{token});
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(fixture.snapshot().currentTurn == token);

    fixture.SetGateBusy(0);  // the presentation settled
    fixture.turns.update(fixture.registry, 0.f);

    CHECK(fixture.requests().empty());
    const auto next = fixture.snapshot();
    CHECK(next.currentTurn.has_value());
    CHECK(next.currentTurn->actor == fixture.slow);                 // fast acted, slow is up
    CHECK(next.currentTurn->turnSerial == token.turnSerial + 1);
    const auto& ended = fixture.endedEvents();
    CHECK(ended.size() == 1);
    CHECK(ended.front().token == token && ended.front().reason == game::ETurnEndReason::Completed);
}

void RemovedAdvanceStillGuardsActions() {
    FTurnFixture fixture;
    fixture.start();
    const FTurnToken first = fixture.token();
    CHECK(first.actor == fixture.fast);

    // The active unit leaves the roster (the death path); synchronize() advances past it even while busy —
    // the documented residual hole (WP-06 closes the input side). The FEndTurn deferral must still hold.
    fixture.registry.remove<game::FTurnParticipant>(fixture.fast);
    fixture.SetGateBusy(1);
    fixture.requests().push_back(game::FEndTurn{first});
    fixture.turns.update(fixture.registry, 0.f);

    const auto afterRemoval = fixture.snapshot();
    CHECK(afterRemoval.currentTurn.has_value());
    CHECK(afterRemoval.currentTurn->actor == fixture.slow);
    CHECK(afterRemoval.currentTurn->turnSerial == first.turnSerial + 1);
    CHECK(CountEndTurnRequests(fixture.requests()) == 1);  // still guarded

    fixture.requests().push_back(game::FEndTurn{*afterRemoval.currentTurn});
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(CountEndTurnRequests(fixture.requests()) == 2);  // the new actor's end-turn waits too

    fixture.SetGateBusy(0);
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(fixture.requests().empty());
    CHECK(fixture.registry.ctx().get<game::FTurnRuntime>().lastError == game::ETurnError::StaleTurn);
    const auto final = fixture.snapshot();
    CHECK(final.currentTurn.has_value());
    CHECK(final.currentTurn->actor == fixture.slow);
    CHECK(final.currentTurn->turnSerial == afterRemoval.currentTurn->turnSerial + 1);
}

void AtMostOneCompletedTurnEndPerUpdate() {
    FTurnFixture fixture;
    fixture.start();
    const FTurnToken current = fixture.token();
    FTurnToken stale = current;
    ++stale.turnSerial;
    FTurnToken staler = stale;
    staler.turnSerial += 4;
    fixture.requests().push_back(game::FEndTurn{stale});
    fixture.requests().push_back(game::FEndTurn{staler});
    fixture.requests().push_back(game::FEndTurn{current});

    fixture.turns.update(fixture.registry, 0.f);

    // The premise WP-04's FTurnEnded consumers rely on: one update can produce at most one Completed turn end.
    const auto& ended = fixture.endedEvents();
    const auto completed = std::ranges::count_if(ended, [](const game::FTurnEnded& event) {
        return event.reason == game::ETurnEndReason::Completed;
    });
    CHECK(completed == 1);
    CHECK(ended.front().token == current);
    CHECK(fixture.registry.ctx().get<game::FTurnRuntime>().lastError == game::ETurnError::StaleTurn);
    CHECK(fixture.snapshot().currentTurn->turnSerial == current.turnSerial + 1);
}

// ---------------------------------------------------------------------------------------------
// Derived view
// ---------------------------------------------------------------------------------------------

void DerivedPhasePrecedence() {
    FTurnToken token;
    token.sessionId = 1;
    token.roundNumber = 1;
    token.turnSerial = 1;
    token.actor = static_cast<entt::entity>(1);

    game::FBattlePhaseInputs inputs;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Setup);  // ⑥ fallback: session not started

    inputs.currentTurn = token;
    inputs.currentTurnIsParty = true;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Input);  // ⑤ party turn
    inputs.autoPlay = true;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::AiThink);  // ⑤ auto-play plays both sides
    inputs.autoPlay = false;
    inputs.currentTurnIsParty = false;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::AiThink);  // ⑤ enemy turn

    inputs.settled = false;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Settling);  // ④ beats ⑤
    inputs.pendingEmpty = false;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Resolving);  // ③ beats ④ and ⑤

    inputs.resolved = true;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::OutcomeDefeat);  // ② resolved loss beats ③④⑤
    inputs.victory = true;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::OutcomeVictory);
    inputs.resolved = false;
    inputs.victory = false;
    inputs.roomClearedOnEnter = true;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::OutcomeVictory);  // ② the saved continue is a win

    inputs.handoffDone = true;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Closed);  // ① beats everything, even Outcome
}

void DerivedPhaseInputsArePure() {
    static_assert(std::is_aggregate_v<game::FBattlePhaseInputs>);
    static_assert(!DerivableFromRegistry<entt::registry>);  // no registry overload exists

    game::FBattlePhaseInputs inputs;
    inputs.pendingEmpty = false;
    const auto previous = inputs.pendingEmpty;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Resolving);
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Resolving);  // same inputs, same view
    CHECK(inputs.pendingEmpty == previous);                             // and nothing was mutated

    inputs.pendingEmpty = true;
    inputs.settled = false;
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Settling);
    CHECK(game::DeriveBattlePhase(inputs) == EBattlePhase::Settling);
}

void PhaseLogOneLinePerChange() {
    entt::registry registry;
    auto& flow = registry.ctx().emplace<game::FBattleFlowState>();
    CHECK(flow.lastLoggedPhase == EBattlePhase::Setup);
    CHECK(!game::RecordBattlePhaseChange(registry, EBattlePhase::Setup));  // the initial value: no change
    CHECK(game::RecordBattlePhaseChange(registry, EBattlePhase::Input));    // one line...
    CHECK(flow.lastLoggedPhase == EBattlePhase::Input);
    CHECK(!game::RecordBattlePhaseChange(registry, EBattlePhase::Input));  // ...not one per frame
    CHECK(game::RecordBattlePhaseChange(registry, EBattlePhase::Resolving));
    CHECK(game::RecordBattlePhaseChange(registry, EBattlePhase::OutcomeVictory));
    CHECK(game::RecordBattlePhaseChange(registry, EBattlePhase::Closed));
    CHECK(flow.lastLoggedPhase == EBattlePhase::Closed);
    CHECK(!game::RecordBattlePhaseChange(registry, EBattlePhase::Closed));

    // The system drives the same record from live registry inputs: a party turn derives Input once.
    entt::registry battle;
    game::ecs::TurnSystem turns;
    turns.onRegister(battle);
    battle.ctx().emplace<game::FEventBus>();
    battle.ctx().emplace<FPresentationGate>();
    const entt::entity hero = battle.create();
    battle.emplace<game::FTurnParticipant>(hero);
    battle.emplace<game::FSpeed>(hero, game::FSpeed{10});
    battle.emplace<game::FPartyMember>(hero);
    battle.ctx().get<game::FTurnRuntime>().requests.push_back(game::FStartTurnSession{3u});
    turns.update(battle, 0.f);
    auto& battleFlow = battle.ctx().emplace<game::FBattleFlowState>();
    game::ecs::FBattleFlowSystem flowSystem;
    flowSystem.update(battle, 0.f);
    CHECK(battleFlow.lastLoggedPhase == EBattlePhase::Input);
    flowSystem.update(battle, 0.f);
    CHECK(battleFlow.lastLoggedPhase == EBattlePhase::Input);  // unchanged: the record does not move
}

void NoStoredPhaseState() {
    static_assert(HasPhaseField<game::FDungeonRun>);           // the probe itself works (the run owns its phase)...
    static_assert(!HasPhaseField<game::FBattleFlowState>);     // ...but the flow state stores no current phase
    static_assert(!HasRoundNumberField<game::FBattleFlowState>);  // the round stays TurnController's
    static_assert(requires(game::FBattleFlowState value) {
        value.roomClearedOnEnter;
        value.handoffDone;
        value.lastLoggedPhase;  // the log-dedup record, the only phase-shaped member
    });

    for (const char* path : {"src/gameplay/combat/FBattleFlow.h", "src/gameplay/combat/FBattleFlow.cpp",
                             "src/ecs/systems/FBattleFlowSystem.h"}) {
        const std::string text = ReadText(path);
        CHECK(text.find("EDungeonPhase") == std::string::npos);  // no run-phase mirror
        CHECK(text.find("roundNumber") == std::string::npos);    // no round mirror
        CHECK(text.find("FTurnEventLog") == std::string::npos);  // and no turn-order log
    }
}

// ---------------------------------------------------------------------------------------------
// Handoff (FEmberBattleState::Update)
// ---------------------------------------------------------------------------------------------

void OutcomeWaitsSettling() {
    FBattleFixture fixture(UniquePath("handoff-wait"));
    auto& registry = fixture.registry;
    fixture.SetGateBusy(1);
    auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    outcome.resolved = true;
    outcome.victory = true;

    CHECK((fixture.battle.Update(registry, 0.f) == FStateTransition{}));  // no modal while the show runs
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == EDungeonPhase::Battle);
    CHECK(!registry.ctx().get<game::FBattleFlowState>().handoffDone);

    fixture.SetGateBusy(0);
    CHECK((fixture.battle.Update(registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
    CHECK(registry.ctx().get<game::FBattleFlowState>().handoffDone);
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == EDungeonPhase::Reward);
}

void StalemateAlsoGated() {
    FBattleFixture fixture(UniquePath("handoff-stalemate"));
    auto& registry = fixture.registry;
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    // The stalemate branch's observable writes: a loss outcome plus a stop request.
    auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    outcome.resolved = true;
    outcome.victory = false;
    runtime.requests.push_back(game::FStopTurnSession{});
    fixture.SetGateBusy(1);

    fixture.turns.update(registry, 0.f);
    CHECK(runtime.controller.snapshot().state == ETurnState::Idle);  // Stop is immediate, not deferred
    CHECK((fixture.battle.Update(registry, 0.f) == FStateTransition{}));  // but the handoff still waits

    fixture.SetGateBusy(0);
    CHECK((fixture.battle.Update(registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalRunEnd)}));
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == EDungeonPhase::Defeat);
}

void VictoryHandoffOnce() {
    FBattleFixture fixture(UniquePath("handoff-victory"));
    auto& registry = fixture.registry;
    auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    outcome.resolved = true;
    outcome.victory = true;

    CHECK((fixture.battle.Update(registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    const int materials = run.materials;
    const std::size_t runes = run.runes.size();

    CHECK((fixture.battle.Update(registry, 0.f) == FStateTransition{}));  // handoffDone: never twice
    CHECK(run.materials == materials && run.runes.size() == runes);
    CHECK(run.phase == EDungeonPhase::Reward);
}

void DefeatHandoffAfterSettle() {
    FBattleFixture fixture(UniquePath("handoff-defeat"));
    auto& registry = fixture.registry;
    fixture.SetGateBusy(1);
    auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    outcome.resolved = true;
    outcome.victory = false;

    CHECK((fixture.battle.Update(registry, 0.f) == FStateTransition{}));  // no runEnd while the party falls
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == EDungeonPhase::Battle);
    CHECK(registry.ctx().get<game::FDungeonSave>().exists);

    fixture.SetGateBusy(0);
    CHECK((fixture.battle.Update(registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalRunEnd)}));
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == EDungeonPhase::Defeat);
    CHECK(!registry.ctx().get<game::FDungeonSave>().exists);
    CHECK(!std::filesystem::exists(fixture.runSave));  // a lost run cannot be continued
    CHECK(registry.ctx().get<game::FBattleFlowState>().handoffDone);
}

void FreezeNoTransitionWhileBusy() {
    FBattleFixture fixture(UniquePath("handoff-freeze"));
    fixture.SetGateBusy(3);
    auto& outcome = fixture.registry.ctx().get<game::FBattleOutcome>();
    outcome.resolved = true;
    outcome.victory = true;

    for (int frame = 0; frame < 5; ++frame) {
        CHECK((fixture.battle.Update(fixture.registry, 1.f / 60.f) == FStateTransition{}));
    }
    CHECK(!fixture.registry.ctx().get<FPresentationWatch>().timedOut);  // no clock runs while frozen
    CHECK(fixture.registry.ctx().get<game::FDungeonRun>().phase == EDungeonPhase::Battle);

    fixture.SetGateBusy(0);
    CHECK((fixture.battle.Update(fixture.registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
}

void ContinuedPushesModalOnce() {
    FContinuedBattle fixture(UniquePath("continued-once"));
    auto& registry = fixture.registry;
    CHECK(!registry.ctx().get<game::FBattleFlowState>().handoffDone);

    CHECK((fixture.battle.Update(registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
    CHECK(registry.ctx().get<game::FBattleFlowState>().handoffDone);
    CHECK((fixture.battle.Update(registry, 0.f) == FStateTransition{}));  // the once-guard: not re-pushed
}

void ContinuedRoomDoesNotResolveRun() {
    FContinuedBattle fixture(UniquePath("continued-no-resolve"));
    auto& registry = fixture.registry;

    CHECK((fixture.battle.Update(registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.phase == EDungeonPhase::Reward);  // the run transition did not run again
    CHECK(run.materials == fixture.materials);
    CHECK(run.runes.size() == fixture.runes);
    CHECK(run.party.front().experience == fixture.experience);
}

void ContinuedNoExtraSave() {
    FContinuedBattle fixture(UniquePath("continued-no-save"));

    CHECK((fixture.battle.Update(fixture.registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
    CHECK(ReadBytes(fixture.runSave) == fixture.saveBefore);  // the handoff wrote nothing
}

void ContinuedSkipsTurnSession() {
    FContinuedBattle fixture(UniquePath("continued-no-session"));
    const auto& runtime = fixture.registry.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.snapshot().state == ETurnState::Idle);  // no turn session for a cleared room
    CHECK(runtime.requests.empty());
}

void ContinuedNoRoomClearHook() {
    FContinuedBattle fixture(UniquePath("continued-no-hook"));
    int roomClear = 0;
    game::RegisterHookListener(fixture.registry, game::gameplay::EHookEvent::OnRoomClear,
        [&roomClear](entt::registry&, const game::FHookContext&) { ++roomClear; });

    CHECK((fixture.battle.Update(fixture.registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
    CHECK(roomClear == 0);

    game::ecs::CombatOutcomeSystem outcomes;
    outcomes.update(fixture.registry, 0.f);  // the resolved outcome short-circuits: nothing refires
    CHECK(roomClear == 0);
}

// ---------------------------------------------------------------------------------------------
// 05b: TurnFlow driver guard
// ---------------------------------------------------------------------------------------------

/// A minimal battle context for the TurnFlow guard: turn runtime, combat/outcome/content contexts, a queue,
/// a log and an event bus. Two participants (speed 20 vs 10, so the fast one acts first) with AP pools and a
/// burn-shaped status. No battle state — the guard is a TurnFlow property; the win/lose handoff is covered
/// by the battle fixtures above.
struct FTurnFlowFixture {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    entt::entity fast{entt::null};
    entt::entity slow{entt::null};

    FTurnFlowFixture() {
        turns.onRegister(registry);
        registry.ctx().emplace<game::FEventBus>();
        registry.ctx().emplace<FPresentationGate>();
        registry.ctx().emplace<game::FCombatRuntime>();
        registry.ctx().emplace<game::FBattleOutcome>();
        registry.ctx().emplace<game::gameplay::FContentRegistry>();
        registry.ctx().emplace<game::FActionQueue>();
        registry.ctx().emplace<game::FCombatLog>();
        fast = MakeParticipant(20);
        slow = MakeParticipant(10);
    }

    entt::entity MakeParticipant(std::int32_t speed) {
        const entt::entity unit = registry.create();
        registry.emplace<game::FTurnParticipant>(unit);
        registry.emplace<game::FSpeed>(unit, game::FSpeed{speed});
        registry.emplace<game::FActionPoints>(unit, game::FActionPoints{0, 1, 0, 2});
        game::FActiveStatuses statuses;
        statuses.effects.push_back(game::FStatusEffect{"status.burn", 3, 5});
        registry.emplace<game::FActiveStatuses>(unit, std::move(statuses));
        return unit;
    }

    [[nodiscard]] game::FTurnSnapshot snapshot() {
        return registry.ctx().get<game::FTurnRuntime>().controller.snapshot();
    }

    [[nodiscard]] FTurnToken token() {
        const auto current = snapshot().currentTurn;
        CHECK(current.has_value());
        return *current;
    }

    [[nodiscard]] std::deque<FTurnRequest>& requests() {
        return registry.ctx().get<game::FTurnRuntime>().requests;
    }

    void SetGateBusy(int busy) { registry.ctx().get<FPresentationGate>().busy = busy; }

    void start(std::uint32_t seed = 7u) {
        requests().push_back(game::FStartTurnSession{seed});
        turns.update(registry, 0.f);
        CHECK(snapshot().state == ETurnState::Running);
        CHECK(token().actor == fast);
    }
};

void TurnFlowHoldsDuringSettling() {
    FTurnFlowFixture fixture;
    fixture.start();
    auto* combat = fixture.registry.ctx().find<game::FCombatRuntime>();
    auto& points = fixture.registry.get<game::FActionPoints>(fixture.fast);
    auto& statuses = fixture.registry.get<game::FActiveStatuses>(fixture.fast);
    combat->thinkDelay = 0.3f;
    const auto serialBefore = combat->scheduledTurnSerial;
    fixture.SetGateBusy(1);

    fixture.flow.update(fixture.registry, 0.1f);

    CHECK(fixture.requests().empty());                        // nothing queued while the show runs
    CHECK(fixture.registry.ctx().get<game::FActionQueue>().pending.empty());
    CHECK(points.move == 0 && points.skill == 0);             // no AP refill
    CHECK(statuses.effects.size() == 1 && statuses.effects.front().turnsRemaining == 3);  // no status tick
    CHECK(combat->thinkDelay == 0.3f);                        // the countdown did not move
    CHECK(combat->scheduledTurnSerial == serialBefore);       // turnStarted still pending
}

void BeginTurnDeferredWhileSettling() {
    FBattleFixture fixture(UniquePath("flow-begin"));
    auto& registry = fixture.registry;
    fixture.turns.update(registry, 0.f);  // process the FStartTurnSession OnEnter queued
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.snapshot().state == ETurnState::Running);
    const entt::entity actor = runtime.controller.snapshot().currentTurn->actor;

    auto& points = registry.get<game::FActionPoints>(actor);
    points.move = 0;
    points.skill = 0;
    auto& statuses = registry.emplace_or_replace<game::FActiveStatuses>(actor);
    statuses.effects.push_back(game::FStatusEffect{"status.burn", 3, 5});
    const int healthBefore = registry.get<game::FHealth>(actor).current;

    game::ecs::TurnFlowSystem flow;
    fixture.SetGateBusy(1);
    flow.update(registry, 1.f);
    CHECK(points.move == 0 && points.skill == 0);                                        // held: no refill
    CHECK(statuses.effects.size() == 1 && statuses.effects.front().turnsRemaining == 3); // held: no tick
    CHECK(registry.get<game::FHealth>(actor).current == healthBefore);                   // held: no damage

    fixture.SetGateBusy(0);
    flow.update(registry, 1.f);
    CHECK(points.move == points.moveMax && points.skill == points.skillMax);  // first settled frame: refill
    CHECK(statuses.effects.size() == 1 && statuses.effects.front().turnsRemaining == 2);  // tick, exactly once
    CHECK(registry.get<game::FHealth>(actor).current == healthBefore - 5);   // burn dot = status magnitude

    flow.update(registry, 1.f);
    CHECK(points.move == points.moveMax && points.skill == points.skillMax);  // a later settled frame...
    CHECK(statuses.effects.size() == 1 && statuses.effects.front().turnsRemaining == 2);  // ...does not repeat
    CHECK(registry.get<game::FHealth>(actor).current == healthBefore - 5);
}

void FlowResumesAfterSettle() {
    FTurnFlowFixture fixture;
    fixture.start();
    auto* combat = fixture.registry.ctx().find<game::FCombatRuntime>();
    combat->thinkDelay = 0.2f;
    fixture.SetGateBusy(1);

    fixture.flow.update(fixture.registry, 0.5f);
    CHECK(fixture.requests().empty());
    CHECK(combat->thinkDelay == 0.2f);  // held together with everything else

    fixture.SetGateBusy(0);
    fixture.flow.update(fixture.registry, 0.5f);
    // The held turn is picked up on the first settled frame: start consumed, AP refilled, empty loadout ends.
    CHECK(combat->scheduledTurnSerial == fixture.token().turnSerial);
    CHECK(fixture.registry.get<game::FActionPoints>(fixture.fast).move == 1);
    CHECK(fixture.registry.get<game::FActionPoints>(fixture.fast).skill == 2);
    CHECK(CountEndTurnRequests(fixture.requests()) == 1);
}

void GuardNoOpsWithoutGate() {
    FTurnFlowFixture fixture;
    fixture.registry.ctx().erase<FPresentationGate>();  // headless fixtures and autoplay carry no gate
    CHECK(game::IsPresentationSettled(fixture.registry));
    fixture.start();
    auto* combat = fixture.registry.ctx().find<game::FCombatRuntime>();
    combat->thinkDelay = 0.f;

    fixture.flow.update(fixture.registry, 0.5f);

    const auto& points = fixture.registry.get<game::FActionPoints>(fixture.fast);
    CHECK(points.move == 1 && points.skill == 2);                      // refilled like before WP-05
    CHECK(combat->scheduledTurnSerial == fixture.token().turnSerial);  // turn start consumed
    CHECK(CountEndTurnRequests(fixture.requests()) == 1);              // the empty loadout still ends the turn
    CHECK(fixture.registry.ctx().get<game::FActionQueue>().pending.empty());
}

void StalemateStillResolves() {
    FBattleFixture fixture(UniquePath("flow-stalemate"));
    auto& registry = fixture.registry;
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    fixture.turns.update(registry, 0.f);  // start the session OnEnter queued
    CHECK(runtime.controller.snapshot().state == ETurnState::Running);

    // End every turn as it comes (TurnSystem only — no flow system in the loop) until the round limit is
    // past. Controller bookkeeping only; nothing else runs while the units stand still.
    const std::size_t unitCount = registry.view<game::FTurnParticipant>().size();
    CHECK(unitCount >= 2);
    CHECK(runtime.controller.snapshot().currentTurn.has_value());
    std::size_t guard = 0;
    while (runtime.controller.snapshot().roundNumber <= game::kMaxBattleRounds &&
           guard < (game::kMaxBattleRounds + 2) * unitCount) {
        runtime.requests.push_back(game::FEndTurn{*runtime.controller.snapshot().currentTurn});
        fixture.turns.update(registry, 0.f);
        ++guard;
    }
    CHECK(runtime.controller.snapshot().roundNumber > game::kMaxBattleRounds);

    game::ecs::TurnFlowSystem flow;
    game::InitializeEventBus(registry);  // the fixture boot recipe has no engine bus; TurnSystem flushes into it
    registry.ctx().get<game::FEventBus>().beginFrame();
    fixture.SetGateBusy(1);
    flow.update(registry, 0.f);
    const auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    CHECK(!outcome.resolved);       // the stalemate resolution itself waits for the show
    CHECK(runtime.requests.empty());

    fixture.SetGateBusy(0);
    flow.update(registry, 0.f);
    CHECK(outcome.resolved && !outcome.victory);  // defeat by round limit
    const std::size_t stops = std::ranges::count_if(runtime.requests, [](const FTurnRequest& request) {
        return std::holds_alternative<game::FStopTurnSession>(request);
    });
    CHECK(stops == 1);

    // The battle handoff turns the stalemate into the run-end push after the settle; a defeat is not saved.
    CHECK((fixture.battle.Update(registry, 0.f) ==
           FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalRunEnd)}));
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == EDungeonPhase::Defeat);
    CHECK(!std::filesystem::exists(fixture.runSave));
}

void TurnEndHookNotDroppedByGuard() {
    FTurnFlowFixture fixture;
    fixture.start();
    int turnEnds = 0;
    game::RegisterHookListener(fixture.registry, game::gameplay::EHookEvent::OnTurnEnd,
        [&turnEnds](entt::registry&, const game::FHookContext&) { ++turnEnds; });

    auto& bus = fixture.registry.ctx().get<game::FEventBus>();
    fixture.SetGateBusy(1);
    fixture.requests().push_back(game::FEndTurn{fixture.token()});  // queued right when the show starts

    // Frame 1 (busy): TurnSystem defers the FEndTurn — no Completed event — and the guard keeps TurnFlow
    // away from the consumption block.
    bus.beginFrame();
    fixture.turns.update(fixture.registry, 0.f);
    fixture.flow.update(fixture.registry, 0.f);
    CHECK(fixture.requests().size() == 1);  // deferred, not dropped
    CHECK(bus.frameEvents<game::FTurnEnded>().empty());
    CHECK(turnEnds == 0);

    // Frame 2 (settled): the deferred turn completes and the same frame consumes it — OnTurnEnd fires once.
    fixture.SetGateBusy(0);
    bus.beginFrame();
    fixture.turns.update(fixture.registry, 0.f);
    CHECK(bus.frameEvents<game::FTurnEnded>().size() == 1);  // Completed: only produced while settled
    fixture.flow.update(fixture.registry, 0.f);
    CHECK(turnEnds == 1);

    // A second TurnFlow update in the same frame must not fire it again (the helper dedupes).
    fixture.flow.update(fixture.registry, 0.f);
    CHECK(turnEnds == 1);
}

template<typename T>
concept HasFocusField = requires(T value) { value.focus; };
template<typename T>
concept HasMoveTargetingField = requires(T value) { value.moveTargeting; };
template<typename T>
concept HasSkillTargetingField = requires(T value) { value.skillTargeting; };
template<typename T>
concept HasInputModeField = requires(T value) { value.inputMode; };
template<typename E>
concept EnumHasInput = requires { E::Input; };
template<typename E>
concept EnumHasMoveTargeting = requires { E::MoveTargeting; };
template<typename E>
concept EnumHasSkillTargeting = requires { E::SkillTargeting; };
template<typename E>
concept EnumHasFocus = requires { E::Focus; };

void InputSubModesAbsent() {
    // Positive controls: the probes do find a real phase and a field of the probed shape.
    static_assert(EnumHasInput<EBattlePhase>);
    struct FFieldProbe {
        int focus;
    };
    static_assert(HasFocusField<FFieldProbe>);

    // The flow state and its inputs carry no input sub-mode; EBattlePhase has no input sub-phases. The HUD
    // (WP-12) is the one source of input modes (W3 review §6.7).
    static_assert(!EnumHasMoveTargeting<EBattlePhase>);
    static_assert(!EnumHasSkillTargeting<EBattlePhase>);
    static_assert(!EnumHasFocus<EBattlePhase>);
    static_assert(!HasFocusField<game::FBattleFlowState>);
    static_assert(!HasMoveTargetingField<game::FBattleFlowState>);
    static_assert(!HasSkillTargetingField<game::FBattleFlowState>);
    static_assert(!HasInputModeField<game::FBattleFlowState>);
    static_assert(!HasFocusField<game::FBattlePhaseInputs>);
    static_assert(!HasMoveTargetingField<game::FBattlePhaseInputs>);
    static_assert(!HasSkillTargetingField<game::FBattlePhaseInputs>);
    static_assert(!HasInputModeField<game::FBattlePhaseInputs>);

    CHECK(std::string(game::BattlePhaseName(EBattlePhase::Input)) == "Input");  // the one input phase stays a name
}

// ---------------------------------------------------------------------------------------------
// Layering / policy
// ---------------------------------------------------------------------------------------------

void NoPresentationInclude() {
    for (const char* path : {"src/gameplay/combat/FBattleFlow.h", "src/gameplay/combat/FBattleFlow.cpp",
                             "src/ecs/systems/FBattleFlowSystem.h"}) {
        const std::string text = ReadText(path);
        CHECK(text.find("presentation/") == std::string::npos);
    }
}

void NoEcsSystemsIncludeInFlow() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("src/gameplay")) {
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension();
        if (extension == ".h" || extension == ".cpp") files.push_back(entry.path());
    }
    CHECK(files.size() > 10);  // sanity: the scan found the gameplay tree
    for (const auto& file : files) {
        CHECK(ReadText(file).find("ecs/systems/") == std::string::npos);  // gameplay -> ecs/systems is banned
    }
}

void FlowNotEnabledUnderModal() {
    const FEmberBattleState shape;  // only its system lists; never entered
    const auto renderNames = [&] {
        std::vector<std::string> names;
        for (const auto name : shape.RequiredRenderSystems()) names.emplace_back(name);
        return names;
    }();
    CHECK(std::ranges::find(renderNames, std::string("BattleFlowSystem")) == renderNames.end());
    const auto requiredNames = [&] {
        std::vector<std::string> names;
        for (const auto name : shape.RequiredSystems()) names.emplace_back(name);
        return names;
    }();
    CHECK(std::ranges::find(requiredNames, std::string("BattleFlowSystem")) != requiredNames.end());

    FBattleFixture fixture(UniquePath("flow-policy"), true, false);
    game::SystemManager systems;
    for (const auto& name : requiredNames) {
        systems.addSystem(std::make_unique<FNamedSystem>(name));
    }
    for (const auto& name : renderNames) {
        if (std::ranges::find(requiredNames, name) == requiredNames.end()) {
            systems.addSystem(std::make_unique<FNamedSystem>(name));
        }
    }

    game::GameStateMachine machine;
    machine.pushState("ember.battle", std::make_unique<FEmberBattleState>(), fixture.registry);
    machine.pushState(std::string(game::kRunModalReward),
                      std::make_unique<FEmberModalHostState>(std::string(game::kRunModalReward), EHostMode::RunPhase),
                      fixture.registry);

    const auto missing = game::ApplyStackSystemPolicy(machine, systems);
    CHECK(missing.empty());
    CHECK(!systems.isEnabled("BattleFlowSystem"));       // under a modal the flow update stops: the clock freezes
    CHECK(!systems.isRenderEnabled("BattleFlowSystem")); // ...and the flow view is not a render system

    (void)machine.popState(fixture.registry);
    const auto resumed = game::ApplyStackSystemPolicy(machine, systems);
    CHECK(resumed.empty());
    CHECK(systems.isEnabled("BattleFlowSystem"));  // the battle is the top again: the flow resumes
}

} // namespace

int main() {
    // Save safety (the effect_hooks contract): ctest exports no GAME01P_SAVE_DIR for feature tests, so
    // outside build-test.ps1 a plain run would let ResolveRoomVictory save to — and a defeat delete — the
    // user's real save. Commit an override before any dungeon service can touch disk.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "battle_flow_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("battle flow", {
        {"HeadlessSettlesImmediately", HeadlessSettlesImmediately},
        {"GateBusyBlocksSettle", GateBusyBlocksSettle},
        {"GateTimeoutForcesSettle", GateTimeoutForcesSettle},
        {"WatchFrozenUnderModal", WatchFrozenUnderModal},
        {"TimeoutCountsConsecutiveBusy", TimeoutCountsConsecutiveBusy},
        {"GateCountsTimelineEffectsDeaths", GateCountsTimelineEffectsDeaths},
        {"WalkHoldsGate", WalkHoldsGate},
        {"AttackSwingHoldsGate", AttackSwingHoldsGate},
        {"HitFlinchDoesNotHold", HitFlinchDoesNotHold},
        {"DeathFinishedReleases", DeathFinishedReleases},
        {"LoopingClipNeverHoldsGate", LoopingClipNeverHoldsGate},
        {"StaleGateErasedOnExit", StaleGateErasedOnExit},
        {"EndTurnDeferredWhileBusy", EndTurnDeferredWhileBusy},
        {"StartStopProcessImmediately", StartStopProcessImmediately},
        {"StopDisposesDeferredEndTurn", StopDisposesDeferredEndTurn},
        {"DuplicateEndTurnIsDropped", DuplicateEndTurnIsDropped},
        {"TurnAdvancesAfterSettle", TurnAdvancesAfterSettle},
        {"RemovedAdvanceStillGuardsActions", RemovedAdvanceStillGuardsActions},
        {"AtMostOneCompletedTurnEndPerUpdate", AtMostOneCompletedTurnEndPerUpdate},
        {"DerivedPhasePrecedence", DerivedPhasePrecedence},
        {"DerivedPhaseInputsArePure", DerivedPhaseInputsArePure},
        {"PhaseLogOneLinePerChange", PhaseLogOneLinePerChange},
        {"NoStoredPhaseState", NoStoredPhaseState},
        {"OutcomeWaitsSettling", OutcomeWaitsSettling},
        {"StalemateAlsoGated", StalemateAlsoGated},
        {"VictoryHandoffOnce", VictoryHandoffOnce},
        {"DefeatHandoffAfterSettle", DefeatHandoffAfterSettle},
        {"FreezeNoTransitionWhileBusy", FreezeNoTransitionWhileBusy},
        {"ContinuedPushesModalOnce", ContinuedPushesModalOnce},
        {"ContinuedRoomDoesNotResolveRun", ContinuedRoomDoesNotResolveRun},
        {"ContinuedNoExtraSave", ContinuedNoExtraSave},
        {"ContinuedSkipsTurnSession", ContinuedSkipsTurnSession},
        {"ContinuedNoRoomClearHook", ContinuedNoRoomClearHook},
        {"TurnFlowHoldsDuringSettling", TurnFlowHoldsDuringSettling},
        {"BeginTurnDeferredWhileSettling", BeginTurnDeferredWhileSettling},
        {"FlowResumesAfterSettle", FlowResumesAfterSettle},
        {"GuardNoOpsWithoutGate", GuardNoOpsWithoutGate},
        {"StalemateStillResolves", StalemateStillResolves},
        {"TurnEndHookNotDroppedByGuard", TurnEndHookNotDroppedByGuard},
        {"InputSubModesAbsent", InputSubModesAbsent},
        {"NoPresentationInclude", NoPresentationInclude},
        {"NoEcsSystemsIncludeInFlow", NoEcsSystemsIncludeInFlow},
        {"FlowNotEnabledUnderModal", FlowNotEnabledUnderModal},
    });
}
