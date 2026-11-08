// WP-11 intent tests. The declared-intent pipeline is exercised three ways:
//   * the real dungeon battle (the AiTests boot recipe) with the full turn pipeline
//     (TurnSystem -> TurnFlowSystem -> ActionResolverSystem -> CombatOutcomeSystem), so commit order,
//     execution, whiffs and the settle run exactly as they run in the game;
//   * pure queries (PlanIntent / EvaluateIntent) that the WP-12 UI will also call per frame;
//   * the FAiLibrary document parser for the `intent_mode` key.
// The metrics are reporting numbers, not pass/fail: the tests pin the semantics (who counts as what),
// never a rate threshold.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/ai/FAiBrain.h"
#include "gameplay/ai/FAiLibrary.h"
#include "gameplay/ai/FAiNodeRegistry.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "states/FEmberBattleState.h"

namespace {

using game::EIntentMode;
using game::EIntentVerdict;
using game::FIntent;
using game::FIntentStats;
using game::FIntentStore;

// ---------------------------------------------------------------------------------------------
// A real battle with the full turn pipeline.
// ---------------------------------------------------------------------------------------------

/// The AiTests boot recipe (content + generated room + battle state) with the whole turn pipeline, so a
/// test can drive the systems the game runs. `arena()` clears the generated room; `place()` revives the
/// units the test wants and puts them back into the turn roster.
struct FIntentBattle {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    FEmberBattleState battle;
    std::filesystem::path checkpoint;
    std::vector<entt::entity> everyone;

    explicit FIntentBattle(std::uint32_t seed = 7, int depth = 1, int room = 0) {
        std::filesystem::create_directories("out");
        static int counter = 0;
        checkpoint = std::filesystem::path("out") /
            ("intent-checkpoint-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
             std::to_string(counter++) + ".json");
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        game::InitializeEventBus(registry); // the composition root's job; rounds are consumed off this bus
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        turns.onRegister(registry);
        game::StartDungeonRun(registry, seed, true);
        auto& run = registry.ctx().get<game::FDungeonRun>();
        run.depth = depth;
        run.room = room;
        battle.OnEnter(registry);
        for (const auto entity : registry.view<game::FHealth>()) {
            everyone.push_back(entity);
        }
    }

    ~FIntentBattle() {
        battle.OnExit(registry);
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(checkpoint.string() + ".tmp", error);
    }

    FIntentBattle(const FIntentBattle&) = delete;
    FIntentBattle& operator=(const FIntentBattle&) = delete;

    game::gameplay::FContentRegistry& content() {
        return registry.ctx().get<game::gameplay::FContentRegistry>();
    }

    game::FIntentStore* store() { return registry.ctx().find<FIntentStore>(); }

    FIntentStats stats() {
        const FIntentStats* found = game::FindIntentStats(registry);
        return found != nullptr ? *found : FIntentStats{};
    }

    game::FTurnSnapshot snapshot() {
        return registry.ctx().get<game::FTurnRuntime>().controller.snapshot();
    }

    game::FActionQueue& queue() { return registry.ctx().get<game::FActionQueue>(); }
    game::FCombatRuntime& combat() { return registry.ctx().get<game::FCombatRuntime>(); }
    game::FGridTerrain& terrain() { return registry.ctx().get<game::FGridTerrain>(); }
    game::FTurnRuntime& runtime() { return registry.ctx().get<game::FTurnRuntime>(); }

    /// Kill the whole generated roster and clear the terrain: the test places its own duel.
    void arena() {
        terrain().cells.fill(game::ETile::Floor);
        for (const auto entity : everyone) {
            (void)game::ApplyDamageToUnit(registry, entity, 100000);
        }
    }

    /// Revive + place a unit. The kill dropped its FTurnParticipant, so it is re-added here.
    entt::entity place(entt::entity unit, game::FGridPosition cell, int hpPercent = 100) {
        registry.remove<game::FDead>(unit);
        auto& health = registry.get<game::FHealth>(unit);
        health.current = std::max(1, health.max * hpPercent / 100);
        registry.get<game::FGridPosition>(unit) = cell;
        registry.emplace_or_replace<game::FTurnParticipant>(unit);
        refill(unit);
        return unit;
    }

    void refill(entt::entity unit) {
        auto& points = registry.get<game::FActionPoints>(unit);
        points.move = points.moveMax;
        points.skill = points.skillMax;
    }

    void skills(entt::entity unit, std::vector<std::string> ids) {
        auto& loadout = registry.get<game::FSkillLoadout>(unit);
        loadout.slots.clear();
        for (std::string& id : ids) {
            loadout.slots.push_back({std::move(id), {}});
        }
    }

    void speed(entt::entity unit, std::int32_t value) { registry.get<game::FSpeed>(unit).value = value; }

    entt::entity party(std::string_view id) {
        for (const auto entity : everyone) {
            if (registry.get<game::FUnitData>(entity).definition.id == id) {
                return entity;
            }
        }
        throw std::runtime_error("no unit " + std::string(id));
    }

    std::vector<entt::entity> enemies() {
        std::vector<entt::entity> result;
        for (const auto entity : everyone) {
            if (registry.all_of<game::FEnemy>(entity)) {
                result.push_back(entity);
            }
        }
        return result;
    }

    /// Starts an engine bus frame. The engine does this before any system runs (src/core/Engine.cpp), and
    /// a test that drives several frames must do the same: the round/turn events are frame-scoped, so
    /// without a frame start they pile up across frames and a consumer can read a stale one. A test that
    /// deliberately erased the bus (the intents-off control run) gets a no-op.
    void beginFrame() {
        if (auto* bus = registry.ctx().find<game::FEventBus>()) {
            bus->beginFrame();
        }
    }

    /// One engine frame start->flow: beginFrame, TurnSystem (queues round/turn events), TurnFlowSystem
    /// (consumes them: commits intents, runs the enemy turn). `actions` is left to the caller — tests
    /// observe the queue between the resolver and the next frame.
    void frame(float dt = 0.5f) {
        beginFrame();
        turns.update(registry, dt);
        flow.update(registry, dt);
    }

    /// The HUD's end-turn for the active actor; TurnSystem processes it right away (headless: settled).
    /// Own frame: the follow-up flowFrame() is that same frame's continuation.
    void endCurrentTurn() {
        const auto current = snapshot().currentTurn;
        CHECK(current.has_value());
        runtime().requests.push_back(game::FEndTurn{*current});
        beginFrame();
        turns.update(registry, 0.1f);
    }

    /// Ends turns until `actor` is the active one.
    void advanceTo(entt::entity actor) {
        for (int guard = 0; guard < 12; ++guard) {
            const auto current = snapshot().currentTurn;
            CHECK(current.has_value());
            if (current->actor == actor) {
                return;
            }
            endCurrentTurn();
        }
        CHECK(false);
    }

    /// Commits a round straight through the event bus — what TurnSystem would queue for FRoundStarted.
    /// Own synthetic frame: beginFrame keeps earlier frames' events out of the bucket.
    void queueRound(std::uint64_t sessionId, std::uint64_t roundNumber) {
        beginFrame();
        game::FTurnSnapshot roundSnapshot;
        roundSnapshot.state = game::ETurnState::Running;
        roundSnapshot.sessionId = sessionId;
        roundSnapshot.roundNumber = roundNumber;
        registry.ctx().get<game::FEventBus>().queueFrame<game::FRoundStarted>(game::FRoundStarted{roundSnapshot});
        game::CommitEnemyIntents(registry, content());
    }

    void drain() { actions.update(registry, 0.0f); }

    /// The same frame's flow step, after frame() or endCurrentTurn(): no beginFrame — that would clear
    /// the events TurnSystem queued in this very frame.
    void flowFrame(float dt = 0.5f) { flow.update(registry, dt); }
};

[[nodiscard]] std::string IntentDump(FIntentBattle& fixture) {
    std::string dump;
    const auto* store = fixture.store();
    if (store == nullptr) {
        return dump;
    }
    for (const auto& [enemy, intent] : store->intents) {
        dump += std::to_string(static_cast<std::uint64_t>(entt::to_integral(enemy)));
        dump += intent.skillId.empty() ? " move" : " " + intent.skillId;
        dump += "@" + std::to_string(intent.aim.x) + "," + std::to_string(intent.aim.y);
        dump += intent.mode == EIntentMode::Tile ? " tile" : " unit";
        dump += intent.attack ? " attack" : " support";
        dump += ";";
    }
    return dump;
}

// ---------------------------------------------------------------------------------------------
// Commit ordering and lifecycle
// ---------------------------------------------------------------------------------------------

void CommitRunsBeforeActionsInSameFrame() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {1, 1});
    const auto imp = fixture.place(fixture.enemies()[0], {2, 1});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(imp, 100); // the enemy leads the round: its turn starts in the same frame
    fixture.speed(warrior, 1);
    fixture.combat().autoPlay = true; // bypass the think pause so the turn really acts this frame

    // Recorded inside the enemy's turn-start hook: the commit must already be visible when the first
    // turn of the round is processed in that same frame.
    int committedAtTurnStart = -1;
    int intentsAtTurnStart = -1;
    game::RegisterHookListener(
        fixture.registry, game::gameplay::EHookEvent::OnTurnStart,
        [&](entt::registry& registry, const game::FHookContext& context) {
            if (context.owner != imp || committedAtTurnStart >= 0) {
                return;
            }
            const FIntentStats* stats = game::FindIntentStats(registry);
            committedAtTurnStart = stats != nullptr ? stats->committed : 0;
            const auto* store = registry.ctx().find<FIntentStore>();
            intentsAtTurnStart = store != nullptr ? static_cast<int>(store->intents.size()) : 0;
        });

    fixture.frame(0.1f); // session + round 1 + FTurnStarted(enemy) queued // commit -> the enemy's turn -> the intent command

    CHECK(committedAtTurnStart == 1);
    CHECK(intentsAtTurnStart == 1);
    CHECK(fixture.stats().committed == 1);
    CHECK(fixture.combat().autoActionsThisTurn == 1);
    CHECK(fixture.queue().pending.size() == 1);
    const auto* request = std::get_if<game::FActionRequest>(&fixture.queue().pending.front());
    CHECK(request != nullptr && request->skillId == "skill.strike" && request->target == warrior);

    const int hpBefore = fixture.registry.get<game::FHealth>(warrior).current;
    fixture.drain();
    CHECK(fixture.registry.get<game::FHealth>(warrior).current < hpBefore);
}

void RoundOneCommits() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 3});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(warrior, 100); // the party leads: the enemy does not act in the commit frame
    fixture.speed(imp, 1);

    fixture.frame(0.1f); // round 1: start -> beginRound -> FRoundStarted -> commit

    CHECK(fixture.snapshot().roundNumber == 1);
    CHECK(fixture.stats().committed == 1);
    const auto* store = fixture.store();
    CHECK(store != nullptr);
    const auto it = store->intents.find(imp);
    CHECK(it != store->intents.end());
    CHECK(it->second.committedRound == 1);
    CHECK(it->second.mode == EIntentMode::Unit);
    CHECK(it->second.skillId == "skill.strike");
    CHECK(it->second.target == warrior);
    CHECK((it->second.aim == game::FGridPosition{5, 5}));
}

void FinalizeCountsLeftoversOnce() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 3});
    fixture.skills(imp, {"skill.strike"});

    fixture.queueRound(1, 1);
    CHECK(fixture.stats().committed == 1);
    CHECK(fixture.stats().invalidated == 0);

    // The declarer dies before its turn: the leftover settles once, at the next round's commit.
    CHECK(game::ApplyDamageToUnit(fixture.registry, imp, 100000));
    fixture.queueRound(1, 2);
    CHECK(fixture.stats().invalidated == 1);

    // The same (session, round) never commits twice.
    fixture.queueRound(1, 2);
    CHECK(fixture.stats().invalidated == 1);

    // A later round has nothing left to settle.
    fixture.queueRound(1, 3);
    CHECK(fixture.stats().invalidated == 1);
    CHECK(fixture.stats().committed == 1);
    CHECK(warrior != entt::null);
}

void IntentIsCommittedAtRoundStart() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 3});
    const auto far = fixture.place(fixture.enemies()[1], {1, 10});
    fixture.skills(imp, {"skill.strike"});
    fixture.skills(far, {"skill.strike"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);
    fixture.speed(far, 1);

    fixture.frame(0.1f);

    const auto* store = fixture.store();
    CHECK(store != nullptr);
    const std::uint64_t round = fixture.snapshot().roundNumber;
    CHECK(round == 1);
    for (const entt::entity enemy : fixture.enemies()) {
        if (game::IsAlive(fixture.registry, enemy)) {
            const auto it = store->intents.find(enemy);
            CHECK(it != store->intents.end());
            CHECK(it->second.committedRound == round);
        }
    }
    CHECK(store->intents.at(imp).skillId == "skill.strike");
    CHECK(store->intents.at(far).skillId.empty()); // nothing in reach: an approach, not a shot

    // A later round commits again (the leftover of round one settles on the way). The leftover settle
    // counts the attack intent only — the far enemy's round-one intent was a move (see moveIntents).
    fixture.queueRound(1, 2);
    CHECK(store->intents.at(imp).committedRound == 2);
    CHECK(store->intents.at(far).committedRound == 2);
    CHECK(fixture.stats().invalidated == 1);
    CHECK(fixture.stats().committed == 2);
    CHECK(fixture.stats().moveIntents == 2);
}

// ---------------------------------------------------------------------------------------------
// Execution: unit mode
// ---------------------------------------------------------------------------------------------

void UnitModeTracksTarget() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 3});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f); // round 1 commit; the party idles
    CHECK(fixture.store()->intents.at(imp).target == warrior);
    CHECK(fixture.store()->intents.at(imp).mode == EIntentMode::Unit);

    // The target steps within reach: the committed *unit* is tracked, not the stale cell.
    fixture.registry.get<game::FGridPosition>(warrior) = {6, 5};

    fixture.endCurrentTurn(); // the warrior's turn ends; the imp is next
    fixture.flowFrame();      // take: position at the firing cell first
    CHECK(fixture.queue().pending.size() == 1);
    CHECK(std::holds_alternative<game::FMoveRequest>(fixture.queue().pending.front()));
    fixture.drain();

    const int hpBefore = fixture.registry.get<game::FHealth>(warrior).current;
    fixture.flowFrame(); // take: fire at the target's current cell
    CHECK(fixture.queue().pending.size() == 1);
    const auto* request = std::get_if<game::FActionRequest>(&fixture.queue().pending.front());
    CHECK(request != nullptr && request->target == warrior);
    CHECK((request->cell == game::FGridPosition{6, 5}));
    fixture.drain();
    fixture.flowFrame(); // settle

    CHECK(fixture.registry.get<game::FHealth>(warrior).current < hpBefore);
    CHECK(fixture.stats().whiffed == 0);
    CHECK(fixture.stats().invalidated == 0);
    CHECK(fixture.stats().lastExecuted.hit);
    CHECK(fixture.store()->intents.empty());
}

void BodyBlockCausesWhiff() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {1, 6});
    const auto imp = fixture.place(fixture.enemies()[0], {1, 1});
    const auto blocker = fixture.place(fixture.enemies()[1], {9, 9});
    fixture.skills(imp, {"skill.strike"});
    fixture.skills(blocker, {"skill.strike"});
    fixture.terrain().moveRange = 4;
    for (int y = 1; y <= 7; ++y) { // a corridor along x = 1: the single firing cell is (1, 5)
        fixture.terrain().cells[static_cast<std::size_t>(y * game::kGridWidth + 0)] = game::ETile::Wall;
        fixture.terrain().cells[static_cast<std::size_t>(y * game::kGridWidth + 2)] = game::ETile::Wall;
    }
    fixture.speed(warrior, 100);
    fixture.speed(blocker, 50);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    CHECK(fixture.store()->intents.at(imp).target == warrior);

    // The enemy's ally steps into the corridor before the imp's turn: the only route is blocked.
    fixture.registry.get<game::FGridPosition>(blocker) = {1, 3};
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Whiff);
    CHECK(evaluation.reason == "blocked");

    fixture.endCurrentTurn(); // warrior
    fixture.advanceTo(imp);   // blocker's turn ends without acting
    fixture.flowFrame();      // the whiff: a closing approach move
    CHECK(fixture.stats().whiffed == 1);
    CHECK(fixture.queue().pending.size() == 1);
    const auto* move = std::get_if<game::FMoveRequest>(&fixture.queue().pending.front());
    CHECK((move != nullptr && move->destination == game::FGridPosition{1, 2}));
    fixture.drain();
    CHECK((fixture.registry.get<game::FGridPosition>(imp) == game::FGridPosition{1, 2}));

    const int warriorHp = fixture.registry.get<game::FHealth>(warrior).current;
    fixture.flowFrame(); // the wrap-up: the turn ends, nobody is attacked
    CHECK(fixture.stats().whiffed == 1); // counted exactly once
    CHECK(fixture.queue().pending.empty());
    CHECK(fixture.registry.get<game::FHealth>(warrior).current == warriorHp);
    CHECK(!fixture.registry.ctx().get<game::FTurnRuntime>().requests.empty()); // the FEndTurn of the wrap-up
    CHECK((fixture.registry.get<game::FGridPosition>(blocker) == game::FGridPosition{1, 3}));
}

void TrapPathCostRespected() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {4, 4});
    const auto imp = fixture.place(fixture.enemies()[0], {6, 6});
    fixture.skills(imp, {"skill.strike"});
    fixture.terrain().moveRange = 3;
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    // Two firing cells of equal path length: (5, 4) sits on a spike, (4, 5) is clean. The existing AI
    // trap cost must pick the clean cell even though (5, 4) sorts earlier by cell order.
    fixture.terrain().cells[static_cast<std::size_t>(4 * game::kGridWidth + 5)] = game::ETile::Trap;
    const auto reachable = game::ComputeReachableCells(fixture.registry, imp, 3);
    CHECK((std::ranges::find(reachable, game::FGridPosition{5, 4}) != reachable.end()));
    CHECK((std::ranges::find(reachable, game::FGridPosition{4, 5}) != reachable.end()));
    CHECK((game::IsInRange(game::FGridPosition{5, 4}, game::FGridPosition{4, 4}, 1)));
    CHECK((game::HasGridLineOfSight(fixture.registry, game::FGridPosition{5, 4}, game::FGridPosition{4, 4})));

    fixture.frame(0.1f);
    CHECK(fixture.store()->intents.at(imp).target == warrior);

    fixture.endCurrentTurn();
    fixture.flowFrame();
    CHECK(fixture.queue().pending.size() == 1);
    const auto* move = std::get_if<game::FMoveRequest>(&fixture.queue().pending.front());
    CHECK((move != nullptr && move->destination == game::FGridPosition{4, 5}));
    fixture.drain();
    CHECK((fixture.registry.get<game::FGridPosition>(imp) == game::FGridPosition{4, 5}));
}

void UnitModeWhiffsOutOfReach() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {7, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    CHECK(fixture.store()->intents.at(imp).target == warrior);

    // The dodge: well beyond one move plus the blade's reach.
    fixture.registry.get<game::FGridPosition>(warrior) = {5, 11};
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Whiff);
    CHECK(evaluation.reason == "out of reach");

    fixture.endCurrentTurn();
    fixture.flowFrame();
    CHECK(fixture.stats().whiffed == 1);
    CHECK(fixture.queue().pending.size() == 1);
    const auto* move = std::get_if<game::FMoveRequest>(&fixture.queue().pending.front());
    CHECK((move != nullptr && move->destination == game::FGridPosition{5, 7})); // the last useless step
    fixture.drain();

    const int warriorHp = fixture.registry.get<game::FHealth>(warrior).current;
    fixture.flowFrame(); // wrap-up: end turn
    CHECK(fixture.stats().whiffed == 1);
    CHECK(fixture.queue().pending.empty());
    CHECK(fixture.registry.get<game::FHealth>(warrior).current == warriorHp);

    // A caster that cannot move at all: its failure is plain distance. The ghost span that classifies
    // "blocked" collapses to 0 with the move, so a rooted unit can never report a body block it had no
    // route to walk around.
    fixture.registry.get<game::FActionPoints>(imp).moveMax = 0;
    fixture.registry.get<game::FGridPosition>(warrior) = {5, 11};
    const auto rooted = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(rooted.verdict == EIntentVerdict::Whiff);
    CHECK(rooted.reason == "out of reach");
}

// ---------------------------------------------------------------------------------------------
// Execution: tile mode
// ---------------------------------------------------------------------------------------------

void TileModeKeepsTile() {
    // The dodging half: the declared tile stays the declared tile — stepping off it makes the shot whiff.
    {
        FIntentBattle fixture;
        fixture.arena();
        const auto dodger = fixture.place(fixture.party("party.warrior"), {4, 5});
        const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
        fixture.skills(imp, {"skill.cleave"}); // spread/enemy: an area skill commits to a tile
        fixture.speed(dodger, 100);
        fixture.speed(imp, 1);

        fixture.frame(0.1f);
        const auto* store = fixture.store();
        CHECK(store != nullptr);
        const auto& intent = store->intents.at(imp);
        CHECK(intent.mode == EIntentMode::Tile);
        CHECK(intent.skillId == "skill.cleave");
        CHECK(intent.attack);
        CHECK((intent.aim == game::FGridPosition{4, 5}));

        fixture.registry.get<game::FGridPosition>(dodger) = {3, 5}; // off the tile
        // Evaluation == execution: the command API can only fire a unit-targeted area skill through the
        // tile's occupant, so the empty tile is already a whiff here (WP-12 shows the same reason). Limit:
        // the spread footprint around the empty center is NOT fired either — (3,5) sits inside cleave's
        // spread around (4,5) and still takes nothing; lifting this needs the resolver to accept a cell
        // aim for unit-targeted area skills (out of this WP).
        const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
        CHECK(evaluation.verdict == EIntentVerdict::Whiff);
        CHECK(evaluation.reason == "tile empty");

        const int hpBefore = fixture.registry.get<game::FHealth>(dodger).current;
        fixture.endCurrentTurn();
        fixture.flowFrame(); // the tile is empty: the dodge worked and the turn ends
        CHECK(fixture.stats().whiffed == 1);
        CHECK(fixture.queue().pending.empty());
        CHECK(fixture.registry.get<game::FHealth>(dodger).current == hpBefore);
        CHECK((store->intents.at(imp).aim == game::FGridPosition{4, 5})); // the commit never moved
    }

    // The committed half: with the target still on the tile, the same declaration fires at that very cell.
    {
        FIntentBattle fixture;
        fixture.arena();
        const auto warrior = fixture.place(fixture.party("party.warrior"), {4, 5});
        const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
        fixture.skills(imp, {"skill.cleave"});
        fixture.speed(warrior, 100);
        fixture.speed(imp, 1);

        fixture.frame(0.1f);
        CHECK(fixture.store()->intents.at(imp).mode == EIntentMode::Tile);

        fixture.endCurrentTurn();
        fixture.flowFrame();
        CHECK(fixture.queue().pending.size() == 1);
        const auto* request = std::get_if<game::FActionRequest>(&fixture.queue().pending.front());
        CHECK(request != nullptr && request->skillId == "skill.cleave");
        CHECK((request->cell == game::FGridPosition{4, 5})); // the committed tile, not a tracked unit
        CHECK(request->target == warrior);
        const int hpBefore = fixture.registry.get<game::FHealth>(warrior).current;
        fixture.drain();
        CHECK(fixture.registry.get<game::FHealth>(warrior).current < hpBefore);
        CHECK(fixture.stats().whiffed == 0);
    }
}

void TilePlanUsesStandingCellOnly() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {6, 5});
    fixture.skills(imp, {"skill.cleave"}); // a unit-targeted area skill: tile mode by the resolved shape

    // In range from the standing cell: the plan is a tile on the target's cell.
    auto plan = game::PlanIntent(fixture.registry, imp, fixture.content());
    CHECK(plan.has_value());
    CHECK(plan->mode == EIntentMode::Tile);
    CHECK(plan->skillId == "skill.cleave");
    CHECK(plan->attack);
    CHECK((plan->aim == game::FGridPosition{5, 5}));

    // Reachable but not standing: a move-then-tile plan must NOT appear. The committed move sets the
    // position up for next round; this round's tile would whiff by its own no-move rule.
    fixture.registry.get<game::FGridPosition>(imp) = {7, 5};
    plan = game::PlanIntent(fixture.registry, imp, fixture.content());
    CHECK(plan.has_value());
    CHECK(plan->skillId.empty());
    CHECK(plan->target == entt::null);
    CHECK(!plan->attack);
    CHECK((plan->aim != game::FGridPosition{5, 5}));
}

void TileModeFiresFromStandingCell() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {4, 5});
    fixture.skills(imp, {"skill.cleave"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    CHECK(fixture.store() != nullptr);
    CHECK(fixture.store()->intents.at(imp).mode == EIntentMode::Tile);
    CHECK((fixture.store()->intents.at(imp).aim == game::FGridPosition{5, 5}));

    // Pushed off its spot but still in range and line of sight of the committed tile: it fires from
    // where it stands, without a move.
    fixture.registry.get<game::FGridPosition>(imp) = {5, 6};
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Valid);

    fixture.endCurrentTurn();
    fixture.flowFrame();
    CHECK(fixture.queue().pending.size() == 1);
    const auto* request = std::get_if<game::FActionRequest>(&fixture.queue().pending.front());
    CHECK((request != nullptr && request->cell == game::FGridPosition{5, 5}));
    CHECK(request->target == warrior);
    const int hpBefore = fixture.registry.get<game::FHealth>(warrior).current;
    fixture.drain();

    CHECK(fixture.registry.get<game::FHealth>(warrior).current < hpBefore);
    CHECK((fixture.registry.get<game::FGridPosition>(imp) == game::FGridPosition{5, 6})); // never moved
    CHECK(fixture.registry.get<game::FActionPoints>(imp).move == 1);                    // no move AP spent
}

void PushedCasterWhiffs() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {4, 5});
    fixture.skills(imp, {"skill.cleave"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    CHECK(fixture.store()->intents.at(imp).mode == EIntentMode::Tile);

    // Pushed out of range of the committed tile: the shot is displaced and cannot re-aim.
    fixture.registry.get<game::FGridPosition>(imp) = {7, 5};
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Whiff);
    CHECK(evaluation.reason == "displaced");

    fixture.endCurrentTurn();
    fixture.flowFrame();
    CHECK(fixture.stats().whiffed == 1);
    CHECK(fixture.queue().pending.empty()); // no shot, no move
    const int warriorHp = fixture.registry.get<game::FHealth>(warrior).current;
    CHECK(fixture.registry.get<game::FHealth>(warrior).current == warriorHp);
    const auto& requests = fixture.runtime().requests;
    CHECK(!requests.empty());
    const auto* endTurn = std::get_if<game::FEndTurn>(&requests.front());
    CHECK(endTurn != nullptr);
}

// ---------------------------------------------------------------------------------------------
// Invalidation and pure evaluation
// ---------------------------------------------------------------------------------------------

void TargetDeathInvalidates() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {6, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    CHECK(fixture.store()->intents.at(imp).target == warrior);

    // The tracked unit dies before the declarer's turn (trap, tick, ally — the cause never matters).
    CHECK(game::ApplyDamageToUnit(fixture.registry, warrior, 100000));
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Invalidated);
    CHECK(evaluation.reason == "target dead");

    fixture.beginFrame();
    fixture.turns.update(fixture.registry, 0.1f); // synchronize removes the dead warrior
    fixture.flowFrame();                          // take: drop the intent, let the tree recompute
    CHECK(fixture.stats().invalidated == 1);
    CHECK(fixture.stats().whiffed == 0);
    CHECK(fixture.store()->intents.empty());
    CHECK(!fixture.registry.ctx().get<game::FTurnRuntime>().requests.empty()); // the fallback ended the turn
}

void BlockedCellFallsBack() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {11, 11});
    const auto imp = fixture.place(fixture.enemies()[0], {1, 1});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    const auto* store = fixture.store();
    CHECK(store != nullptr);
    CHECK(store->intents.at(imp).skillId.empty()); // nothing in reach: an approach intent
    CHECK((store->intents.at(imp).aim == game::FGridPosition{3, 1})); // closest cell, then lowest y

    // The way out fills up before the move: no cell makes progress any more.
    fixture.terrain().cells[static_cast<std::size_t>(2 * game::kGridWidth + 2)] = game::ETile::Wall;
    fixture.terrain().cells[static_cast<std::size_t>(1 * game::kGridWidth + 2)] = game::ETile::Wall;
    fixture.terrain().cells[static_cast<std::size_t>(2 * game::kGridWidth + 1)] = game::ETile::Wall;
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Invalidated);
    CHECK(evaluation.reason == "blocked cell");

    fixture.endCurrentTurn();
    fixture.flowFrame(); // drop -> the tree recomputes -> it has nothing either
    CHECK(fixture.stats().invalidated == 0); // a move intent invalidates without feeding the attack rate
    CHECK(fixture.stats().moveIntents == 1);
    CHECK(fixture.stats().whiffed == 0);
    CHECK(fixture.store()->intents.empty());
}

void IncapacitatedInvalidates() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {6, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    CHECK(fixture.store() != nullptr && fixture.store()->intents.contains(imp));

    // A turn_start control with magnitude 0 is a certain skip (IsTurnSkipped through WP-13).
    fixture.registry.get<game::FActiveStatuses>(imp).effects.push_back(game::FStatusEffect{"status.freeze", 2, 0});
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Invalidated);
    CHECK(evaluation.reason == "incapacitated");

    // The real turn consumes the skip at its start: the intent never executes and settles once as a
    // leftover at the next round's commit.
    fixture.endCurrentTurn();
    fixture.flowFrame();
    CHECK(fixture.stats().invalidated == 0); // the skip route has not settled it yet
    CHECK(fixture.stats().whiffed == 0);
    CHECK(fixture.store()->intents.contains(imp));
    CHECK(!fixture.registry.ctx().get<game::FTurnRuntime>().requests.empty()); // the skip ended the turn

    fixture.beginFrame();
    fixture.turns.update(fixture.registry, 0.1f); // end-turn -> the round wraps
    CHECK(fixture.snapshot().roundNumber == 2);
    fixture.flow.update(fixture.registry, 0.1f); // commit round 2: the leftover settles once
    CHECK(fixture.stats().invalidated == 1);
    CHECK(fixture.store()->intents.at(imp).committedRound == 2);
}

void LivingTargetKeepsIntent() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {7, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    const FIntent before = fixture.store()->intents.at(imp);

    // The unit walks (still reachable) and a fresh unit joins the fight: neither invalidates anything.
    fixture.registry.get<game::FGridPosition>(warrior) = {7, 4};
    fixture.place(fixture.enemies()[1], {10, 10});
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Valid);
    CHECK(evaluation.reason.empty());

    const FIntent& after = fixture.store()->intents.at(imp);
    CHECK(after.target == before.target);
    CHECK(after.skillId == before.skillId);
    CHECK(after.committedRound == before.committedRound);
}

void EvaluateIntentIsStateless() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {7, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});

    fixture.queueRound(1, 1);
    const auto* store = fixture.store();
    CHECK(store != nullptr);
    const FIntent before = store->intents.at(imp);
    const FIntentStats statsBefore = store->stats;

    const auto first = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    const auto second = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    const auto third = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(first.verdict == second.verdict && second.verdict == third.verdict);
    CHECK(first.reason == second.reason && second.reason == third.reason);

    // No store mutation, no events needed: the verdict is derived from the world alone.
    CHECK(store->intents.size() == 1);
    const FIntent& after = store->intents.at(imp);
    CHECK(after.target == before.target && after.skillId == before.skillId && after.state == before.state);
    CHECK(store->stats.committed == statsBefore.committed);
    CHECK(store->stats.invalidated == statsBefore.invalidated);
    CHECK(store->stats.whiffed == statsBefore.whiffed);

    // Same world, same answer; a world change moves it and moving back restores the first answer.
    fixture.registry.get<game::FGridPosition>(warrior) = {5, 11};
    const auto far = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(far.verdict == EIntentVerdict::Whiff);
    CHECK(far.reason == "out of reach");
    fixture.registry.get<game::FGridPosition>(warrior) = {7, 5};
    const auto back = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(back.verdict == first.verdict);
    CHECK(back.reason == first.reason);
}

void TrapDeathDoesNotCorruptCounters() {
    // Variant A — a status tick kills the declarer at its own turn start: no whiff, one leftover.
    {
        FIntentBattle fixture;
        fixture.arena();
        const auto warrior = fixture.place(fixture.party("party.warrior"), {6, 5});
        const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
        fixture.skills(imp, {"skill.strike"});
        fixture.registry.get<game::FActiveStatuses>(imp).effects.push_back(game::FStatusEffect{"status.burn", 3, 50});
        fixture.speed(warrior, 100);
        fixture.speed(imp, 1);

        fixture.frame(0.1f);
        CHECK(fixture.store()->intents.contains(imp));

        fixture.endCurrentTurn();
        fixture.flowFrame(); // the tick kills the imp before it can act
        CHECK(!game::IsAlive(fixture.registry, imp));
        CHECK(fixture.stats().whiffed == 0);
        CHECK(fixture.stats().invalidated == 0);

        fixture.frame(0.1f); // synchronize: the active entry was removed // commit the next round: the leftover settles once
        CHECK(fixture.stats().invalidated == 1);
        CHECK(fixture.stats().whiffed == 0);
        CHECK(warrior != entt::null);
    }

    // Variant B — the enemy dies on spikes while carrying out its own move intent: the executed intent
    // is not a leftover, and the death is not a whiff.
    {
        FIntentBattle fixture;
        fixture.arena();
        const auto warrior = fixture.place(fixture.party("party.warrior"), {1, 1});
        const auto imp = fixture.place(fixture.enemies()[0], {5, 1});
        fixture.skills(imp, {"skill.strike"});
        fixture.registry.get<game::FHealth>(imp).current = 1; // one spike finishes it
        fixture.terrain().cells[static_cast<std::size_t>(1 * game::kGridWidth + 4)] = game::ETile::Trap;
        fixture.speed(imp, 100); // the imp leads: commit and move land in the same stretch of frames
        fixture.speed(warrior, 1);

        fixture.frame(0.1f); // commit only (the think pause still holds)
        CHECK(fixture.store()->intents.at(imp).skillId.empty());
        CHECK((fixture.store()->intents.at(imp).aim == game::FGridPosition{3, 1}));

        fixture.flowFrame();
        CHECK(fixture.queue().pending.size() == 1);
        fixture.drain(); // the path crosses the spike: the imp falls mid-move
        CHECK(!game::IsAlive(fixture.registry, imp));
        CHECK(fixture.stats().moveIntents == 1);
        CHECK(fixture.stats().whiffed == 0);
        CHECK(fixture.stats().invalidated == 0);

        fixture.beginFrame();
        fixture.turns.update(fixture.registry, 0.1f); // the removed actor advances the turn
        fixture.endCurrentTurn();                     // the warrior's round ends: round two begins
        fixture.flowFrame();                          // no leftover, no new intent
        CHECK(fixture.stats().invalidated == 0);
        CHECK(fixture.stats().whiffed == 0);
    }
}

void MoveIntentsDoNotFeedRates() {
    // The rates (whiffed/committed, invalidated/committed) are about ATTACK intents only (lead decision
    // 6); move intents are logged and counted in moveIntents but never in the rate numerators.
    {
        FIntentBattle fixture;
        fixture.arena();
        const auto warrior = fixture.place(fixture.party("party.warrior"), {11, 11});
        const auto imp = fixture.place(fixture.enemies()[0], {1, 1});
        fixture.skills(imp, {"skill.strike"});
        fixture.speed(warrior, 100);
        fixture.speed(imp, 1);

        fixture.frame(0.1f); // commit round 1: nobody in reach — a move intent
        CHECK(fixture.stats().moveIntents == 1);
        CHECK(fixture.stats().committed == 0);

        // Boxed into the corner before its turn: the move intent invalidates, but that is not an attack.
        fixture.terrain().cells[static_cast<std::size_t>(1 * game::kGridWidth + 2)] = game::ETile::Wall;
        fixture.terrain().cells[static_cast<std::size_t>(2 * game::kGridWidth + 1)] = game::ETile::Wall;
        fixture.endCurrentTurn();
        fixture.flowFrame();
        CHECK(fixture.stats().invalidated == 0); // logged, never counted
        CHECK(fixture.stats().moveIntents == 1);
        CHECK(fixture.stats().committed == 0);
        CHECK(fixture.stats().whiffed == 0);
    }
    {
        FIntentBattle fixture;
        fixture.arena();
        const auto warrior = fixture.place(fixture.party("party.warrior"), {11, 11});
        const auto imp = fixture.place(fixture.enemies()[0], {1, 1});
        fixture.skills(imp, {"skill.strike"});
        fixture.speed(warrior, 100);
        fixture.speed(imp, 1);

        fixture.frame(0.1f);
        CHECK(fixture.stats().moveIntents == 1);

        // A leftover move intent (the declarer dies before its turn) settles without touching the rate.
        CHECK(game::ApplyDamageToUnit(fixture.registry, imp, 100000));
        fixture.queueRound(1, 2);
        CHECK(fixture.stats().invalidated == 0);
        CHECK(fixture.stats().moveIntents == 1);
        CHECK(fixture.stats().committed == 0);
        CHECK(warrior != entt::null);
    }
}

// ---------------------------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------------------------

void FaithfulToExecutionPreview() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 4});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(imp, 100);
    fixture.speed(warrior, 1);
    fixture.registry.get<game::FHealth>(warrior).current = 3; // one strike (4 dmg) finishes it

    fixture.frame(0.1f); // commit; the think pause holds the action back
    const auto* store = fixture.store();
    CHECK(store != nullptr && store->intents.contains(imp));

    fixture.flowFrame(); // the committed strike fires
    CHECK(store->hasExecution);
    CHECK(store->execution.expectedAtExecution == 3); // min(4, current 3)
    fixture.drain();                                  // the warrior falls: the battle ends here

    // The shot still settles: the settle step runs before the resolved guard.
    fixture.outcomes.update(fixture.registry, 0.0f);
    CHECK(fixture.registry.ctx().get<game::FBattleOutcome>().resolved);
    fixture.frame(0.1f); // the session stop is processed
    CHECK(!store->hasExecution);
    CHECK(store->stats.lastExecuted.expectedAtExecution == 3);
    CHECK(store->stats.lastExecuted.actualDamage == 3);
    CHECK(store->stats.lastExecuted.hit);
}

void LastExecutedRecordsExecutionTimePreview() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 4});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(imp, 100);
    fixture.speed(warrior, 1);

    fixture.frame(0.1f);
    const int commitPreview = fixture.store()->intents.at(imp).expectedAtCommit;
    CHECK(commitPreview > 0);

    fixture.flowFrame(); // fire
    fixture.drain();     // the strike lands
    fixture.flowFrame(); // settle

    const FIntentStats::FLastExecuted& executed = fixture.store()->stats.lastExecuted;
    CHECK(executed.skillId == "skill.strike");
    CHECK((executed.aim == game::FGridPosition{5, 4}));
    CHECK(executed.expectedAtCommit == commitPreview);
    CHECK(executed.expectedAtExecution == commitPreview); // nothing legitimately changed in between
    CHECK(executed.actualDamage == executed.expectedAtExecution);
    CHECK(executed.hit);
}

void WhiffRateRecorded() {
    entt::registry bare;
    CHECK(game::FindIntentStats(bare) == nullptr); // no store: no report

    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {7, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    fixture.frame(0.1f);
    fixture.registry.get<game::FGridPosition>(warrior) = {5, 11}; // the dodge

    fixture.endCurrentTurn();
    fixture.flowFrame(); // the whiff

    const FIntentStats* stats = game::FindIntentStats(fixture.registry);
    CHECK(stats != nullptr);
    CHECK(stats->committed == 1);
    CHECK(stats->whiffed == 1);
    const int whiffPercent = stats->committed > 0 ? stats->whiffed * 100 / stats->committed : 0;
    CHECK(whiffPercent == 100); // a reporting number — the test pins that it is exposed and consistent
}

// ---------------------------------------------------------------------------------------------
// Planning is read-only and deterministic; the counter split and the mode parser
// ---------------------------------------------------------------------------------------------

[[nodiscard]] std::string BrainFingerprint(game::ai::FAiBrain& brain) {
    std::string out;
    for (const game::ai::FBtSlot& slot : brain.memory.slots) {
        out += std::to_string(static_cast<int>(slot.status)) + ":" + std::to_string(slot.cursor) + ":" +
               std::to_string(slot.counter) + ";";
    }
    out += std::to_string(brain.blackboard.started) + ":" + std::to_string(brain.blackboard.lastTurnSerial) + ":" +
           std::to_string(brain.blackboard.turnsTaken) + ":" +
           std::to_string(static_cast<std::uint64_t>(entt::to_integral(brain.blackboard.focusTarget))) + ":" +
           std::to_string(brain.blackboard.marks.size()) + ":" + std::to_string(brain.blackboard.rejected.size()) +
           ":" + std::to_string(brain.blackboard.decision.has_value()) + ":" + brain.blackboard.reason + ":" +
           brain.profileId + ":" + std::to_string(reinterpret_cast<std::uintptr_t>(brain.tree.get()));
    return out;
}

void PlannerDoesNotTouchBrain() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {6, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"});

    auto& brain = fixture.registry.get<game::ai::FAiBrain>(imp);
    // Dirty the memory and the blackboard: any write by the planner becomes visible.
    brain.blackboard.turnsTaken = 3;
    brain.blackboard.focusTarget = warrior;
    brain.blackboard.setMark("probe", 7);
    brain.blackboard.rejected.push_back("move@1,1");
    brain.blackboard.reason = "dirty";
    const std::string before = BrainFingerprint(brain);

    const auto plan = game::PlanIntent(fixture.registry, imp, fixture.content());
    CHECK(plan.has_value());
    CHECK(plan->skillId == "skill.strike");

    CHECK(BrainFingerprint(brain) == before);
}

void StatsCountModes() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {6, 5});
    const auto unit = fixture.place(fixture.enemies()[0], {5, 5});
    const auto tile = fixture.place(fixture.enemies()[1], {7, 5});
    fixture.skills(unit, {"skill.strike"});  // single/enemy → unit commit
    fixture.skills(tile, {"skill.cleave"});  // spread/enemy → tile commit

    fixture.queueRound(1, 1);
    const FIntentStats stats = fixture.stats();
    CHECK(stats.committed == 2);
    CHECK(stats.unitCommitted == 1);
    CHECK(stats.tileCommitted == 1);
    CHECK(stats.committed == stats.unitCommitted + stats.tileCommitted);
    CHECK(stats.moveIntents == 0);
    CHECK(fixture.store()->intents.at(unit).mode == EIntentMode::Unit);
    CHECK(fixture.store()->intents.at(tile).mode == EIntentMode::Tile);
    CHECK((fixture.store()->intents.at(tile).aim == game::FGridPosition{6, 5}));
    CHECK(warrior != entt::null);
}

void PlanAssumesRefilledMoveAp() {
    FIntentBattle fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 3});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 5});
    fixture.skills(imp, {"skill.strike"}); // range 1
    fixture.speed(warrior, 100);
    fixture.speed(imp, 1);

    // The enemy spent its move last round: move 0, max 1. Planning at round start runs OUTSIDE its own
    // turn, so it must assume the refill its turn start performs — warrior two cells away = move 1 +
    // range 1 — instead of planning from the stale pool (which would give a move intent, or worse).
    auto& points = fixture.registry.get<game::FActionPoints>(imp);
    points.move = 0;
    points.moveMax = 1;

    fixture.frame(0.1f);
    const auto* store = fixture.store();
    CHECK(store != nullptr);
    const auto& intent = store->intents.at(imp);
    CHECK(intent.skillId == "skill.strike");
    CHECK(intent.mode == EIntentMode::Unit);
    CHECK(intent.target == warrior);
    CHECK(fixture.stats().unitCommitted == 1);
    CHECK(fixture.stats().moveIntents == 0);

    // And the per-frame evaluation (WP-12 runs it during the party's turn, move AP still 0) agrees: the
    // reach check reads what the enemy's own turn will hold (moveMax), not the leftover.
    const auto evaluation = game::EvaluateIntent(fixture.registry, imp, fixture.content());
    CHECK(evaluation.verdict == EIntentVerdict::Valid);
    CHECK(evaluation.reason.empty());
}

void DeterministicOrder() {
    const auto build = [](FIntentBattle& fixture) {
        fixture.place(fixture.party("party.warrior"), {7, 5});
        fixture.place(fixture.party("party.archer"), {9, 9});
        fixture.place(fixture.enemies()[0], {5, 5});
        fixture.place(fixture.enemies()[1], {2, 2});
        fixture.skills(fixture.enemies()[0], {"skill.strike"});
        fixture.skills(fixture.enemies()[1], {"skill.cleave", "skill.strike"});
    };

    FIntentBattle first(7, 1, 0);
    first.arena();
    build(first);
    first.queueRound(1, 1);

    FIntentBattle second(7, 1, 0);
    second.arena();
    build(second);
    second.queueRound(1, 1);

    CHECK(!IntentDump(first).empty());
    CHECK(IntentDump(first) == IntentDump(second));

    // The same seed reproduces the sequence across rounds, leftovers included.
    first.queueRound(1, 2);
    second.queueRound(1, 2);
    CHECK(IntentDump(first) == IntentDump(second));
}

void IntentModeParsesAndRejects() {
    FIntentBattle fixture;
    fixture.arena();
    const auto& content = fixture.content();
    const auto document = [](const char* mode) {
        return nlohmann::json{
            {"schema_version", 1},
            {"profiles", nlohmann::json::array({
                {{"id", "ai.default"}, {"intent_mode", mode}, {"tree", {{"type", "Wait"}}}},
                {{"id", "ai.boss"}, {"tree", {{"type", "Wait"}}}},
            })}};
    };

    const std::pair<const char*, std::optional<EIntentMode>> valid[] = {
        {"tile", EIntentMode::Tile},
        {"unit", EIntentMode::Unit},
        {"hybrid", std::nullopt},
    };
    for (const auto& [mode, expected] : valid) {
        std::string error;
        auto library = game::ai::FAiLibrary::fromDocument(document(mode), content, game::ai::FAiNodeRegistry::builtin(), error);
        CHECK(library.has_value());
        CHECK(library->fallback().intentMode == expected);
    }

    std::string error;
    auto broken = game::ai::FAiLibrary::fromDocument(document("sideways"), content, game::ai::FAiNodeRegistry::builtin(), error);
    CHECK(!broken.has_value());
    CHECK(error.find("intent_mode") != std::string::npos);
    CHECK(error.find("sideways") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// Whole battles with intents on (the WP acceptance run)
// ---------------------------------------------------------------------------------------------

struct FIntentBattleSummary {
    bool finished{false};
    bool victory{false};
    int rounds{0};
    FIntentStats stats;
};

/// The AiTests PlayBattle drive: a full battle through turns/flow/actions/outcomes with the bus frame
/// started every step, exactly like the engine. `withIntents=false` erases the bus first, so
/// CommitEnemyIntents is a no-op — the pre-WP-11 behavior, for the win-count comparison.
[[nodiscard]] FIntentBattleSummary PlayIntentBattle(std::uint32_t seed, int room, bool withIntents) {
    FIntentBattle fixture(seed, 1, room);
    if (!withIntents) {
        fixture.registry.ctx().erase<game::FEventBus>();
    }
    fixture.combat().autoPlay = true; // no think pause: the battles run at full speed
    auto& outcome = fixture.registry.ctx().get<game::FBattleOutcome>();

    int steps = 0;
    while (!outcome.resolved && steps++ < 6000) {
        fixture.beginFrame();
        fixture.turns.update(fixture.registry, 0.1f);
        fixture.flow.update(fixture.registry, 0.1f);
        fixture.actions.update(fixture.registry, 0.1f);
        fixture.outcomes.update(fixture.registry, 0.1f);
    }

    FIntentBattleSummary summary;
    summary.finished = outcome.resolved && steps < 6000;
    summary.victory = outcome.victory;
    summary.rounds = static_cast<int>(fixture.snapshot().roundNumber);
    if (const FIntentStats* stats = game::FindIntentStats(fixture.registry); stats != nullptr) {
        summary.stats = *stats;
    }
    return summary;
}

void AutoBattlesWithIntentsFinish() {
    // WP acceptance: "자동 전투 3시드 완주" — 3 seeds x 3 rooms at depth 1, with the bus (intents ON), and
    // the same 9 battles with the bus erased (intents OFF) so the lead can compare the distributions.
    // No win-rate threshold: the assertions are completion and that the pipeline really ran.
    int winsWith = 0;
    int winsWithout = 0;
    int totalCommitted = 0;
    for (int room = 0; room < 3; ++room) {
        for (std::uint32_t seed = 1; seed <= 3; ++seed) {
            const FIntentBattleSummary battle = PlayIntentBattle(seed, room, true);
            if (battle.victory) {
                ++winsWith;
            }
            totalCommitted += battle.stats.committed;
            std::cout << "  intent battle seed " << seed << " room " << room << ": victory " << battle.victory
                      << " rounds " << battle.rounds << " committed " << battle.stats.committed << " (tile "
                      << battle.stats.tileCommitted << " unit " << battle.stats.unitCommitted << ") move "
                      << battle.stats.moveIntents << " whiffed " << battle.stats.whiffed << " invalidated "
                      << battle.stats.invalidated << '\n';
            CHECK(battle.finished);
            CHECK(static_cast<std::uint64_t>(battle.rounds) <= game::kMaxBattleRounds);

            const FIntentBattleSummary without = PlayIntentBattle(seed, room, false);
            if (without.victory) {
                ++winsWithout;
            }
            CHECK(without.finished);
        }
    }
    CHECK(totalCommitted > 0); // the 9 battles really ran the commit pipeline
    std::cout << "  party wins with intents " << winsWith << "/9, without " << winsWithout << "/9\n";
}

} // namespace

int main() {
    std::cout.setf(std::ios::unitbuf); // a crash must still show how far the suite got

    // Save safety: ctest exports no GAME01P_SAVE_DIR for feature tests, so outside build-test.ps1 a plain
    // run would let run bookkeeping touch — and a defeat delete — the user's real save. Commit an override
    // before any dungeon service can touch disk.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "intent_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("intent", {
        {"CommitRunsBeforeActionsInSameFrame", CommitRunsBeforeActionsInSameFrame},
        {"RoundOneCommits", RoundOneCommits},
        {"FinalizeCountsLeftoversOnce", FinalizeCountsLeftoversOnce},
        {"IntentIsCommittedAtRoundStart", IntentIsCommittedAtRoundStart},
        {"UnitModeTracksTarget", UnitModeTracksTarget},
        {"BodyBlockCausesWhiff", BodyBlockCausesWhiff},
        {"TrapPathCostRespected", TrapPathCostRespected},
        {"UnitModeWhiffsOutOfReach", UnitModeWhiffsOutOfReach},
        {"TileModeKeepsTile", TileModeKeepsTile},
        {"TilePlanUsesStandingCellOnly", TilePlanUsesStandingCellOnly},
        {"PlannerDoesNotTouchBrain", PlannerDoesNotTouchBrain},
        {"TileModeFiresFromStandingCell", TileModeFiresFromStandingCell},
        {"PushedCasterWhiffs", PushedCasterWhiffs},
        {"TargetDeathInvalidates", TargetDeathInvalidates},
        {"BlockedCellFallsBack", BlockedCellFallsBack},
        {"IncapacitatedInvalidates", IncapacitatedInvalidates},
        {"LivingTargetKeepsIntent", LivingTargetKeepsIntent},
        {"EvaluateIntentIsStateless", EvaluateIntentIsStateless},
        {"TrapDeathDoesNotCorruptCounters", TrapDeathDoesNotCorruptCounters},
        {"MoveIntentsDoNotFeedRates", MoveIntentsDoNotFeedRates},
        {"FaithfulToExecutionPreview", FaithfulToExecutionPreview},
        {"LastExecutedRecordsExecutionTimePreview", LastExecutedRecordsExecutionTimePreview},
        {"WhiffRateRecorded", WhiffRateRecorded},
        {"DeterministicOrder", DeterministicOrder},
        {"StatsCountModes", StatsCountModes},
        {"PlanAssumesRefilledMoveAp", PlanAssumesRefilledMoveAp},
        {"IntentModeParsesAndRejects", IntentModeParsesAndRejects},
        {"AutoBattlesWithIntentsFinish", AutoBattlesWithIntentsFinish},
    });
}
