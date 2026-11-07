// WP-06 battle-command tests. The command API (FBattleCommand.h), the resolver's drain/rejection
// standardisation and the turn flow's AI submission are exercised three ways:
//   * hand-built registries (loaded content + minimal units): every rejection code, result recording,
//     meta pairing, the session guard and the sheet-refresh guard — exact codes and byte-identical texts;
//   * a real Ember battle (the effect_hooks boot recipe): the AI submission path end to end — source,
//     seq, and the busy-submit retry contract;
//   * source greps: the fixed reject order stays pinned, the PreviewFailed mapping exists even though its
//     branch is defensive (content + stats are validated before it), and the B1 surfaces
//     (FAutoAction, FCombatTypes' pending type) stay untouched.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBaseCombatStats.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FBattleFlow.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace {

using game::ECommandReject;
using game::ECommandSource;
using game::FCommandResult;
using game::FBattleAction;
using game::FBattleCommand;
using game::FTurnToken;

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::filesystem::path SaveDirRoot() {
    if (const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
        overrideDir != nullptr && *overrideDir != '\0') {
        return std::filesystem::path(overrideDir);
    }
    return std::filesystem::current_path() / "out" / "test-saves" / "battle_command_tests";
}

std::filesystem::path MakeCheckpointPath(std::string_view label) {
    const std::filesystem::path root = SaveDirRoot();
    std::filesystem::create_directories(root);
    static int counter = 0;
    return root / ("cmd-" + std::string{label} + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(counter++) + ".json");
}

void RemoveCheckpointFiles(const std::filesystem::path& checkpoint) {
    std::error_code error;
    std::filesystem::remove(checkpoint, error);
    std::filesystem::remove(checkpoint.string() + ".run.json", error);
    std::filesystem::remove(checkpoint.string() + ".run.json.tmp", error);
}

const game::FCombatLog& Log(entt::registry& registry) {
    return registry.ctx().get<game::FCombatLog>();
}

void Drain(entt::registry& registry) {
    game::ecs::ActionResolverSystem resolver;
    resolver.update(registry, 0.f);
}

entt::entity FindUnit(entt::registry& registry, std::string_view id) {
    for (const auto entity : registry.view<game::FUnitData>()) {
        if (registry.get<game::FUnitData>(entity).definition.id == id) {
            return entity;
        }
    }
    return entt::null;
}

std::vector<entt::entity> Enemies(entt::registry& registry) {
    std::vector<entt::entity> enemies;
    for (const auto entity : registry.view<game::FEnemy, game::FUnitData>()) {
        enemies.push_back(entity);
    }
    return enemies;
}

FBattleAction SkillAction(const FTurnToken& token, entt::entity actor, entt::entity target, std::string skillId) {
    return game::FActionRequest{actor, target, std::move(skillId), token, {}};
}

FBattleAction MoveAction(const FTurnToken& token, entt::entity actor, game::FGridPosition destination) {
    return game::FMoveRequest{actor, destination, token};
}

// ---------------------------------------------------------------------------------------------
// Fixture A: hand-built commands on loaded content
// ---------------------------------------------------------------------------------------------

/// Content + the minimal battle contexts on a hand-built 12x12 floor grid. No battle state and no
/// presentation gate (headless = settled). `withRun` adds FDungeonRun (the Ember sheet-refresh guard);
/// `withResults` adds the record contexts (the resolver only FINDS them — pure direct-push setups stay
/// untouched). Default layout: A (party) at (1,1), B (enemy) at (2,1), C (party) at (1,2).
struct FCommandFixture {
    std::filesystem::path checkpoint;
    game_test::FTestRegistry loaded;
    entt::entity actor{entt::null};
    entt::entity foe{entt::null};
    entt::entity ally{entt::null};

    explicit FCommandFixture(std::string_view label, bool withRun = false, bool withResults = true)
        : checkpoint(MakeCheckpointPath(label)),
          loaded(game_test::LoadTestRegistry(TestAssetRoots(), checkpoint.string())) {
        CHECK(loaded.content);
        CHECK(loaded.dungeon);
        auto& r = registry();
        r.ctx().emplace<game::FCombatLog>();
        r.ctx().emplace<game::FActionQueue>();
        r.ctx().emplace<game::FTurnRuntime>();
        r.ctx().emplace<game::FEventBus>();
        r.ctx().emplace<game::FCombatRuntime>();
        r.ctx().emplace<game::FBattleOutcome>();
        r.ctx().emplace<game::FGridMetrics>();
        r.ctx().emplace<game::FGridTerrain>().moveRange = 2;
        if (withRun) {
            r.ctx().emplace<game::FDungeonRun>();
        }
        if (withResults) {
            r.ctx().emplace<game::FCommandResults>();
            r.ctx().emplace<game::FCommandOutbox>();
        }
        actor = MakeUnit("A", 1, 1, true);
        foe = MakeUnit("B", 2, 1, false);
        ally = MakeUnit("C", 1, 2, true);
        r.emplace<game::FTurnParticipant>(actor);
    }

    ~FCommandFixture() { RemoveCheckpointFiles(checkpoint); }

    FCommandFixture(const FCommandFixture&) = delete;
    FCommandFixture& operator=(const FCommandFixture&) = delete;

    entt::registry& registry() { return loaded.registry; }

    entt::entity MakeUnit(std::string_view tag, int x, int y, bool party) {
        auto& r = registry();
        const entt::entity unit = r.create();
        r.emplace<game::FTag>(unit, game::FTag{std::string(tag)});
        r.emplace<game::FStats>(unit, game::FStats{10, 8, 2, 2});
        r.emplace<game::FHealth>(unit, game::FHealth{50, 50});
        r.emplace<game::FGridPosition>(unit, game::FGridPosition{x, y});
        r.emplace<FPosition>(unit, FPosition{0.f, 0.f});
        r.emplace<game::FActionPoints>(unit, game::FActionPoints{2, 2, 5, 5});
        r.emplace<game::FSpeed>(unit, game::FSpeed{10});
        r.emplace<game::FSkillLoadout>(unit, game::FSkillLoadout{{game::FSkillSlot{"skill.strike", {}},
                                                                  game::FSkillSlot{"skill.firebolt", {}},
                                                                  game::FSkillSlot{"skill.arrow", {}}}});
        if (party) {
            r.emplace<game::FPartyMember>(unit);
        } else {
            r.emplace<game::FEnemy>(unit);
        }
        return unit;
    }

    /// Starts the session and returns the token of `unit`, which must be the active participant.
    FTurnToken StartTurn(entt::entity unit) {
        auto& runtime = registry().ctx().get<game::FTurnRuntime>();
        CHECK(runtime.controller.start(registry(), 20261108u).has_value());
        const auto current = runtime.controller.snapshot().currentTurn;
        CHECK(current.has_value());
        CHECK(current->actor == unit);
        return *current;
    }

    FTurnToken StartTurn() { return StartTurn(actor); }

    game::FCommandResults& Results() { return registry().ctx().get<game::FCommandResults>(); }

    /// Direct push (the pre-API path: no meta) + drain; returns the recorded result.
    FCommandResult DrainDirect(FBattleAction action) {
        registry().ctx().get<game::FActionQueue>().pending.push_back(std::move(action));
        Drain(registry());
        CHECK(!Results().recent.empty());
        return Results().recent.back();
    }
};

// ---------------------------------------------------------------------------------------------
// Reject order / texts
// ---------------------------------------------------------------------------------------------

/// P6-4: the check order is canonical. A command that fails several checks reports the FIRST one.
void RejectPriorityIsStable() {
    // Dead beats the unknown skill and the range.
    {
        FCommandFixture fixture{"priority-dead"};
        const auto token = fixture.StartTurn();
        CHECK(game::ApplyDamageToUnit(fixture.registry(), fixture.actor, 9999));
        fixture.registry().get<game::FGridPosition>(fixture.foe) = {8, 1};
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.nope"));
        CHECK(result.reject == ECommandReject::Dead);
        CHECK(result.reason == "Cannot act: knocked out.");
    }
    // Unknown skill beats the dead target and the range.
    {
        FCommandFixture fixture{"priority-skill"};
        const auto token = fixture.StartTurn();
        CHECK(game::ApplyDamageToUnit(fixture.registry(), fixture.foe, 9999));
        fixture.registry().get<game::FGridPosition>(fixture.foe) = {8, 1};
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.nope"));
        CHECK(result.reject == ECommandReject::UnknownSkill);
        CHECK(result.reason == "A: unknown skill.");
    }
    // Wrong faction beats the range.
    {
        FCommandFixture fixture{"priority-faction"};
        const auto token = fixture.StartTurn();
        fixture.registry().get<game::FGridPosition>(fixture.ally) = {8, 1};
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.ally, "skill.strike"));
        CHECK(result.reject == ECommandReject::WrongFaction);
        CHECK(result.reason == "Wrong target faction for this skill.");
    }
    // Line of sight beats the empty AP pool.
    {
        FCommandFixture fixture{"priority-los"};
        const auto token = fixture.StartTurn();
        fixture.registry().get<game::FGridPosition>(fixture.foe) = {3, 1};
        fixture.registry().ctx().get<game::FGridTerrain>().cells[1 * game::kGridWidth + 2] = game::ETile::Wall;
        fixture.registry().get<game::FActionPoints>(fixture.actor).skill = 0;
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.arrow"));
        CHECK(result.reject == ECommandReject::NoLos);
        CHECK(result.reason == "Line of sight blocked by stone.");
    }
}

/// Every non-None code: 15 live scenarios (one per drain code) + PresentationBusy, each with its exact
/// text. PreviewFailed is the one defensive drain branch — PreviewSkill only fails when the content or
/// the caster's FStats is missing, and the resolver validates both before it runs, so no live scenario
/// can reach it; its mapping is pinned by source instead (the grep below).
void EveryRejectHasReason() {
    std::vector<ECommandReject> seen;

    // 1. StaleTurn — a token that is not the current turn's.
    {
        FCommandFixture fixture{"reason-stale"};
        const auto token = fixture.StartTurn();
        auto stale = token;
        ++stale.turnSerial;
        const auto result = fixture.DrainDirect(SkillAction(stale, fixture.actor, fixture.foe, "skill.strike"));
        CHECK(result.reject == ECommandReject::StaleTurn);
        CHECK(result.reason == "Not your turn: stale action rejected.");
        seen.push_back(result.reject);
    }
    // ...and no session at all is the same code and text.
    {
        FCommandFixture fixture{"reason-nosession"};
        FTurnToken token;
        token.actor = fixture.actor;
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
        CHECK(result.reject == ECommandReject::StaleTurn);
        CHECK(result.reason == "Not your turn: stale action rejected.");
    }
    // 2. NotActor — the token matches, but the command claims a different actor than its token.
    {
        FCommandFixture fixture{"reason-notactor"};
        const auto token = fixture.StartTurn();
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.foe, fixture.actor, "skill.strike"));
        CHECK(result.reject == ECommandReject::NotActor);
        CHECK(result.reason == "Not your turn: stale action rejected.");
        seen.push_back(result.reject);
    }
    // 3. Unavailable — both shapes (move guard, missing combat components).
    {
        FCommandFixture fixture{"reason-unavailable"};
        auto& r = fixture.registry();
        r.remove<game::FTurnParticipant>(fixture.actor);
        const entt::entity bare = r.create();
        r.emplace<game::FGridPosition>(bare, game::FGridPosition{5, 5});
        r.emplace<game::FSpeed>(bare, game::FSpeed{10});
        r.emplace<game::FTurnParticipant>(bare);
        const auto token = fixture.StartTurn(bare);
        const auto result = fixture.DrainDirect(MoveAction(token, bare, {6, 5}));
        CHECK(result.reject == ECommandReject::Unavailable);
        CHECK(result.reason == "Cannot move: unit or grid is unavailable.");
        seen.push_back(result.reject);
    }
    {
        FCommandFixture fixture{"reason-unavailable-act"};
        const auto token = fixture.StartTurn();
        fixture.registry().remove<game::FStats>(fixture.actor);
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
        CHECK(result.reject == ECommandReject::Unavailable);
        CHECK(result.reason == "Cannot act: missing combat components.");
    }
    // 4. NoAp — both shapes (movement pool, skill pool with the x/y fragment).
    {
        FCommandFixture fixture{"reason-noap"};
        const auto token = fixture.StartTurn();
        fixture.registry().get<game::FActionPoints>(fixture.actor).move = 0;
        const auto moveResult = fixture.DrainDirect(MoveAction(token, fixture.actor, {1, 3}));
        CHECK(moveResult.reject == ECommandReject::NoAp);
        CHECK(moveResult.reason == "No movement AP remaining.");
        seen.push_back(moveResult.reject);

        fixture.registry().get<game::FActionPoints>(fixture.actor).skill = 0;
        const auto skillResult = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
        CHECK(skillResult.reject == ECommandReject::NoAp);
        CHECK(skillResult.reason == "A: not enough skill AP (0/2)");
    }
    // 5. Blocked — the destination cannot be entered (occupied; no separate Occupied code, P6-4).
    {
        FCommandFixture fixture{"reason-blocked"};
        const auto token = fixture.StartTurn();
        const auto result = fixture.DrainDirect(MoveAction(token, fixture.actor, {2, 1}));
        CHECK(result.reject == ECommandReject::Blocked);
        CHECK(result.reason == "Blocked: wall, occupied cell or movement range.");
        seen.push_back(result.reject);
    }
    // 6. Dead.
    {
        FCommandFixture fixture{"reason-dead"};
        const auto token = fixture.StartTurn();
        CHECK(game::ApplyDamageToUnit(fixture.registry(), fixture.actor, 9999));
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
        CHECK(result.reject == ECommandReject::Dead);
        CHECK(result.reason == "Cannot act: knocked out.");
        seen.push_back(result.reject);
    }
    // 7. UnknownSkill.
    {
        FCommandFixture fixture{"reason-unknown"};
        const auto token = fixture.StartTurn();
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.nope"));
        CHECK(result.reject == ECommandReject::UnknownSkill);
        CHECK(result.reason == "A: unknown skill.");
        seen.push_back(result.reject);
    }
    // 8. SkillResolutionFailed — the slot exists, the content row does not (a wiring bug in production).
    {
        FCommandFixture fixture{"reason-failed"};
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        r.get<game::FSkillLoadout>(fixture.actor).slots.push_back(game::FSkillSlot{"skill.ghost", {}});
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.ghost"));
        CHECK(result.reject == ECommandReject::SkillResolutionFailed);
        CHECK(result.reason == "A: skill resolution failed.");
        seen.push_back(result.reject);
    }
    // 9. BadTarget — both shapes (dead target, self-skill aimed elsewhere).
    {
        FCommandFixture fixture{"reason-badtarget"};
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        CHECK(game::ApplyDamageToUnit(r, fixture.foe, 9999));
        const auto deadTarget = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
        CHECK(deadTarget.reject == ECommandReject::BadTarget);
        CHECK(deadTarget.reason == "A: no living target.");
        seen.push_back(deadTarget.reject);

        // A "self" skill aimed at somebody else: mutating the content row is test-only.
        for (auto& skill : r.ctx().get<game::gameplay::FContentRegistry>().skills.rows) {
            if (skill.id == "skill.strike") {
                skill.target = "self";
            }
        }
        const auto selfMiss = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.ally, "skill.strike"));
        CHECK(selfMiss.reject == ECommandReject::BadTarget);
        CHECK(selfMiss.reason == "This skill can only target its caster.");
    }
    // 10. WrongFaction.
    {
        FCommandFixture fixture{"reason-faction"};
        const auto token = fixture.StartTurn();
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.ally, "skill.strike"));
        CHECK(result.reject == ECommandReject::WrongFaction);
        CHECK(result.reason == "Wrong target faction for this skill.");
        seen.push_back(result.reject);
    }
    // 11. OutsideGrid.
    {
        FCommandFixture fixture{"reason-outside"};
        const auto token = fixture.StartTurn();
        fixture.registry().get<game::FGridPosition>(fixture.foe) = {99, 1};
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
        CHECK(result.reject == ECommandReject::OutsideGrid);
        CHECK(result.reason == "Target is outside the battlefield.");
        seen.push_back(result.reject);
    }
    // 12. OutOfRange.
    {
        FCommandFixture fixture{"reason-range"};
        const auto token = fixture.StartTurn();
        fixture.registry().get<game::FGridPosition>(fixture.foe) = {9, 1};
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.arrow"));
        CHECK(result.reject == ECommandReject::OutOfRange);
        CHECK(result.reason == "A: target out of range.");
        seen.push_back(result.reject);
    }
    // 13. NoLos — in range, but a wall sits between the cells.
    {
        FCommandFixture fixture{"reason-los"};
        const auto token = fixture.StartTurn();
        fixture.registry().get<game::FGridPosition>(fixture.foe) = {3, 1};
        fixture.registry().ctx().get<game::FGridTerrain>().cells[1 * game::kGridWidth + 2] = game::ETile::Wall;
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.arrow"));
        CHECK(result.reject == ECommandReject::NoLos);
        CHECK(result.reason == "Line of sight blocked by stone.");
        seen.push_back(result.reject);
    }
    // 14. PreviewFailed — defensive-only, pinned by source (see the function comment).
    {
        const std::string resolver = ReadText("src/ecs/systems/ActionResolverSystem.h");
        CHECK(resolver.find("ECommandReject::PreviewFailed") != std::string::npos);
        CHECK(resolver.find("\": skill preview failed.\"") != std::string::npos);
    }
    // 15. OutcomeResolved — the drain of a decided battle.
    {
        FCommandFixture fixture{"reason-resolved"};
        const auto token = fixture.StartTurn();
        fixture.registry().ctx().get<game::FBattleOutcome>().resolved = true;
        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
        CHECK(result.reject == ECommandReject::OutcomeResolved);
        CHECK(result.reason == std::string(game::kOutcomeResolvedReason));
        seen.push_back(result.reject);
    }
    // 16. PresentationBusy — the submit path, with its one log line.
    {
        FCommandFixture fixture{"reason-busy"};
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        r.ctx().emplace<game::FPresentationGate>().busy = 1;
        const int linesBefore = static_cast<int>(Log(r).lines.size());
        CHECK(!game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.strike"),
                                                           ECommandSource::Player, 0}));
        const auto& recorded = fixture.Results().recent.back();
        CHECK(recorded.reject == ECommandReject::PresentationBusy);
        CHECK(recorded.reason == std::string(game::kPresentationBusyReason));
        CHECK(static_cast<int>(Log(r).lines.size()) == linesBefore + 1);
        CHECK(Log(r).lines.back() == recorded.reason);
        seen.push_back(recorded.reject);
    }

    // Completeness: all 15 live codes appeared above; PreviewFailed is pinned by the grep.
    const std::vector<ECommandReject> required{
        ECommandReject::StaleTurn, ECommandReject::NotActor, ECommandReject::Unavailable, ECommandReject::NoAp,
        ECommandReject::Blocked, ECommandReject::Dead, ECommandReject::UnknownSkill,
        ECommandReject::SkillResolutionFailed, ECommandReject::BadTarget, ECommandReject::WrongFaction,
        ECommandReject::OutsideGrid, ECommandReject::OutOfRange, ECommandReject::NoLos,
        ECommandReject::OutcomeResolved, ECommandReject::PresentationBusy};
    for (const auto code : required) {
        CHECK(std::ranges::find(seen, code) != seen.end());
    }
    // The drain codes are all mapped in the resolver source: a missing mapping cannot survive the grep.
    const std::string resolver = ReadText("src/ecs/systems/ActionResolverSystem.h");
    for (const char* code : {"ECommandReject::StaleTurn", "ECommandReject::NotActor", "ECommandReject::Unavailable",
                             "ECommandReject::NoAp", "ECommandReject::Blocked", "ECommandReject::Dead",
                             "ECommandReject::UnknownSkill", "ECommandReject::SkillResolutionFailed",
                             "ECommandReject::BadTarget", "ECommandReject::WrongFaction", "ECommandReject::OutsideGrid",
                             "ECommandReject::OutOfRange", "ECommandReject::NoLos", "ECommandReject::PreviewFailed",
                             "ECommandReject::OutcomeResolved"}) {
        CHECK(resolver.find(code) != std::string::npos);
    }
}

// ---------------------------------------------------------------------------------------------
// Recording / submission
// ---------------------------------------------------------------------------------------------

void ResultsAreRecorded() {
    FCommandFixture fixture{"recorded"};
    const auto token = fixture.StartTurn();
    const auto accept = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
    CHECK(accept.reject == ECommandReject::None);
    CHECK(accept.reason.empty());
    CHECK(accept.command.source == ECommandSource::Unknown);  // direct push: no meta
    CHECK(accept.command.seq == 0);

    auto stale = token;
    ++stale.turnSerial;
    const auto reject = fixture.DrainDirect(SkillAction(stale, fixture.actor, fixture.foe, "skill.strike"));
    CHECK(reject.reject == ECommandReject::StaleTurn);

    const auto& recent = fixture.Results().recent;
    CHECK(recent.size() == 2);
    CHECK(recent[0].reject == ECommandReject::None);
    CHECK(recent[1].reject == ECommandReject::StaleTurn);
    CHECK(recent[0].command.seq == 0 && recent[1].command.seq == 0);
}

void SubmitRejectedWhileSettling() {
    FCommandFixture fixture{"submit-busy"};
    auto& r = fixture.registry();
    const auto token = fixture.StartTurn();
    r.ctx().emplace<game::FPresentationGate>().busy = 1;
    const int linesBefore = static_cast<int>(Log(r).lines.size());
    const FBattleCommand command{SkillAction(token, fixture.actor, fixture.foe, "skill.strike"),
                                 ECommandSource::Player, 0};
    CHECK(!game::SubmitBattleCommand(r, command));
    CHECK(r.ctx().get<game::FActionQueue>().pending.empty());
    CHECK(r.ctx().get<game::FCommandOutbox>().meta.empty());
    const auto& recent = fixture.Results().recent;
    CHECK(recent.size() == 1);
    CHECK(recent.back().reject == ECommandReject::PresentationBusy);
    CHECK(recent.back().command.seq == 0);  // a rejected submit issues no seq
    CHECK(recent.back().reason == std::string(game::kPresentationBusyReason));
    CHECK(static_cast<int>(Log(r).lines.size()) == linesBefore + 1);
    CHECK(Log(r).lines.back() == std::string(game::kPresentationBusyReason));

    r.ctx().get<game::FPresentationGate>().busy = 0;
    CHECK(game::SubmitBattleCommand(r, command));
    CHECK(r.ctx().get<game::FActionQueue>().pending.size() == 1);
    const auto& meta = r.ctx().get<game::FCommandOutbox>().meta;
    CHECK(meta.size() == 1 && meta.front().source == ECommandSource::Player && meta.front().seq == 1);
}

void SubmitRejectedAfterOutcome() {
    FCommandFixture fixture{"submit-resolved"};
    auto& r = fixture.registry();
    const auto token = fixture.StartTurn();
    r.ctx().get<game::FBattleOutcome>().resolved = true;
    const int linesBefore = static_cast<int>(Log(r).lines.size());
    CHECK(!game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.strike"),
                                                       ECommandSource::Player, 0}));
    CHECK(r.ctx().get<game::FActionQueue>().pending.empty());
    const auto& recent = fixture.Results().recent;
    CHECK(recent.size() == 1);
    CHECK(recent.back().reject == ECommandReject::OutcomeResolved);
    CHECK(recent.back().command.seq == 0);
    CHECK(recent.back().reason == std::string(game::kOutcomeResolvedReason));
    CHECK(static_cast<int>(Log(r).lines.size()) == linesBefore);  // recorded, not narrated
}

void HeadlessSubmissionSucceeds() {
    // No gate (settled by definition), no record contexts (the API creates them) — the headless path.
    FCommandFixture fixture{"headless", false, false};
    auto& r = fixture.registry();
    const auto token = fixture.StartTurn();
    CHECK(!r.ctx().contains<game::FCommandResults>());
    CHECK(!r.ctx().contains<game::FCommandOutbox>());
    CHECK(!r.ctx().contains<game::FPresentationGate>());
    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.strike"),
                                                      ECommandSource::Player, 0}));
    CHECK(r.ctx().get<game::FActionQueue>().pending.size() == 1);
    CHECK(r.ctx().get<game::FCommandOutbox>().meta.size() == 1);
    const auto& results = r.ctx().get<game::FCommandResults>();
    CHECK(results.sessionId == token.sessionId && results.nextSeq == 2);

    // The drain records the acceptance through the same session guard.
    Drain(r);
    CHECK(results.recent.size() == 1);
    CHECK(results.recent.front().reject == ECommandReject::None);
    CHECK(results.recent.front().command.source == ECommandSource::Player);
    CHECK(results.recent.front().command.seq == 1);
}

void MixedPushBatchIsUnknown() {
    FCommandFixture fixture{"mixed"};
    auto& r = fixture.registry();
    const auto token = fixture.StartTurn();
    // One direct push (no meta), then one API submit: the batch no longer matches the FIFO 1:1, so the
    // whole batch must be Unknown (P6-1) instead of guessing half the provenance by index.
    r.ctx().get<game::FActionQueue>().pending.push_back(
        SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.firebolt"),
                                                      ECommandSource::Player, 0}));
    CHECK(r.ctx().get<game::FCommandOutbox>().meta.size() == 1);

    Drain(r);
    const auto& recent = fixture.Results().recent;
    CHECK(recent.size() == 2);
    CHECK(recent[0].reject == ECommandReject::None && recent[1].reject == ECommandReject::None);
    CHECK(recent[0].command.source == ECommandSource::Unknown && recent[0].command.seq == 0);
    CHECK(recent[1].command.source == ECommandSource::Unknown && recent[1].command.seq == 0);
    CHECK(r.ctx().get<game::FCommandOutbox>().meta.empty());
}

void ApiBatchKeepsSource() {
    FCommandFixture fixture{"api-source"};
    auto& r = fixture.registry();
    const auto token = fixture.StartTurn();
    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.strike"),
                                                      ECommandSource::Player, 0}));
    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.firebolt"),
                                                      ECommandSource::Test, 0}));
    Drain(r);
    const auto& recent = fixture.Results().recent;
    CHECK(recent.size() == 2);
    CHECK(recent[0].reject == ECommandReject::None && recent[1].reject == ECommandReject::None);
    CHECK(recent[0].command.source == ECommandSource::Player && recent[0].command.seq == 1);
    CHECK(recent[1].command.source == ECommandSource::Test && recent[1].command.seq == 2);
}

void CommandSeqIsMonotonic() {
    FCommandFixture fixture{"seq"};
    auto& r = fixture.registry();
    const auto token = fixture.StartTurn();
    auto& outbox = r.ctx().get<game::FCommandOutbox>();

    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.strike"),
                                                      ECommandSource::Player, 0}));
    CHECK(outbox.meta.size() == 1 && outbox.meta.back().seq == 1);
    // A rejected submit consumes no seq (0 is not an issued number).
    r.ctx().emplace<game::FPresentationGate>().busy = 1;
    CHECK(!game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.firebolt"),
                                                       ECommandSource::Player, 0}));
    CHECK(fixture.Results().recent.back().reject == ECommandReject::PresentationBusy);
    CHECK(fixture.Results().recent.back().command.seq == 0);
    r.ctx().get<game::FPresentationGate>().busy = 0;
    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.arrow"),
                                                      ECommandSource::Player, 0}));
    CHECK(outbox.meta.size() == 2 && outbox.meta[0].seq == 1 && outbox.meta[1].seq == 2);

    // A new battle session restarts the sequence and drops the stale meta with the old record.
    auto nextSession = token;
    ++nextSession.sessionId;
    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(nextSession, fixture.actor, fixture.foe, "skill.strike"),
                                                      ECommandSource::Player, 0}));
    CHECK(outbox.meta.size() == 1 && outbox.meta.front().seq == 1);
}

void NewBattleClearsCommandResults() {
    FCommandFixture fixture{"session-new"};
    auto& r = fixture.registry();
    const auto token = fixture.StartTurn();
    (void)fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
    auto& results = fixture.Results();
    CHECK(results.recent.size() == 1);
    CHECK(results.sessionId == token.sessionId);

    auto nextSession = token;
    ++nextSession.sessionId;
    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(nextSession, fixture.actor, fixture.foe, "skill.strike"),
                                                      ECommandSource::Player, 0}));
    CHECK(results.recent.empty());  // the previous battle's record died with its session
    CHECK(results.sessionId == nextSession.sessionId);
    CHECK(results.nextSeq == 2);    // the new session issued seq 1
}

void SameBattleKeepsResults() {
    FCommandFixture fixture{"session-same"};
    const auto token = fixture.StartTurn();
    (void)fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
    (void)fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.firebolt"));
    const auto& results = fixture.Results();
    CHECK(results.recent.size() == 2);
    CHECK(results.sessionId == token.sessionId);
    CHECK(results.recent[0].reject == ECommandReject::None && results.recent[1].reject == ECommandReject::None);
}

void ApiPathEqualsTestPath() {
    FCommandFixture fixture{"api-vs-test"};
    auto& r = fixture.registry();
    const auto token = fixture.StartTurn();
    // The same illegal command through both entrances: same code, same text; only the recorded
    // provenance differs (Player + issued seq vs Unknown + 0).
    CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.ally, "skill.strike"),
                                                      ECommandSource::Player, 0}));
    Drain(r);
    auto& results = fixture.Results();
    CHECK(results.recent.size() == 1);
    const FCommandResult viaApi = results.recent.back();

    const FCommandResult viaTest = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.ally, "skill.strike"));
    CHECK(results.recent.size() == 2);
    CHECK(viaApi.reject == ECommandReject::WrongFaction);
    CHECK(viaTest.reject == viaApi.reject);
    CHECK(viaTest.reason == viaApi.reason);
    CHECK(viaTest.reason == "Wrong target faction for this skill.");
    CHECK(viaApi.command.source == ECommandSource::Player && viaApi.command.seq == 1);
    CHECK(viaTest.command.source == ECommandSource::Unknown && viaTest.command.seq == 0);
}

// ---------------------------------------------------------------------------------------------
// Clock identity / B1 surfaces
// ---------------------------------------------------------------------------------------------

template<typename T>
concept HasSessionIdField = requires(T value) { value.sessionId; };
template<typename T>
concept HasRoundNumberField = requires(T value) { value.roundNumber; };
template<typename T>
concept HasTurnSerialField = requires(T value) { value.turnSerial; };
template<typename T>
concept HasCommandSeqField = requires(T value) { value.seq; };
template<typename T>
concept HasCommandActionField = requires(T value) { value.action; };

void NoDuplicateClockField() {
    // Positive controls: the probes find a real token clock and the command's real fields.
    static_assert(HasSessionIdField<FTurnToken>);
    static_assert(HasRoundNumberField<FTurnToken>);
    static_assert(HasTurnSerialField<FTurnToken>);
    static_assert(HasCommandSeqField<FBattleCommand>);
    static_assert(HasCommandActionField<FBattleCommand>);
    // P6-2: no second clock on the command or the meta — the identity is the action token.
    static_assert(!HasSessionIdField<FBattleCommand>);
    static_assert(!HasRoundNumberField<FBattleCommand>);
    static_assert(!HasTurnSerialField<FBattleCommand>);
    static_assert(!HasSessionIdField<game::FCommandMeta>);
    static_assert(!HasRoundNumberField<game::FCommandMeta>);
    static_assert(!HasTurnSerialField<game::FCommandMeta>);

    // The token IS the session source: both action shapes read it through the same helper.
    FCommandFixture fixture{"clock"};
    const auto token = fixture.StartTurn();
    CHECK(game::CommandSessionId(MoveAction(token, fixture.actor, {1, 3})) == token.sessionId);
    CHECK(game::CommandSessionId(SkillAction(token, fixture.actor, fixture.foe, "skill.strike")) == token.sessionId);

    const std::string header = ReadText("src/gameplay/combat/FBattleCommand.h");
    CHECK(header.find("roundNumber") == std::string::npos);
    CHECK(header.find("turnSerial") == std::string::npos);
    CHECK(header.find("frameIndex") == std::string::npos);
}

void DirectPushSurfacesStayUntouched() {
    // B1: FActionQueue::pending keeps its type; FCombatTypes.h is not part of this WP's edits.
    static_assert(std::is_same_v<decltype(game::FActionQueue{}.pending), std::vector<FBattleAction>>);
    CHECK(ReadText("src/gameplay/combat/FCombatTypes.h").find("FBattleCommand") == std::string::npos);

    // FAutoAction's contract is unchanged (the caller composes the command, the decision code knows
    // nothing about the API). The HUD's direct pushes are deliberately NOT pinned here: they are a
    // transitional state that WP-12 replaces, and the build already proves they still compile.
    CHECK(ReadText("src/gameplay/combat/FAutoAction.h").find("FBattleCommand") == std::string::npos);
    CHECK(ReadText("src/gameplay/combat/FAutoAction.cpp").find("FBattleCommand") == std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// Real battle: AI path, sheet guard, drain
// ---------------------------------------------------------------------------------------------

/// The real Ember battlefield (the effect_hooks boot recipe): content, scene, units with brains and the
/// battle contexts — everything the AI submission path touches. No battle state, no gate (headless).
struct FBattleTest {
    entt::registry registry;
    std::unique_ptr<game::FScene> scene;
    std::filesystem::path checkpoint;
    std::filesystem::path runSave;

    FBattleTest() {
        checkpoint = MakeCheckpointPath("battle");
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        game::StartDungeonRun(registry, 1234u, true);

        const auto document = game::BuildDungeonBattlefield(registry);
        scene = std::make_unique<game::FScene>(registry, "BattleCommandTest");
        scene->Load();
        const auto battlefield = game::FEntityFactory::CreateBattlefield(*scene, document);
        CHECK(battlefield.has_value());
        registry.ctx().erase<game::FGridTerrain>();
        registry.ctx().emplace<game::FGridTerrain>(battlefield->terrain);
        if (auto* metrics = registry.ctx().find<game::FGridMetrics>()) {
            *metrics = battlefield->metrics;
        } else {
            registry.ctx().emplace<game::FGridMetrics>(battlefield->metrics);
        }
        const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
        CHECK(game::SetupCombatUnits(registry, *battlefield, content));
        registry.ctx().emplace<game::FActionQueue>();
        registry.ctx().emplace<game::FCombatLog>();
        registry.ctx().emplace<game::FTurnRuntime>();
        registry.ctx().emplace<game::FCombatRuntime>();
        registry.ctx().emplace<game::FEventBus>();
        registry.ctx().emplace<game::FBattleOutcome>();
        game::ApplyRunParty(registry);
    }

    ~FBattleTest() {
        scene.reset();
        RemoveCheckpointFiles(checkpoint);
        std::error_code error;
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave.string() + ".tmp", error);
    }

    FBattleTest(const FBattleTest&) = delete;
    FBattleTest& operator=(const FBattleTest&) = delete;

    /// Deterministic placement regardless of the generated room (the effect_hooks recipe).
    void ParkAll() {
        registry.ctx().get<game::FGridTerrain>().cells.fill(game::ETile::Floor);
        int slot = 0;
        for (const auto entity : registry.view<game::FPartyMember, game::FGridPosition>()) {
            registry.get<game::FGridPosition>(entity) = {slot++, 11};
        }
        slot = 0;
        for (const auto entity : registry.view<game::FEnemy, game::FGridPosition>()) {
            registry.get<game::FGridPosition>(entity) = {slot++, 10};
        }
    }

    /// Places one enemy next to the warrior so its brain has a melee target, makes it the first actor and
    /// starts the session. Returns the enemy.
    entt::entity StartEnemyTurn() {
        ParkAll();
        const auto enemy = Enemies(registry).front();
        const auto warrior = FindUnit(registry, "party.warrior");
        CHECK(enemy != entt::null && warrior != entt::null);
        registry.get<game::FGridPosition>(enemy) = {2, 1};
        registry.get<game::FGridPosition>(warrior) = {1, 1};
        registry.get<game::FSpeed>(enemy).value = 500;
        auto& runtime = registry.ctx().get<game::FTurnRuntime>();
        CHECK(runtime.controller.start(registry, 99u).has_value());
        CHECK(runtime.controller.snapshot().currentTurn->actor == enemy);
        return enemy;
    }
};

void AiSubmissionCarriesSource() {
    FBattleTest fixture;
    auto& registry = fixture.registry;
    fixture.StartEnemyTurn();
    registry.ctx().get<game::FCombatRuntime>().autoPlay = true;  // bypass the think delay

    game::ecs::TurnFlowSystem flow;
    flow.update(registry, 0.f);
    CHECK(registry.ctx().get<game::FCombatRuntime>().autoActionsThisTurn == 1);
    CHECK(registry.ctx().get<game::FActionQueue>().pending.size() == 1);
    const auto& meta = registry.ctx().get<game::FCommandOutbox>().meta;
    CHECK(meta.size() == 1);
    CHECK(meta.front().source == ECommandSource::Ai && meta.front().seq == 1);

    // The drain records the acceptance for the same command: source Ai, seq 1, no reject.
    Drain(registry);
    const auto& recent = registry.ctx().get<game::FCommandResults>().recent;
    CHECK(recent.size() == 1);
    CHECK(recent.back().reject == ECommandReject::None);
    CHECK(recent.back().reason.empty());
    CHECK(recent.back().command.source == ECommandSource::Ai);
    CHECK(recent.back().command.seq == 1);
}

void AiRetriesAfterBusySubmit() {
    // Variant A — auto-play bypasses the think delay; an OnTurnStart listener closes the gate mid-frame
    // (the WP-05b guard already ran at the top of the update), so the submit really sees it busy.
    {
        FBattleTest fixture;
        auto& registry = fixture.registry;
        fixture.StartEnemyTurn();
        auto& combat = registry.ctx().get<game::FCombatRuntime>();
        combat.autoPlay = true;

        int busySets = 0;
        game::RegisterHookListener(registry, game::gameplay::EHookEvent::OnTurnStart,
            [&busySets](entt::registry& r, const game::FHookContext&) {
                if (busySets++ > 0) {
                    return;
                }
                r.ctx().emplace<game::FPresentationGate>().busy = 1;
            });

        game::ecs::TurnFlowSystem flow;
        flow.update(registry, 0.f);  // frame 1: hooks close the gate, the submit is refused
        CHECK(busySets == 1);
        CHECK(combat.autoActionsThisTurn == 0);                        // no cap consumption
        CHECK(registry.ctx().get<game::FTurnRuntime>().requests.empty());  // no FEndTurn either
        CHECK(registry.ctx().get<game::FActionQueue>().pending.empty());
        CHECK(combat.thinkDelay == 0.f);                               // retry immediately next frame
        const auto& recent = registry.ctx().get<game::FCommandResults>().recent;
        CHECK(recent.size() == 1);
        CHECK(recent.back().reject == ECommandReject::PresentationBusy);
        CHECK(recent.back().command.source == ECommandSource::Ai);

        registry.ctx().get<game::FPresentationGate>().busy = 0;
        flow.update(registry, 0.016f);  // frame 2: the same plan goes through
        CHECK(combat.autoActionsThisTurn == 1);
        CHECK(registry.ctx().get<game::FActionQueue>().pending.size() == 1);
        const auto& meta = registry.ctx().get<game::FCommandOutbox>().meta;
        CHECK(meta.size() == 1 && meta.front().source == ECommandSource::Ai);
        CHECK(registry.ctx().get<game::FTurnRuntime>().requests.empty());
    }

    // Variant B — a non-autoplay enemy with deltaTime >= 0.45 (a real think pause): the refused submit
    // restores thinkDelay to 0, so the retry happens on the very next frame, not another 0.45s later.
    {
        FBattleTest fixture;
        auto& registry = fixture.registry;
        fixture.StartEnemyTurn();
        auto& combat = registry.ctx().get<game::FCombatRuntime>();
        CHECK(!combat.autoPlay);

        int busySets = 0;
        game::RegisterHookListener(registry, game::gameplay::EHookEvent::OnTurnStart,
            [&busySets](entt::registry& r, const game::FHookContext&) {
                if (busySets++ > 0) {
                    return;
                }
                r.ctx().emplace<game::FPresentationGate>().busy = 1;
            });

        game::ecs::TurnFlowSystem flow;
        flow.update(registry, 0.5f);  // frame 1: the pause elapses, hooks close the gate, submit refused
        CHECK(busySets == 1);
        CHECK(combat.autoActionsThisTurn == 0);
        CHECK(combat.thinkDelay == 0.f);
        CHECK(registry.ctx().get<game::FActionQueue>().pending.empty());

        registry.ctx().get<game::FPresentationGate>().busy = 0;
        flow.update(registry, 0.016f);  // frame 2: retry succeeds right away
        CHECK(combat.autoActionsThisTurn == 1);
        CHECK(registry.ctx().get<game::FActionQueue>().pending.size() == 1);
        const auto& meta = registry.ctx().get<game::FCommandOutbox>().meta;
        CHECK(meta.size() == 1 && meta.front().source == ECommandSource::Ai);
    }
}

void DrainWritesNoCombatLogLine() {
    // An API-accepted command still pending when the battle resolves: the drain consumes it as
    // OutcomeResolved, writes no FCombatLog line, and leaves no queue behind (P6-3).
    {
        FCommandFixture fixture{"drain"};
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.strike"),
                                                          ECommandSource::Player, 0}));
        CHECK(r.ctx().get<game::FActionQueue>().pending.size() == 1);
        const auto linesBefore = Log(r).lines.size();
        r.ctx().get<game::FBattleOutcome>().resolved = true;

        Drain(r);

        CHECK(r.ctx().get<game::FActionQueue>().pending.empty());
        CHECK(Log(r).lines.size() == linesBefore);
        CHECK(r.ctx().get<game::FCommandOutbox>().meta.empty());
        const auto& recent = fixture.Results().recent;
        CHECK(recent.size() == 1);
        CHECK(recent.back().reject == ECommandReject::OutcomeResolved);
        CHECK(recent.back().reason == std::string(game::kOutcomeResolvedReason));
        CHECK(recent.back().command.source == ECommandSource::Player);  // the pairing survived the drain
        CHECK(recent.back().command.seq == 1);
    }
    // Direct pushes drain the same way (Unknown provenance), still without a log line.
    {
        FCommandFixture fixture{"drain-direct"};
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        r.ctx().get<game::FActionQueue>().pending.push_back(
            SkillAction(token, fixture.actor, fixture.foe, "skill.strike"));
        r.ctx().get<game::FBattleOutcome>().resolved = true;
        const auto linesBefore = Log(r).lines.size();

        Drain(r);

        CHECK(r.ctx().get<game::FActionQueue>().pending.empty());
        CHECK(Log(r).lines.size() == linesBefore);
        CHECK(fixture.Results().recent.back().reject == ECommandReject::OutcomeResolved);
        CHECK(fixture.Results().recent.back().command.source == ECommandSource::Unknown);
    }
    // Meta without a batch (the queue was emptied elsewhere) is stale provenance: cleared, not kept.
    {
        FCommandFixture fixture{"drain-stale-meta"};
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        CHECK(game::SubmitBattleCommand(r, FBattleCommand{SkillAction(token, fixture.actor, fixture.foe, "skill.strike"),
                                                          ECommandSource::Player, 0}));
        CHECK(r.ctx().get<game::FCommandOutbox>().meta.size() == 1);
        r.ctx().get<game::FActionQueue>().pending.clear();

        Drain(r);

        CHECK(r.ctx().get<game::FCommandOutbox>().meta.empty());
        CHECK(fixture.Results().recent.empty());
    }
}

void SheetRefreshOnlyInEmber() {
    // ① The resolver's status application refreshes through the Ember guard. With a run in context the
    // sheet is recomputed from FBaseCombatStats; without it (forest) the tampered values must survive.
    {
        FCommandFixture fixture{"sheet-run", true};
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        r.emplace<game::FActiveStatuses>(fixture.foe);
        r.emplace<game::FBaseCombatStats>(fixture.foe, game::FBaseCombatStats{game::FStats{5, 5, 2, 2}, 60, 42});
        r.get<game::FStats>(fixture.foe).attack = 777;
        r.get<game::FSpeed>(fixture.foe).value = 777;

        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.firebolt"));
        CHECK(result.reject == ECommandReject::None);
        const auto& statuses = r.get<game::FActiveStatuses>(fixture.foe);
        CHECK(statuses.effects.size() == 1 && statuses.effects.front().statusId == "status.burn");
        CHECK(r.get<game::FSpeed>(fixture.foe).value == 42);   // refreshed from base
        CHECK(r.get<game::FStats>(fixture.foe).attack == 5);
    }
    {
        FCommandFixture fixture{"sheet-forest"};  // no FDungeonRun
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        r.emplace<game::FActiveStatuses>(fixture.foe);
        r.emplace<game::FBaseCombatStats>(fixture.foe, game::FBaseCombatStats{game::FStats{5, 5, 2, 2}, 60, 42});
        r.get<game::FStats>(fixture.foe).attack = 777;
        r.get<game::FSpeed>(fixture.foe).value = 777;

        const auto result = fixture.DrainDirect(SkillAction(token, fixture.actor, fixture.foe, "skill.firebolt"));
        CHECK(result.reject == ECommandReject::None);
        const auto& statuses = r.get<game::FActiveStatuses>(fixture.foe);
        CHECK(statuses.effects.size() == 1 && statuses.effects.front().statusId == "status.burn");
        CHECK(r.get<game::FSpeed>(fixture.foe).value == 777);  // no run: the sheet was not touched
        CHECK(r.get<game::FStats>(fixture.foe).attack == 777);
    }

    // ② The turn-start tick refreshes the actor the same way, and equally only in Ember.
    {
        FCommandFixture fixture{"tick-run", true};
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        r.emplace<game::FBaseCombatStats>(fixture.actor, game::FBaseCombatStats{game::FStats{10, 8, 2, 2}, 50, 30});
        r.get<game::FStats>(fixture.actor).attack = 777;
        r.get<game::FSpeed>(fixture.actor).value = 777;
        auto& statuses = r.emplace<game::FActiveStatuses>(fixture.actor);
        statuses.effects.push_back(game::FStatusEffect{"status.burn", 3, 5});

        game::ecs::TurnFlowSystem flow;
        flow.update(r, 0.f);

        CHECK(!statuses.effects.empty() && statuses.effects.front().turnsRemaining == 2);  // the tick ran
        CHECK(r.get<game::FSpeed>(fixture.actor).value == 30);
        CHECK(r.get<game::FStats>(fixture.actor).attack == 10);
    }
    {
        FCommandFixture fixture{"tick-forest"};  // no FDungeonRun
        auto& r = fixture.registry();
        const auto token = fixture.StartTurn();
        r.emplace<game::FBaseCombatStats>(fixture.actor, game::FBaseCombatStats{game::FStats{10, 8, 2, 2}, 50, 30});
        r.get<game::FStats>(fixture.actor).attack = 777;
        r.get<game::FSpeed>(fixture.actor).value = 777;
        auto& statuses = r.emplace<game::FActiveStatuses>(fixture.actor);
        statuses.effects.push_back(game::FStatusEffect{"status.burn", 3, 5});

        game::ecs::TurnFlowSystem flow;
        flow.update(r, 0.f);

        CHECK(!statuses.effects.empty() && statuses.effects.front().turnsRemaining == 2);  // the tick ran
        CHECK(r.get<game::FSpeed>(fixture.actor).value == 777);  // left as-is
        CHECK(r.get<game::FStats>(fixture.actor).attack == 777);
    }
}

} // namespace

int main() {
    // Save safety (the effect_hooks contract): ctest exports no GAME01P_SAVE_DIR for feature tests, so
    // outside build-test.ps1 a plain run would let run bookkeeping touch — and a defeat delete — the
    // user's real save. Commit an override before any dungeon service can touch disk.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "battle_command_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("battle command", {
        {"RejectPriorityIsStable", RejectPriorityIsStable},
        {"EveryRejectHasReason", EveryRejectHasReason},
        {"ResultsAreRecorded", ResultsAreRecorded},
        {"SubmitRejectedWhileSettling", SubmitRejectedWhileSettling},
        {"SubmitRejectedAfterOutcome", SubmitRejectedAfterOutcome},
        {"HeadlessSubmissionSucceeds", HeadlessSubmissionSucceeds},
        {"MixedPushBatchIsUnknown", MixedPushBatchIsUnknown},
        {"ApiBatchKeepsSource", ApiBatchKeepsSource},
        {"CommandSeqIsMonotonic", CommandSeqIsMonotonic},
        {"NewBattleClearsCommandResults", NewBattleClearsCommandResults},
        {"SameBattleKeepsResults", SameBattleKeepsResults},
        {"ApiPathEqualsTestPath", ApiPathEqualsTestPath},
        {"NoDuplicateClockField", NoDuplicateClockField},
        {"DirectPushSurfacesStayUntouched", DirectPushSurfacesStayUntouched},
        {"AiSubmissionCarriesSource", AiSubmissionCarriesSource},
        {"AiRetriesAfterBusySubmit", AiRetriesAfterBusySubmit},
        {"DrainWritesNoCombatLogLine", DrainWritesNoCombatLogLine},
        {"SheetRefreshOnlyInEmber", SheetRefreshOnlyInEmber},
    });
}
