// WP-12 combat information UI tests. Everything asserted here is the render-free display contract:
// BuildCombatInfoSnapshot consumes WP-11's committed intents, WP-03's preview and the grid pathfinder, and
// the HUD/board only draw the snapshot. The battle fixture is the intent_tests boot recipe (real dungeon
// battle, full turn pipeline) so the intents in the store are the ones CommitEnemyIntents produced; the
// tests then overwrite individual intents when they need a specific geometry (three shots on one cell,
// a lethal mark, a dead target).
//
// Covered: turn ribbon marker, lethal-first order, same-cell collapse, preview==PreviewSkill,
// per-frame damage recalculation, always-on numbers for damage intents only, invalid reason, party threat
// aggregation, reach cells == the pathfinder construction and HUD submits through SubmitBattleCommand.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <entt/entt.hpp>

#include "core/EngineState.h"
#include "core/InputState.h"
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
#include "ecs/systems/EmberCombatHudSystem.h"
#include "ecs/systems/EmberCombatInfo.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "presentation/FUiFrame.h"
#include "states/FEmberBattleState.h"

namespace {

using game::ECommandSource;
using game::FIntent;
using game::FIntentStore;
using game::ecs::BuildCombatInfoSnapshot;
using game::ecs::FCombatInfoSnapshot;
using game::ecs::FIntentBadge;
using game::ecs::FTurnOrderLine;

// ---------------------------------------------------------------------------------------------
// The intent_tests boot recipe: a real Ember battle with the whole turn pipeline.
// ---------------------------------------------------------------------------------------------

std::filesystem::path SaveDir() {
    if (const char* value = std::getenv("GAME01P_SAVE_DIR"); value != nullptr && *value != '\0') {
        return std::filesystem::path(value);
    }
    return std::filesystem::path("out");
}

struct FCombatInfoBattle {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    FEmberBattleState battle;
    std::filesystem::path checkpoint;
    std::vector<entt::entity> everyone;

    explicit FCombatInfoBattle(std::uint32_t seed = 7, int depth = 1, int room = 0) {
        std::filesystem::create_directories(SaveDir());
        static int counter = 0;
        checkpoint = SaveDir() / ("combat-info-checkpoint-" + std::to_string(counter++) + ".json");
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        game::InitializeEventBus(registry);
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

    ~FCombatInfoBattle() {
        battle.OnExit(registry);
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(checkpoint.string() + ".tmp", error);
    }

    FCombatInfoBattle(const FCombatInfoBattle&) = delete;
    FCombatInfoBattle& operator=(const FCombatInfoBattle&) = delete;

    game::gameplay::FContentRegistry& content() { return registry.ctx().get<game::gameplay::FContentRegistry>(); }
    FIntentStore* store() { return registry.ctx().find<FIntentStore>(); }
    game::FTurnSnapshot snapshot() { return registry.ctx().get<game::FTurnRuntime>().controller.snapshot(); }
    game::FActionQueue& queue() { return registry.ctx().get<game::FActionQueue>(); }
    game::FCombatRuntime& combat() { return registry.ctx().get<game::FCombatRuntime>(); }
    game::FGridTerrain& terrain() { return registry.ctx().get<game::FGridTerrain>(); }
    game::FTurnRuntime& runtime() { return registry.ctx().get<game::FTurnRuntime>(); }

    /// Kill the generated roster and clear the terrain: the test places its own duel.
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

    void applyStatus(entt::entity unit, std::string_view statusId) {
        if (!registry.all_of<game::FActiveStatuses>(unit)) {
            registry.emplace<game::FActiveStatuses>(unit);
        }
        const game::gameplay::FStatusContent* definition = content().statuses.find(statusId);
        CHECK(definition != nullptr);
        game::ApplyStatus(registry.get<game::FActiveStatuses>(unit), *definition);
    }

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

    void beginFrame() {
        if (auto* bus = registry.ctx().find<game::FEventBus>()) {
            bus->beginFrame();
        }
    }

    /// One engine frame start->flow. Round events are frame-scoped, so every driven frame starts the bus.
    void frame(float dt = 0.5f) {
        beginFrame();
        turns.update(registry, dt);
        flow.update(registry, dt);
    }

    void drain() { actions.update(registry, 0.0f); }
};

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

/// One crafted intent: the store exists (CommitEnemyIntents created it); the geometry is set by the test.
void SetIntent(FCombatInfoBattle& fixture, entt::entity enemy, const FIntent& intent) {
    FIntentStore* store = fixture.store();
    CHECK(store != nullptr);
    store->intents[enemy] = intent;
}

void SetUnitAttack(FCombatInfoBattle& fixture, entt::entity enemy, entt::entity target) {
    FIntent intent;
    intent.mode = game::EIntentMode::Unit;
    intent.skillId = "skill.strike";
    intent.target = target;
    intent.aim = fixture.registry.get<game::FGridPosition>(target);
    intent.attack = true;
    intent.committedRound = fixture.snapshot().roundNumber;
    SetIntent(fixture, enemy, intent);
}

void SetMoveIntent(FCombatInfoBattle& fixture, entt::entity enemy, game::FGridPosition aim) {
    FIntent intent;
    intent.mode = game::EIntentMode::Unit;
    intent.target = entt::null;
    intent.aim = aim;
    intent.attack = false;
    intent.committedRound = fixture.snapshot().roundNumber;
    SetIntent(fixture, enemy, intent);
}

const FIntentBadge* FindBadge(const FCombatInfoSnapshot& info, entt::entity enemy) {
    for (const FIntentBadge& badge : info.intents) {
        if (badge.enemy == enemy) return &badge;
    }
    return nullptr;
}

const game::ecs::FThreatTotal* FindThreat(const FCombatInfoSnapshot& info, entt::entity unit) {
    for (const game::ecs::FThreatTotal& threat : info.threats) {
        if (threat.unit == unit) return &threat;
    }
    return nullptr;
}

int SumBadgeDamage(const FCombatInfoSnapshot& info) {
    int total = 0;
    for (const FIntentBadge& badge : info.intents) {
        if (badge.valid && badge.attack) total += badge.expectedDamage;
    }
    return total;
}

int SumThreatDamage(const FCombatInfoSnapshot& info) {
    int total = 0;
    for (const game::ecs::FThreatTotal& threat : info.threats) total += threat.incomingThisRound;
    return total;
}

/// Puts every living unit at its generated cell with no enemies in reach, then the test places its own.
/// The initial frame commits intents for whatever enemies survive `arena()`; the tests overwrite them.
void OpenBattle(FCombatInfoBattle& fixture, std::int32_t partySpeed = 100, std::int32_t enemySpeed = 1) {
    fixture.arena();
    for (const entt::entity unit : fixture.everyone) {
        if (fixture.registry.all_of<game::FPartyMember>(unit)) fixture.speed(unit, partySpeed);
        else fixture.speed(unit, enemySpeed);
    }
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// ---------------------------------------------------------------------------------------------
// 1. Turn ribbon: current round only, next round is a marker (no sorted preview)
// ---------------------------------------------------------------------------------------------

template <class T>
concept HasNextRoundMember = requires(const T& value) { value.nextRound; };

void SnapshotTurnOrderMarksSpeculative() {
    // Structural: there is no next-round order array to draw a false promise from.
    static_assert(!HasNextRoundMember<FTurnOrderLine>);

    FCombatInfoBattle fixture;
    CHECK(fixture.checkpoint.parent_path() == SaveDir());  // save isolation (CMake's GAME01P_SAVE_DIR)
    OpenBattle(fixture);
    fixture.place(fixture.party("party.warrior"), {5, 5});
    fixture.place(fixture.enemies()[0], {5, 3});
    fixture.skills(fixture.enemies()[0], {"skill.strike"});
    fixture.frame(0.1f);

    const FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    CHECK(info.order.nextRoundDecidedAtRoundStart);
    CHECK(!info.order.currentRound.empty());

    std::size_t livingEntries = 0;
    for (const game::FTurnEntry& entry : fixture.snapshot().entries) {
        if (entry.state == game::ETurnEntryState::Skipped || entry.state == game::ETurnEntryState::Completed) continue;
        ++livingEntries;
    }
    CHECK(info.order.currentRound.size() == livingEntries);
}

// ---------------------------------------------------------------------------------------------
// 2. Density: lethal first, same-cell collapse, "+N"
// ---------------------------------------------------------------------------------------------

void LethalIntentsRankFirst() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5}, 1);  // 1 HP: one hit is lethal
    const auto archer = fixture.place(fixture.party("party.archer"), {7, 5});
    std::vector<entt::entity> ids = fixture.enemies();
    std::ranges::sort(ids, [](entt::entity left, entt::entity right) {
        return entt::to_integral(left) < entt::to_integral(right);
    });
    const auto lower = fixture.place(ids[0], {5, 4});   // lower entity id
    const auto higher = fixture.place(ids[1], {7, 4});  // higher entity id
    const auto invalid = fixture.place(ids[2], {3, 4});
    for (const entt::entity enemy : {lower, higher, invalid}) {
        fixture.skills(enemy, {"skill.strike"});
    }
    fixture.frame(0.1f);

    // Deliberately inverted: the LOWER id declares the non-lethal shot and the HIGHER id the lethal one.
    // Entity-id-ascending alone would put the non-lethal first; only the lethal priority can rank the higher
    // id ahead, so this pins the sort instead of the tie-break.
    CHECK(entt::to_integral(lower) < entt::to_integral(higher));
    SetUnitAttack(fixture, lower, archer);   // 100 HP archer: not lethal
    SetUnitAttack(fixture, higher, warrior); // 1 HP warrior: lethal
    // A third, invalid intent (its target is already down) must rank last whatever its entity id.
    const auto dead = fixture.party("party.mage");
    CHECK(!game::IsAlive(fixture.registry, dead));
    SetUnitAttack(fixture, invalid, dead);

    const FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    CHECK(info.intents.size() == 3);
    CHECK(info.intents.front().enemy == higher);
    CHECK(info.intents.front().lethal);
    CHECK(info.intents.front().cell == fixture.registry.get<game::FGridPosition>(warrior));
    const FIntentBadge* lowerBadge = FindBadge(info, lower);
    CHECK(lowerBadge != nullptr && !lowerBadge->lethal);
    CHECK(info.intents.back().enemy == invalid);
    CHECK(!info.intents.back().valid);
    CHECK(info.intents.back().expectedDamage == 0);
}

void OverlapCollapsesToBadge() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5}, 40);
    const auto first = fixture.place(fixture.enemies()[0], {5, 4});
    const auto second = fixture.place(fixture.enemies()[1], {4, 5});
    const auto third = fixture.place(fixture.enemies()[2], {6, 5});
    for (const entt::entity enemy : {first, second, third}) {
        fixture.skills(enemy, {"skill.strike"});
    }
    fixture.frame(0.1f);
    SetUnitAttack(fixture, first, warrior);
    SetUnitAttack(fixture, second, warrior);
    SetUnitAttack(fixture, third, warrior);

    const FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    CHECK(info.intents.size() == 1);
    const FIntentBadge& badge = info.intents.front();
    CHECK(badge.cell == fixture.registry.get<game::FGridPosition>(warrior));
    CHECK(badge.stacked == 3);
    CHECK(badge.stacked - 1 == 2);  // the drawn "+2"
    CHECK(!info.compact);           // three intents stay under the authored threshold (12)

    // The threshold is data (FUiLayout): below the intent count the snapshot reports compact mode.
    if (!fixture.registry.ctx().contains<game::ui::FUiFrame>()) {
        fixture.registry.ctx().emplace<game::ui::FUiFrame>();
    }
    fixture.registry.ctx().get<game::ui::FUiFrame>().spec.intentCompactThreshold = 2;
    auto* window = fixture.registry.ctx().find<game::FEngineWindowState>();
    if (window == nullptr) window = &fixture.registry.ctx().emplace<game::FEngineWindowState>();
    window->width = 1280;
    window->height = 800;
    CHECK(BuildCombatInfoSnapshot(fixture.registry).compact);
}

// ---------------------------------------------------------------------------------------------
// 3. Preview is WP-03's numbers (no second damage pipeline)
// ---------------------------------------------------------------------------------------------

void PreviewMatchesPipeline() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 4});
    fixture.skills(warrior, {"skill.strike"});
    fixture.skills(imp, {"skill.strike"});
    fixture.frame(0.1f);  // the party leads: it is the warrior's turn
    CHECK(fixture.snapshot().currentTurn->actor == warrior);

    fixture.combat().selectedSkill = 0;
    auto& selection = fixture.registry.ctx().get<game::FGridSelection>();
    selection.hovered = fixture.registry.get<game::FGridPosition>(imp);

    const FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    const auto& slots = fixture.registry.get<game::FSkillLoadout>(warrior).slots;
    const auto resolved = game::ResolveSkillSlot(slots[0], fixture.content());
    CHECK(resolved.has_value());
    const auto expected = game::PreviewSkill(fixture.registry, warrior, *resolved,
                                             fixture.registry.get<game::FGridPosition>(imp),
                                             fixture.registry.get<game::FGridPosition>(warrior));
    CHECK(expected.has_value());
    CHECK(info.preview.size() == expected->targets.size());
    for (std::size_t index = 0; index < info.preview.size(); ++index) {
        const game::FPreviewTarget& target = expected->targets[index];
        CHECK(info.preview[index].cell == fixture.registry.get<game::FGridPosition>(target.unit));
        CHECK(info.preview[index].damage == target.damage);
        CHECK(info.preview[index].heal == target.heal);
        CHECK(info.preview[index].lethal == target.lethal);
    }
}

// ---------------------------------------------------------------------------------------------
// 3b. Move preview follows the HUD's move pool (no path the unit cannot walk)
// ---------------------------------------------------------------------------------------------

void MovePreviewFollowsMoveAp() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    fixture.place(fixture.enemies()[0], {10, 10});  // out of reach: the hover is a plain move
    fixture.skills(warrior, {"skill.strike"});
    fixture.frame(0.1f);
    CHECK(fixture.snapshot().currentTurn->actor == warrior);
    CHECK(fixture.registry.get<game::FActionPoints>(warrior).moveMax > 0);

    fixture.registry.ctx().emplace<game::FEngineWindowState>();
    auto& window = fixture.registry.ctx().get<game::FEngineWindowState>();
    window.width = 1280;
    window.height = 800;
    auto& input = fixture.registry.ctx().emplace<game::FInputState>();
    CHECK(game::ui::InitializeUiFrame(fixture.registry));
    game::ecs::EmberCombatHudSystem hud;
    hud.boundTurn = fixture.snapshot().currentTurn->turnSerial;
    const auto isoView =
        game::ui::FindBattleIsoView(fixture.registry, fixture.registry.ctx().get<game::FGridMetrics>());
    CHECK(isoView.has_value());
    input.mousePos = isoView->cellCenter(6, 5);  // empty, one cell away

    hud.update(fixture.registry, 0.f);
    const game::ecs::FCombatInfoSnapshot* info = game::ecs::FindCombatInfoSnapshot(fixture.registry);
    CHECK(info != nullptr);
    CHECK(info->moveDestination.has_value());
    CHECK((*info->moveDestination == game::FGridPosition{6, 5}));
    CHECK(!info->movePath.empty());

    // No move AP: the HUD offers no reachable cell, so the board must not draw a path. The builder reads the
    // HUD's reachable list; a second reachability computation behind its back would still find (6,5) here.
    fixture.registry.get<game::FActionPoints>(warrior).move = 0;
    hud.update(fixture.registry, 0.f);
    info = game::ecs::FindCombatInfoSnapshot(fixture.registry);
    CHECK(info != nullptr);
    CHECK(info->movePath.empty());
    CHECK(!info->moveDestination.has_value());
}

// ---------------------------------------------------------------------------------------------
// 4b. A naturally committed intent: the displayed number is the one the shot really deals
// ---------------------------------------------------------------------------------------------

void IntentNumberMatchesExecution() {
    FCombatInfoBattle fixture;
    // The enemy leads the round (party speed 1, enemy 100), so its declared shot is the round's first turn
    // and the board can be read before it fires.
    OpenBattle(fixture, 1, 100);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 4});  // adjacent, inside strike range
    fixture.skills(imp, {"skill.strike"});
    fixture.frame(0.1f);  // round 1: FRoundStarted -> CommitEnemyIntents (no SetIntent anywhere)

    CHECK(fixture.snapshot().currentTurn.has_value());
    CHECK(fixture.snapshot().currentTurn->actor == imp);
    const game::FIntentStore* store = fixture.store();
    CHECK(store != nullptr);
    CHECK(store->intents.contains(imp));
    CHECK(store->intents.at(imp).skillId == "skill.strike");
    CHECK(store->intents.at(imp).attack);
    CHECK(store->stats.committed == 1);  // the commit pipeline really ran, once

    // What the board promises at the start of the enemy's turn, before the shot leaves.
    const FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    const FIntentBadge* badge = FindBadge(info, imp);
    CHECK(badge != nullptr);
    CHECK(badge->valid && badge->attack);
    const int promised = badge->expectedDamage;
    CHECK(promised > 0);

    // Run the real pipeline (turn flow, intent step, submit, drain, settle) until the shot is measured.
    const game::FIntentStats* stats = game::FindIntentStats(fixture.registry);
    int steps = 0;
    while (steps++ < 400 && (stats == nullptr || !stats->lastExecuted.hit)) {
        fixture.frame(0.1f);
        fixture.drain();
        stats = game::FindIntentStats(fixture.registry);
    }
    CHECK(stats != nullptr);
    CHECK(stats->lastExecuted.hit);
    CHECK(stats->lastExecuted.skillId == "skill.strike");
    // WP acceptance "프리뷰와 실제 피해 불일치 0": the number the board showed, the number the execution
    // recorded and the damage that landed are one value.
    CHECK(stats->lastExecuted.expectedAtExecution == promised);
    CHECK(stats->lastExecuted.actualDamage == promised);
}

// ---------------------------------------------------------------------------------------------
// 4. Intent numbers: per-frame recalculation, damage intents only, invalid reasons
// ---------------------------------------------------------------------------------------------

void IntentNumbersRecalcPerFrame() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 4});
    fixture.skills(imp, {"skill.strike"});
    fixture.frame(0.1f);
    SetUnitAttack(fixture, imp, warrior);

    const FCombatInfoSnapshot before = BuildCombatInfoSnapshot(fixture.registry);
    const FIntentBadge* badge = FindBadge(before, imp);
    CHECK(badge != nullptr && badge->expectedDamage > 0);
    const int committed = fixture.store()->intents.at(imp).expectedAtCommit;

    // A stat buff on the caster changes the promise immediately; the commit-time reference stays put.
    fixture.applyStatus(imp, "status.buff");
    const FCombatInfoSnapshot after = BuildCombatInfoSnapshot(fixture.registry);
    badge = FindBadge(after, imp);
    CHECK(badge != nullptr);
    CHECK(badge->expectedDamage > before.intents.front().expectedDamage);
    CHECK(fixture.store()->intents.at(imp).expectedAtCommit == committed);
    CHECK(badge->attack);
}

void IntentNumbersAlwaysOn() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 4});
    fixture.skills(imp, {"skill.strike"});
    fixture.frame(0.1f);

    SetUnitAttack(fixture, imp, warrior);
    FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    const FIntentBadge* badge = FindBadge(info, imp);
    CHECK(badge != nullptr);
    CHECK(badge->attack && badge->expectedDamage > 0);
    CHECK(badge->glyph == "sword");  // melee damage skill -> the sword icon token

    SetMoveIntent(fixture, imp, {5, 4});
    info = BuildCombatInfoSnapshot(fixture.registry);
    badge = FindBadge(info, imp);
    CHECK(badge != nullptr);
    CHECK(!badge->attack);
    CHECK(badge->expectedDamage == 0);
    CHECK(badge->glyph == "boots");
}

void InvalidIntentShowsReason() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 4});
    fixture.skills(imp, {"skill.strike"});
    fixture.frame(0.1f);
    SetUnitAttack(fixture, imp, warrior);

    CHECK(game::ApplyDamageToUnit(fixture.registry, warrior, 100000));
    const FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    const FIntentBadge* badge = FindBadge(info, imp);
    CHECK(badge != nullptr);
    CHECK(!badge->valid);
    CHECK(badge->tooltip.find("target dead") != std::string::npos);  // exact WP-11 reason string
    CHECK(badge->expectedDamage == 0);
    CHECK(SumBadgeDamage(info) == 0);
}

// ---------------------------------------------------------------------------------------------
// 5. Party threat totals
// ---------------------------------------------------------------------------------------------

void IncomingDamageAggregated() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5}, 1);  // 1 HP: one hit is lethal
    const auto archer = fixture.place(fixture.party("party.archer"), {7, 5});
    const auto first = fixture.place(fixture.enemies()[0], {5, 4});
    const auto second = fixture.place(fixture.enemies()[1], {4, 5});
    const auto third = fixture.place(fixture.enemies()[2], {7, 4});
    for (const entt::entity enemy : {first, second, third}) {
        fixture.skills(enemy, {"skill.strike"});
    }
    fixture.frame(0.1f);
    SetUnitAttack(fixture, first, warrior);
    SetUnitAttack(fixture, second, warrior);
    SetUnitAttack(fixture, third, archer);

    const FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    const auto* warriorThreat = FindThreat(info, warrior);
    const auto* archerThreat = FindThreat(info, archer);
    CHECK(warriorThreat != nullptr && archerThreat != nullptr);
    CHECK(warriorThreat->incomingThisRound > 0);
    CHECK(warriorThreat->lethalThisRound ==
          (warriorThreat->incomingThisRound >= fixture.registry.get<game::FHealth>(warrior).current));
    CHECK(warriorThreat->lethalThisRound);   // two hits exceed 7 HP
    CHECK(!archerThreat->lethalThisRound);   // one hit cannot kill 100 HP
    // The invariant the UI relies on: cards and badges share one per-frame preview.
    CHECK(SumThreatDamage(info) == SumBadgeDamage(info));
}

// ---------------------------------------------------------------------------------------------
// 6. Reach cells use the pathfinder (walls and bodies), hover only
// ---------------------------------------------------------------------------------------------

void ReachCellsMatchPathfinder() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {9, 9});
    (void)warrior;
    const auto imp = fixture.place(fixture.enemies()[0], {1, 1});
    const auto body = fixture.place(fixture.enemies()[1], {1, 2});
    fixture.skills(imp, {"skill.strike"});
    fixture.skills(body, {"skill.strike"});
    fixture.frame(0.1f);
    SetUnitAttack(fixture, imp, fixture.party("party.warrior"));

    auto& terrain = fixture.terrain();
    terrain.cells.fill(game::ETile::Floor);
    terrain.moveRange = 2;
    const auto wall = [&terrain](int x, int y) {
        terrain.cells[static_cast<std::size_t>(y * game::kGridWidth + x)] = game::ETile::Wall;
    };
    wall(0, 1);
    wall(2, 1);

    auto& selection = fixture.registry.ctx().get<game::FGridSelection>();
    selection.hovered = fixture.registry.get<game::FGridPosition>(imp);

    const FCombatInfoSnapshot info = BuildCombatInfoSnapshot(fixture.registry);
    const FIntentBadge* badge = FindBadge(info, imp);
    CHECK(badge != nullptr);
    CHECK(!badge->reachCells.empty());

    // The same construction the lead decision specifies: one move's reachable cells plus the standing
    // cell, then every in-grid cell in skill range with line of sight from one of them.
    std::vector<game::FGridPosition> standing{fixture.registry.get<game::FGridPosition>(imp)};
    for (const game::FGridPosition& cell : game::ComputeReachableCells(fixture.registry, imp, terrain.moveRange)) {
        standing.push_back(cell);
    }
    std::vector<game::FGridPosition> expected;
    for (int y = 0; y < game::kGridHeight; ++y) {
        for (int x = 0; x < game::kGridWidth; ++x) {
            if (terrain.at(x, y) == game::ETile::Wall) continue;
            const game::FGridPosition cell{x, y};
            for (const game::FGridPosition& origin : standing) {
                if (game::IsInRange(origin, cell, 1) && game::HasGridLineOfSight(fixture.registry, origin, cell)) {
                    expected.push_back(cell);
                    break;
                }
            }
        }
    }
    CHECK(badge->reachCells == expected);

    const auto has = [&badge](int x, int y) {
        const game::FGridPosition cell{x, y};
        return std::ranges::find(badge->reachCells, cell) != badge->reachCells.end();
    };
    CHECK(!has(0, 1));  // the wall cell is not a reach cell
    CHECK(!has(2, 1));  // nor the other wall
    CHECK(has(1, 1));   // the standing cell is included
    // Cells that would only appear if the body-blocked ally could stand on (1,2) (its neighbours outside
    // every real standing cell's range).
    CHECK(!has(2, 2));
    CHECK(!has(0, 2));
}

// ---------------------------------------------------------------------------------------------
// 7. Submissions go through WP-06 (the HUD's five sites share one helper)
// ---------------------------------------------------------------------------------------------

void SubmitPathSites() {
    FCombatInfoBattle fixture;
    OpenBattle(fixture);
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto far = fixture.place(fixture.enemies()[0], {10, 10});
    const auto adjacent = fixture.place(fixture.enemies()[1], {6, 5});
    fixture.skills(warrior, {"skill.strike"});
    fixture.skills(far, {"skill.strike"});
    fixture.skills(adjacent, {"skill.strike"});
    fixture.frame(0.1f);
    CHECK(fixture.snapshot().currentTurn->actor == warrior);

    fixture.registry.ctx().emplace<game::FEngineWindowState>();
    auto& window = fixture.registry.ctx().get<game::FEngineWindowState>();
    window.width = 1280;
    window.height = 800;
    auto& input = fixture.registry.ctx().emplace<game::FInputState>();
    CHECK(game::ui::InitializeUiFrame(fixture.registry));

    game::ecs::EmberCombatHudSystem hud;
    hud.boundTurn = fixture.snapshot().currentTurn->turnSerial;
    const auto isoView =
        game::ui::FindBattleIsoView(fixture.registry, fixture.registry.ctx().get<game::FGridMetrics>());
    CHECK(isoView.has_value());

    // 1) Unit-target skill click (EmberCombatHudSystem's FActionRequest site): a real enemy in range.
    input.mousePos = isoView->cellCenter(6, 5);
    input.mouseLeftPressed = true;
    hud.update(fixture.registry, 0.f);
    input.mouseLeftPressed = false;
    CHECK(fixture.queue().pending.size() == 1);
    auto& outbox = fixture.registry.ctx().get<game::FCommandOutbox>();  // created by the first submit
    const auto* unitAction = std::get_if<game::FActionRequest>(&fixture.queue().pending.front());
    CHECK(unitAction != nullptr);
    CHECK(unitAction->target == adjacent);
    CHECK(unitAction->skillId == "skill.strike");
    CHECK((unitAction->cell == game::FGridPosition{6, 5}));
    CHECK(outbox.meta.size() == 1);
    CHECK(outbox.meta.back().source == ECommandSource::Player);
    CHECK(outbox.meta.back().seq >= 1);
    fixture.drain();

    // 2) Cell-target skill click (the other FActionRequest site): skill.fog aims at a cell, no unit.
    fixture.skills(warrior, {"skill.fog"});
    fixture.refill(warrior);
    hud.selectedSkill = 0;
    input.mousePos = isoView->cellCenter(6, 4);
    input.mouseLeftPressed = true;
    hud.update(fixture.registry, 0.f);
    input.mouseLeftPressed = false;
    CHECK(fixture.queue().pending.size() == 1);
    const auto* cellAction = std::get_if<game::FActionRequest>(&fixture.queue().pending.front());
    CHECK(cellAction != nullptr);
    CHECK(cellAction->skillId == "skill.fog");
    CHECK(cellAction->target == entt::null);
    CHECK((cellAction->cell == game::FGridPosition{6, 4}));
    CHECK(outbox.meta.size() == 1);
    CHECK(outbox.meta.back().source == ECommandSource::Player);
    CHECK(outbox.meta.back().seq >= 2);
    fixture.drain();

    // Keyboard move: the system's private push site now goes through SubmitHudAction. The adjacent enemy
    // leaves first so the step west is free; skill.strike is restored so an empty-cell click stays a move.
    fixture.registry.get<game::FGridPosition>(adjacent) = {10, 11};
    fixture.skills(warrior, {"skill.strike"});
    fixture.refill(warrior);
    hud.selectedSkill = 0;
    input.keysJustPressed[SDL_SCANCODE_W] = true;
    hud.update(fixture.registry, 0.f);
    input.keysJustPressed[SDL_SCANCODE_W] = false;
    CHECK(fixture.queue().pending.size() == 1);
    CHECK(std::holds_alternative<game::FMoveRequest>(fixture.queue().pending.front()));
    CHECK(outbox.meta.size() == 1);
    CHECK(outbox.meta.back().source == ECommandSource::Player);
    CHECK(outbox.meta.back().seq >= 3);
    fixture.drain();

    // Mouse click on an empty reachable cell: the same API, the same provenance.
    input.mousePos = isoView->cellCenter(6, 4);
    input.mouseLeftPressed = true;
    hud.update(fixture.registry, 0.f);
    input.mouseLeftPressed = false;
    CHECK(fixture.queue().pending.size() == 1);
    CHECK(std::holds_alternative<game::FMoveRequest>(fixture.queue().pending.front()));
    CHECK(outbox.meta.size() == 1);
    CHECK(outbox.meta.back().source == ECommandSource::Player);
    CHECK(outbox.meta.back().seq >= 4);
    fixture.drain();

    // The shared helper (the view's target button uses the same one): direct call, same result.
    const bool accepted = game::ecs::SubmitHudAction(
        fixture.registry, game::FMoveRequest{warrior, {5, 6}, *fixture.snapshot().currentTurn});
    CHECK(accepted);
    CHECK(fixture.queue().pending.size() == 1);
    CHECK(outbox.meta.size() == 1);
    CHECK(outbox.meta.back().source == ECommandSource::Player);

    // 3) A refused submit: a resolved battle records the reject code and queues nothing.
    auto& outcome = fixture.registry.ctx().get<game::FBattleOutcome>();
    const std::size_t queued = fixture.queue().pending.size();
    outcome.resolved = true;
    const bool refused = game::ecs::SubmitHudAction(
        fixture.registry, game::FMoveRequest{warrior, {5, 7}, *fixture.snapshot().currentTurn});
    outcome.resolved = false;
    CHECK(!refused);
    CHECK(fixture.queue().pending.size() == queued);
    const auto& results = fixture.registry.ctx().get<game::FCommandResults>();
    CHECK(!results.recent.empty());
    CHECK(results.recent.back().reject == game::ECommandReject::OutcomeResolved);

    // No direct push sites are left in the two HUD files; the view calls the shared helper.
    const std::string systemSource = ReadText("src/ecs/systems/EmberCombatHudSystem.cpp");
    CHECK(!systemSource.empty());
    CHECK(systemSource.find("pending.push_back") == std::string::npos);
    const std::string viewSource = ReadText("src/ecs/systems/EmberCombatHudView.cpp");
    CHECK(!viewSource.empty());
    CHECK(viewSource.find("pending.push_back") == std::string::npos);
    CHECK(viewSource.find("SubmitHudAction") != std::string::npos);
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);  // an assertion must still show how far the suite got
    return game_test::RunTests("combat_info_ui_tests", {
        {"SnapshotTurnOrderMarksSpeculative", SnapshotTurnOrderMarksSpeculative},
        {"LethalIntentsRankFirst", LethalIntentsRankFirst},
        {"OverlapCollapsesToBadge", OverlapCollapsesToBadge},
        {"PreviewMatchesPipeline", PreviewMatchesPipeline},
        {"MovePreviewFollowsMoveAp", MovePreviewFollowsMoveAp},
        {"IntentNumbersRecalcPerFrame", IntentNumbersRecalcPerFrame},
        {"IntentNumberMatchesExecution", IntentNumberMatchesExecution},
        {"IntentNumbersAlwaysOn", IntentNumbersAlwaysOn},
        {"InvalidIntentShowsReason", InvalidIntentShowsReason},
        {"IncomingDamageAggregated", IncomingDamageAggregated},
        {"ReachCellsMatchPathfinder", ReachCellsMatchPathfinder},
        {"SubmitPathSites", SubmitPathSites},
    });
}