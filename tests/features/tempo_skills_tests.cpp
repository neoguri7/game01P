// WP-16 action-economy tests: the tempo effects (push/pull displacement, chain bounce), the move_lock
// status kind, the D-1 skill_ap_cap gate, and the two report metrics (tempo usage; round count before/after
// the 1-AP skills).
//
// Structure:
//   * hand-built battles on loaded fixture content — the displacement, chain, presentation-order and 1-AP
//     tests run the real ActionResolverSystem with direct-push commands (the battle_command_tests recipe);
//   * rule-level tests — move_lock ticks and AI value, on hand-built rows and on the frozen fixture rows;
//   * the boot validation tests — chain/push validators through ValidateEffectContent, the AP-cap gate with
//     a temporary dungeon document;
//   * two auto-battle report tests (3 seeds x depth 3) that print their numbers and never gate on them
//     (R3-review #1: 보고 지표는 pass/fail 아님).

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
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
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiQueries.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/ai/FAiWorld.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/combat/FSkillApCap.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "states/FEmberBattleState.h"

namespace {

using game::ECommandReject;
using game::FCommandResult;
using game::FTurnToken;
using game::gameplay::FStatusContent;

// ---------------------------------------------------------------------------------------------
// Filesystem / data helpers
// ---------------------------------------------------------------------------------------------

/// The save dir the command fixtures write checkpoints into — always the override build-test.ps1 sets,
/// never the user's real save (WP-00b). main() commits an override for plain runs.
std::filesystem::path SaveDirRoot() {
    if (const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
        overrideDir != nullptr && *overrideDir != '\0') {
        return std::filesystem::path(overrideDir);
    }
    return std::filesystem::current_path() / "out" / "test-saves" / "tempo_skills_tests";
}

std::filesystem::path MakeCheckpointPath(std::string_view label) {
    const std::filesystem::path root = SaveDirRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    static int counter = 0;
    return root / ("tempo-" + std::string{label} + "-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                   std::to_string(counter++) + ".json");
}

void RemoveCheckpointFiles(const std::filesystem::path& checkpoint) {
    std::error_code error;
    std::filesystem::remove(checkpoint, error);
    std::filesystem::remove(checkpoint.string() + ".run.json", error);
    std::filesystem::remove(checkpoint.string() + ".run.json.tmp", error);
}

/// A unique throwaway asset root with a data/ subdirectory, removed when it goes out of scope.
struct FTempRoot {
    std::filesystem::path root;

    explicit FTempRoot(std::string_view label) {
        static int counter = 0;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("game01p-wp16-" + std::string{label} + "-" + std::to_string(ticks) + "-" + std::to_string(counter++));
        std::error_code error;
        std::filesystem::remove_all(root, error);
        CHECK(std::filesystem::create_directories(root / "data"));
    }

    ~FTempRoot() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    FTempRoot(const FTempRoot&) = delete;
    FTempRoot& operator=(const FTempRoot&) = delete;
};

nlohmann::json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    CHECK(file.good());
    nlohmann::json document;
    file >> document;
    return document;
}

void WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& document) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << document.dump(2);
    file.flush();
    CHECK(file.good());
}

[[nodiscard]] nlohmann::json FixtureDungeon() { return ReadJsonFile(FixtureDataPath("dungeon.json")); }

/// The temp root first, then the frozen fixture and the shipped assets (per-file fallback).
[[nodiscard]] std::vector<std::string> RootsWith(const FTempRoot& temp) {
    std::vector<std::string> roots{temp.root.string()};
    const std::vector<std::string> base = TestAssetRoots();
    roots.insert(roots.end(), base.begin(), base.end());
    return roots;
}

// ---------------------------------------------------------------------------------------------
// Content / rule helpers
// ---------------------------------------------------------------------------------------------

[[nodiscard]] game::gameplay::FContentRegistry FixtureContent() {
    const game::FAssetManager assets{TestAssetRoots()};
    std::optional<game::gameplay::FContentRegistry> content = game::gameplay::FContentRegistry::load(assets);
    CHECK(content.has_value());
    return std::move(*content);
}

/// A hand-built move_lock row with the authored shape: kind `move_lock`, magnitude 0, duration 1.
[[nodiscard]] FStatusContent MoveLockStatus() {
    FStatusContent status;
    status.id = "status.locked_move";
    status.displayName = "Shackled";
    status.kind = game::gameplay::kStatusKindMoveLock;
    status.magnitude = 0;
    status.durationTurns = 1;
    return status;
}

[[nodiscard]] game::gameplay::FSkillContent FakeThreeApSkill() {
    game::gameplay::FSkillContent skill;
    skill.id = "skill.test3";
    skill.displayName = "Test Three";
    skill.apCost = 3;
    skill.power = 1;
    skill.range = 1;
    skill.shape = "single";
    skill.target = "enemy";
    return skill;
}

game::gameplay::FSkillContent* FindSkillRow(entt::registry& registry, std::string_view id) {
    auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    for (game::gameplay::FSkillContent& skill : content.skills.rows) {
        if (skill.id == id) {
            return &skill;
        }
    }
    return nullptr;
}

/// A hand-built typed hook exactly as DecodeHooks would produce it (provenance stamped explicitly).
[[nodiscard]] game::gameplay::FHook OnHitHook(std::string id, std::string kind, int amount, int distance) {
    game::gameplay::FHook hook;
    hook.event = game::gameplay::EHookEvent::OnHit;
    hook.eventName = game::gameplay::HookEventName(hook.event);
    hook.effect.kind = std::move(kind);
    hook.effect.amount = amount;
    hook.effect.distance = distance;
    hook.effect.from = "caster";
    hook.source = "test:" + id;
    hook.id = std::move(id);
    return hook;
}

/// A `log` probe hook: if the pipeline re-entered FireHooks with its owner, the text would show up.
[[nodiscard]] game::gameplay::FHook LogProbeHook(std::string id, std::string text) {
    game::gameplay::FHook hook = OnHitHook(std::move(id), "log", 0, 0);
    hook.effect.text = std::move(text);
    return hook;
}

/// Hand-built active hooks (bypasses BuildActiveHooks — the lazy build only runs when the component is
/// absent, so a pre-built list is exactly what FireHooks dispatches; effect_hooks_tests recipe).
void GiveHooks(
    entt::registry& registry, entt::entity owner, std::vector<game::gameplay::FHook> hooks,
    std::string slotSkillId) {
    game::FActiveHooks active;
    active.hooks.reserve(hooks.size());
    for (game::gameplay::FHook& hook : hooks) {
        active.hooks.push_back(game::FActiveHook{std::move(hook), slotSkillId});
    }
    registry.emplace_or_replace<game::FActiveHooks>(owner, std::move(active));
}

const game::FCombatLog& Log(entt::registry& registry) { return registry.ctx().get<game::FCombatLog>(); }

[[nodiscard]] int CountLines(const std::vector<std::string>& lines, std::string_view needle) {
    return static_cast<int>(std::ranges::count_if(
        lines, [&](const std::string& line) { return line.find(needle) != std::string::npos; }));
}

// ---------------------------------------------------------------------------------------------
// Hand-built battle fixture (the battle_command_tests recipe, WP-16 numbers)
// ---------------------------------------------------------------------------------------------

/// Content + the minimal battle contexts on a hand-built floor grid. `actor` (party) at (5,5) with a
/// tempo-capable loadout, `foe` (enemy) north at (5,4), `foe2` (enemy) west at (4,5). Enemy defense is 20 so
/// every skill lands the minimum 1 damage — the tests can then tell skill damage from tempo damage exactly.
struct FTempoFixture {
    std::filesystem::path checkpoint;
    game_test::FTestRegistry loaded;
    entt::entity actor{entt::null};
    entt::entity foe{entt::null};
    entt::entity foe2{entt::null};

    explicit FTempoFixture(std::string_view label, std::vector<std::string> roots = TestAssetRoots())
        : checkpoint(MakeCheckpointPath(label)),
          loaded(game_test::LoadTestRegistry(std::move(roots), checkpoint.string())) {
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
        r.ctx().emplace<game::FBattleEvents>();
        r.ctx().emplace<game::FCommandResults>();
        r.ctx().emplace<game::FCommandOutbox>();
        actor = MakeUnit("Adam", 5, 5, true);
        foe = MakeUnit("Bela", 5, 4, false);
        foe2 = MakeUnit("Cato", 4, 5, false);
        r.emplace<game::FTurnParticipant>(actor);
    }

    ~FTempoFixture() { RemoveCheckpointFiles(checkpoint); }

    FTempoFixture(const FTempoFixture&) = delete;
    FTempoFixture& operator=(const FTempoFixture&) = delete;

    entt::registry& registry() { return loaded.registry; }

    entt::entity MakeUnit(std::string_view tag, int x, int y, bool party, std::vector<std::string> skills = {}) {
        auto& r = registry();
        const entt::entity unit = r.create();
        r.emplace<game::FTag>(unit, game::FTag{std::string(tag)});
        r.emplace<game::FStats>(unit, party ? game::FStats{10, 2, 2, 2} : game::FStats{1, 1, 20, 20});
        r.emplace<game::FHealth>(unit, game::FHealth{50, 50});
        r.emplace<game::FGridPosition>(unit, game::FGridPosition{x, y});
        r.emplace<FPosition>(unit, FPosition{0.f, 0.f});
        r.emplace<game::FActionPoints>(unit, game::FActionPoints{1, 1, 2, 2});
        r.emplace<game::FSpeed>(unit, game::FSpeed{10});
        r.emplace<game::FActiveStatuses>(unit);
        if (skills.empty()) {
            skills = {"skill.shove", "skill.strike", "skill.barrage", "skill.arrow"};
        }
        game::FSkillLoadout loadout;
        for (std::string& id : skills) {
            loadout.slots.push_back(game::FSkillSlot{std::move(id), {}});
        }
        r.emplace<game::FSkillLoadout>(unit, std::move(loadout));
        if (party) {
            r.emplace<game::FPartyMember>(unit);
        } else {
            r.emplace<game::FEnemy>(unit);
        }
        return unit;
    }

    /// Starts the session; the actor is the only participant, so it always opens.
    FTurnToken StartTurn() {
        auto& runtime = registry().ctx().get<game::FTurnRuntime>();
        CHECK(runtime.controller.start(registry(), 20261108u).has_value());
        const auto current = runtime.controller.snapshot().currentTurn;
        CHECK(current.has_value());
        CHECK(current->actor == actor);
        return *current;
    }

    /// Direct push (the pre-API path: no meta) + one resolver drain; returns the recorded result.
    FCommandResult Tackle(const FTurnToken& token, entt::entity target, std::string_view skillId) {
        auto& r = registry();
        r.ctx().get<game::FActionQueue>().pending.push_back(
            game::FActionRequest{actor, target, std::string{skillId}, token, {}});
        game::ecs::ActionResolverSystem resolver;
        resolver.update(r, 0.f);
        const auto& recent = r.ctx().get<game::FCommandResults>().recent;
        CHECK(!recent.empty());
        return recent.back();
    }
};

// ---------------------------------------------------------------------------------------------
// Push / pull (WP-16 §1: displacement)
// ---------------------------------------------------------------------------------------------

/// PushStopsAtWallUnit: a wall, another unit or the grid edge stops the push on the last valid cell and
/// the blocked movement deals the collision damage; a stop cell that is a trap deals the trap damage.
void PushStopsAtWallUnit() {
    // Wall: no movement at all — the foe cannot enter (5,3); collision damage on top of the hit.
    {
        FTempoFixture fixture{"push-wall"};
        const auto token = fixture.StartTurn();
        fixture.registry().ctx().get<game::FGridTerrain>().cells[3 * game::kGridWidth + 5] = game::ETile::Wall;
        const int hpBefore = fixture.registry().get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK((fixture.registry().get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4}));
        CHECK(fixture.registry().get<game::FHealth>(fixture.foe).current == hpBefore - 1 - 1); // hit + collision
        CHECK(CountLines(Log(fixture.registry()).lines, "slams into an obstacle: -1 HP.") == 1);
    }
    // Another unit blocking the cell behaves exactly like a wall (same collision).
    {
        FTempoFixture fixture{"push-unit"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.get<game::FGridPosition>(fixture.foe2) = {5, 3};
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 2);
        CHECK(CountLines(Log(r).lines, "slams into an obstacle: -1 HP.") == 1);
    }
    // Edge: the push would leave the grid — the foe stays and takes the collision.
    {
        FTempoFixture fixture{"push-edge"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.get<game::FGridPosition>(fixture.actor) = {5, 1};
        r.get<game::FGridPosition>(fixture.foe) = {5, 0};
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 0}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 2);
    }
    // (a) A distance-2 push whose first step lands on a trap and whose second is blocked: the foe really
    // enters the trap cell, so the hit, the spikes AND the collision all land.
    {
        FTempoFixture fixture{"push-blocked-trap"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        GiveHooks(r, fixture.actor, {OnHitHook("test.push2", "push", 0, 2)}, "skill.shove");
        r.ctx().get<game::FGridTerrain>().cells[3 * game::kGridWidth + 5] = game::ETile::Trap; // (5,3) = first step
        r.ctx().get<game::FGridTerrain>().cells[2 * game::kGridWidth + 5] = game::ETile::Wall; // (5,2) blocks
        r.ctx().get<game::FGridTerrain>().trapDamage = 6;
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 3}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1 - 6 - 1); // hit + spikes + collision
        CHECK(CountLines(Log(r).lines, "lands on spikes: -6 HP.") == 1);
        CHECK(CountLines(Log(r).lines, "slams into an obstacle: -1 HP.") == 1);
    }
    // (b) The foe already stands on the trap and the push is blocked immediately: it never enters a cell,
    // so the trap does not re-trigger — only the hit and the collision land.
    {
        FTempoFixture fixture{"push-blocked-trap-standing"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.ctx().get<game::FGridTerrain>().cells[3 * game::kGridWidth + 5] = game::ETile::Wall; // (5,3) blocks
        r.ctx().get<game::FGridTerrain>().cells[4 * game::kGridWidth + 5] = game::ETile::Trap; // (5,4) = its own cell
        r.ctx().get<game::FGridTerrain>().trapDamage = 6;
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1 - 1); // hit + collision only
        CHECK(CountLines(Log(r).lines, "lands on spikes") == 0);
        CHECK(CountLines(Log(r).lines, "slams into an obstacle: -1 HP.") == 1);
    }
    // A killing hit does not move its victim: the damage application precedes the OnHit dispatch.
    {
        FTempoFixture fixture{"push-kill"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.get<game::FHealth>(fixture.foe).current = 1;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK(r.all_of<game::FDead>(fixture.foe));
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4})); // stayed where it fell
        CHECK(CountLines(Log(r).lines, "slams into an obstacle") == 0);
    }
}

/// PushDirectionRules: the pure ComputePushDestination rules — no resolver, no battle: the dominant axis
/// (tie = x), the pull's never-past-the-caster rule, and the zero-distance no-op.
void PushDirectionRules() {
    const auto setup = [](entt::registry& registry, game::FGridPosition targetCell) {
        registry.ctx().emplace<game::FGridMetrics>();
        registry.ctx().emplace<game::FGridTerrain>();
        const entt::entity target = registry.create();
        registry.emplace<game::FGridPosition>(target, targetCell);
        return target;
    };

    // Non-aligned pull: caster (0,0), target (3,1), pull 5. The dominant axis is x (|3| > |1|), so the
    // step walks left along the target's row; reaching the caster's column (0,1) is fine, (-1,1) would
    // pass it — that stop is the intended stop and must not report a collision.
    {
        entt::registry registry;
        const entt::entity target = setup(registry, {3, 1});
        const game::FPushResult result = game::ComputePushDestination(
            registry, target, game::FGridPosition{0, 0}, 5, game::EDisplacementDirection::Toward);
        CHECK((result.finalCell == game::FGridPosition{0, 1}));
        CHECK(result.path.size() == 4u);
        CHECK(!result.hitObstacle);
    }
    // Tie rule: |dx| == |dy| picks the x axis, so a push of (2,2) from (0,0) goes right, not down.
    {
        entt::registry registry;
        const entt::entity target = setup(registry, {2, 2});
        const game::FPushResult result = game::ComputePushDestination(
            registry, target, game::FGridPosition{0, 0}, 1, game::EDisplacementDirection::Away);
        CHECK((result.finalCell == game::FGridPosition{3, 2}));
        CHECK(result.path.size() == 2u);
        CHECK(!result.hitObstacle);
    }
    // Distance 0: no movement, the path holds only the unit's own cell.
    {
        entt::registry registry;
        const entt::entity target = setup(registry, {2, 2});
        const game::FPushResult result = game::ComputePushDestination(
            registry, target, game::FGridPosition{0, 0}, 0, game::EDisplacementDirection::Away);
        CHECK((result.finalCell == game::FGridPosition{2, 2}));
        CHECK(result.path.size() == 1u);
        CHECK(!result.hitObstacle);
    }
}

/// PushCollisionDealsOne: the collision damage is the `push_collision_damage` data constant — default 1,
/// and the captured value follows a temporary dungeon document that authors 3.
void PushCollisionDealsOne() {
    {
        FTempoFixture fixture{"collision-default"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.ctx().get<game::FGridTerrain>().cells[3 * game::kGridWidth + 5] = game::ETile::Wall;
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1 - 1);
        CHECK(CountLines(Log(r).lines, "slams into an obstacle: -1 HP.") == 1);
    }
    {
        FTempRoot temp{"collision-tune"};
        nlohmann::json dungeon = FixtureDungeon();
        dungeon["balance"]["push_collision_damage"] = 3;
        WriteJsonFile(temp.root / "data" / "dungeon.json", dungeon);
        FTempoFixture fixture{"collision-three", RootsWith(temp)};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.ctx().get<game::FGridTerrain>().cells[3 * game::kGridWidth + 5] = game::ETile::Wall;
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1 - 3);
        CHECK(CountLines(Log(r).lines, "slams into an obstacle: -3 HP.") == 1);
    }
}

/// PushOntoTrapUsesTrapDamage: a push that travels its full distance onto a trap deals the trap damage and
/// no collision damage (the landing cell is where the unit stops, not an obstacle).
void PushOntoTrapUsesTrapDamage() {
    FTempoFixture fixture{"push-trap"};
    const auto token = fixture.StartTurn();
    auto& r = fixture.registry();
    r.ctx().get<game::FGridTerrain>().cells[3 * game::kGridWidth + 5] = game::ETile::Trap; // (5,3)
    r.ctx().get<game::FGridTerrain>().trapDamage = 6;
    const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
    const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
    CHECK(result.reject == ECommandReject::None);
    CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 3})); // full distance
    CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1 - 6);          // hit + spikes only
    CHECK(CountLines(Log(r).lines, "lands on spikes: -6 HP.") == 1);
}

/// PullStopsAdjacentCaster: a pull stops on the cell adjacent to the caster — the intended stop is not a
/// collision; a pull blocked earlier by a unit is.
void PullStopsAdjacentCaster() {
    const auto givePull = [](FTempoFixture& fixture, std::string_view skill, int distance) {
        GiveHooks(fixture.registry(), fixture.actor,
                  {OnHitHook("test.pull", "pull", 0, distance)}, std::string{skill});
    };
    // Adjacent already: the pull cannot enter the caster's cell — no movement, no collision.
    {
        FTempoFixture fixture{"pull-adjacent"};
        const auto token = fixture.StartTurn();
        givePull(fixture, "skill.strike", 2);
        auto& r = fixture.registry();
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.strike");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1); // strike only
        CHECK(CountLines(Log(r).lines, "slams into an obstacle") == 0);
        CHECK(CountLines(Log(r).lines, " is pulled ") == 0);
    }
    // Gap 4, pull 4: three cells of travel, then the caster's cell — the stop is intended, no collision.
    {
        FTempoFixture fixture{"pull-gap"};
        const auto token = fixture.StartTurn();
        givePull(fixture, "skill.arrow", 4);
        auto& r = fixture.registry();
        r.get<game::FGridPosition>(fixture.foe) = {5, 1};
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.arrow");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1); // arrow only
        CHECK(CountLines(Log(r).lines, " is pulled 3.") == 1);
        CHECK(CountLines(Log(r).lines, "slams into an obstacle") == 0);
    }
    // A unit blocking the pull path stops it early — that IS a collision. (A wall cannot be used here: it
    // would also sit between caster and target and the resolver's line-of-sight check rejects the skill
    // before any hook runs.)
    {
        FTempoFixture fixture{"pull-block"};
        const auto token = fixture.StartTurn();
        givePull(fixture, "skill.arrow", 4);
        auto& r = fixture.registry();
        r.get<game::FGridPosition>(fixture.foe) = {5, 1};
        r.get<game::FGridPosition>(fixture.foe2) = {5, 3}; // blocks the pull path, not the arrow's LoS (terrain-only)
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.arrow");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 2}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1 - 1); // arrow + collision
        CHECK(CountLines(Log(r).lines, "slams into an obstacle: -1 HP.") == 1);
    }
}

/// PushOffGridStops: a push never leaves the 12x12 battlefield — the edge cell is enterable, one step
/// further is not.
void PushOffGridStops() {
    // Pushed onto the edge cell itself: full distance, no collision.
    {
        FTempoFixture fixture{"push-edge-landing"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.get<game::FGridPosition>(fixture.actor) = {5, 2};
        r.get<game::FGridPosition>(fixture.foe) = {5, 1};
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 0}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 1);
        CHECK(CountLines(Log(r).lines, "slams into an obstacle") == 0);
    }
    // Pushed from the edge outward: the grid edge stops it like a wall.
    {
        FTempoFixture fixture{"push-edge-blocked"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.get<game::FGridPosition>(fixture.actor) = {5, 1};
        r.get<game::FGridPosition>(fixture.foe) = {5, 0};
        const int hpBefore = r.get<game::FHealth>(fixture.foe).current;
        const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
        CHECK(result.reject == ECommandReject::None);
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 0}));
        CHECK(r.get<game::FHealth>(fixture.foe).current == hpBefore - 2);
    }
}

/// PushMovesPresentation: the resolver emits the skill event BEFORE the Move the push hook produced, and
/// the Move event carries the walked path (start cell first) for the presentation.
void PushMovesPresentation() {
    FTempoFixture fixture{"push-presentation"};
    const auto token = fixture.StartTurn();
    auto& r = fixture.registry();
    auto& events = r.ctx().get<game::FBattleEvents>();
    CHECK(events.pending.empty());
    const auto result = fixture.Tackle(token, fixture.foe, "skill.shove");
    CHECK(result.reject == ECommandReject::None);
    CHECK(events.pending.size() == 2);
    const game::FBattleEvent& skill = events.pending[0];
    const game::FBattleEvent& move = events.pending[1];
    CHECK(skill.kind == game::FBattleEvent::EKind::Skill);
    CHECK(skill.actor == fixture.actor);
    CHECK(skill.skillId == "skill.shove");
    CHECK(skill.targets.size() == 1);
    CHECK(skill.targets[0].unit == fixture.foe);
    CHECK(move.kind == game::FBattleEvent::EKind::Move);
    CHECK(move.actor == fixture.foe);
    CHECK(move.path.size() == 2);
    CHECK((move.path[0] == game::FGridPosition{5, 4}));
    CHECK((move.path[1] == game::FGridPosition{5, 3}));

    // A killing hit moves nobody: the skill event is the only one.
    FTempoFixture killed{"push-presentation-kill"};
    const auto killToken = killed.StartTurn();
    auto& killedRegistry = killed.registry();
    auto& killEvents = killedRegistry.ctx().get<game::FBattleEvents>();
    killedRegistry.get<game::FHealth>(killed.foe).current = 1;
    const auto killResult = killed.Tackle(killToken, killed.foe, "skill.shove");
    CHECK(killResult.reject == ECommandReject::None);
    CHECK(killEvents.pending.size() == 1);
    CHECK(killEvents.pending[0].kind == game::FBattleEvent::EKind::Skill);
}

// ---------------------------------------------------------------------------------------------
// move_lock (WP-16 §2: the status kind)
// ---------------------------------------------------------------------------------------------

/// LockMoveTickFlag: a move_lock tick sets moveApZero with no RNG roll — neither at application
/// (TryApplyStatus delegates: it is not a control kind) nor at the tick.
void LockMoveTickFlag() {
    const FStatusContent status = MoveLockStatus();
    game::FActiveStatuses active;
    game::FBattleRng rng{0x1357u};
    const std::uint32_t before = rng.state;
    CHECK(game::TryApplyStatus(active, status, 0, 0, rng));
    CHECK(active.effects.size() == 1);
    CHECK(active.effects[0].statusId == "status.locked_move");
    CHECK(!active.effects[0].skipPending);
    CHECK(rng.state == before); // no application roll

    const std::vector<const FStatusContent*> definitions{&status};
    const game::FStatusTickResult tick = game::TickStatuses(active, definitions, rng);
    CHECK(tick.moveApZero);
    CHECK(!tick.skipTurn);
    CHECK(tick.damage == 0);
    CHECK(rng.state == before); // no tick roll either
}

/// LockMoveDoesNotSkipTurn: the authored row decodes to the new kind and denies movement only — it never
/// reads as a skipped turn (W5 review #8: that is why it is not a control).
void LockMoveDoesNotSkipTurn() {
    const game::gameplay::FContentRegistry content = FixtureContent();
    const FStatusContent* definition = content.statuses.find("status.locked_move");
    CHECK(definition != nullptr);
    CHECK(definition->kind == game::gameplay::kStatusKindMoveLock);
    CHECK(definition->magnitude == 0);
    CHECK(definition->durationTurns == 1);

    game::FActiveStatuses active;
    game::ApplyStatus(active, *definition);
    CHECK(!game::IsTurnSkipped(active, {definition}));
    game::FBattleRng rng{0x2468u};
    const game::FStatusTickResult tick = game::TickStatuses(active, {definition}, rng);
    CHECK(tick.moveApZero);
    CHECK(!tick.skipTurn);
}

/// LockMoveExpiresAfterConsume: duration 1 = "the owner's next turn once" — the flag is produced and the
/// effect expires in the same tick; a fresh application locks the next turn again.
void LockMoveExpiresAfterConsume() {
    const FStatusContent status = MoveLockStatus();
    game::FActiveStatuses active;
    game::ApplyStatus(active, status); // deterministic
    const std::vector<const FStatusContent*> definitions{&status};
    game::FBattleRng rng{0x9876u};

    CHECK(game::TickStatuses(active, definitions, rng).moveApZero);
    CHECK(active.effects.empty()); // duration 1 consumed by the same tick

    CHECK(!game::TickStatuses(active, definitions, rng).moveApZero); // no leftover lock

    game::ApplyStatus(active, status); // re-application starts a new one-turn lock
    CHECK(game::TickStatuses(active, definitions, rng).moveApZero);
    CHECK(active.effects.empty());
}

/// MoveLockValueIsHalved: the AI values the lock at halved threat, hostile to its target, and a re-apply
/// while the lock is already pending adds nothing (the gain rule shared with the other kinds).
void MoveLockValueIsHalved() {
    struct FAiContent {
        game::gameplay::FContentRegistry content;
        entt::registry registry;
        game::ai::FAiBlackboard blackboard;
        game::ai::FBtMemory memory;
        game::ai::FAiTuning tuning;
        game::ai::FAiWorld world;

        FAiContent() {
            const game::FAssetManager assets{TestAssetRoots()};
            std::optional<game::gameplay::FContentRegistry> loaded = game::gameplay::FContentRegistry::load(assets);
            CHECK(loaded.has_value());
            content = std::move(*loaded);
        }

        [[nodiscard]] game::ai::FBtContext Context() {
            return {registry, content, tuning, world, blackboard, memory, entt::null, {}, false};
        }
    };

    FAiContent fixture;
    game::ai::FUnitView target;
    target.hp = target.hpMax = 30;
    target.threat = 10;
    auto ctx = fixture.Context();
    CHECK(game::ai::ValueOfStatus(ctx, {"status.locked_move", 0, 0}, target, false) == 5);  // 10 / 2
    CHECK(game::ai::ValueOfStatus(ctx, {"status.locked_move", 0, 0}, target, true) == -5);  // hostile debuff

    target.effects.push_back(game::FStatusEffect{"status.locked_move", 1, 0, false});
    CHECK(game::ai::ValueOfStatus(ctx, {"status.locked_move", 0, 0}, target, false) == 0);  // re-apply adds nothing
}

// ---------------------------------------------------------------------------------------------
// chain (WP-16 §3)
// ---------------------------------------------------------------------------------------------

/// ChainBouncesNearestSameSide: the bounce picks the nearest OTHER living unit of the victim's side
/// within reach; ties go to the lower entity id, and the other side is never touched.
void ChainBouncesNearestSameSide() {
    // Nearest wins even when a farther candidate has the lower id.
    {
        FTempoFixture fixture{"chain-nearest"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        GiveHooks(r, fixture.actor, {OnHitHook("test.chain", "chain", 3, 2)}, "skill.strike");
        r.get<game::FGridPosition>(fixture.foe2) = {7, 4}; // distance 2 from the victim (5,4)
        const entt::entity foe3 = fixture.MakeUnit("Dora", 5, 3, false); // distance 1, higher id
        const entt::entity ally = fixture.MakeUnit("Eve", 4, 4, true);   // distance 1, wrong side
        const int foeHp = r.get<game::FHealth>(fixture.foe).current;
        const int foe3Hp = r.get<game::FHealth>(foe3).current;
        const int allyHp = r.get<game::FHealth>(ally).current;

        const auto result = fixture.Tackle(token, fixture.foe, "skill.strike");
        CHECK(result.reject == ECommandReject::None);
        CHECK(r.get<game::FHealth>(fixture.foe).current == foeHp - 1);   // the strike itself
        CHECK(r.get<game::FHealth>(foe3).current == foe3Hp - 3);         // nearest of the victim's side
        CHECK(r.get<game::FHealth>(fixture.foe2).current == 50);         // farther candidate untouched
        CHECK(r.get<game::FHealth>(ally).current == allyHp);             // other side never touched
        CHECK(CountLines(Log(r).lines, "is caught in the chain: -3 HP.") == 1);
    }
    // Equal distance: the lower entity id (created first) takes the bounce.
    {
        FTempoFixture fixture{"chain-tie"};
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        GiveHooks(r, fixture.actor, {OnHitHook("test.chain", "chain", 3, 2)}, "skill.strike");
        r.get<game::FGridPosition>(fixture.foe2) = {6, 4}; // distance 1, lower id
        const entt::entity foe3 = fixture.MakeUnit("Dora", 5, 3, false); // distance 1, higher id

        const auto result = fixture.Tackle(token, fixture.foe, "skill.strike");
        CHECK(result.reject == ECommandReject::None);
        CHECK(r.get<game::FHealth>(fixture.foe2).current == 47);
        CHECK(r.get<game::FHealth>(foe3).current == 50);
    }
}

/// ChainKindValidatedAtBoot: the chain validator demands amount > 0 && distance >= 1; the push/pull
/// validators demand from == "caster" && distance >= 1 — every violation fails ValidateEffectContent.
void ChainKindValidatedAtBoot() {
    const auto checkpoint = MakeCheckpointPath("chain-validate");
    {
        game_test::FTestRegistry loaded = game_test::LoadTestRegistry(TestAssetRoots(), checkpoint.string());
        CHECK(loaded.content);
        CHECK(loaded.dungeon);
        auto& r = loaded.registry;
        game::gameplay::FSkillContent* probe = FindSkillRow(r, "skill.snipe");
        CHECK(probe != nullptr);

        probe->hooks = {OnHitHook("test.chain.valid", "chain", 3, 2)};
        CHECK(game::ValidateEffectContent(r));

        probe->hooks = {OnHitHook("test.chain.amount", "chain", 0, 2)};
        CHECK(!game::ValidateEffectContent(r));

        probe->hooks = {OnHitHook("test.chain.distance", "chain", 3, 0)};
        CHECK(!game::ValidateEffectContent(r));
        probe->hooks.clear();

        // The shipped shove hook is valid; zeroing its distance or changing `from` fails the boot gate.
        game::gameplay::FSkillContent* shove = FindSkillRow(r, "skill.shove");
        CHECK(shove != nullptr && shove->hooks.size() == 1);
        CHECK(game::ValidateEffectContent(r));
        shove->hooks[0].effect.distance = 0;
        CHECK(!game::ValidateEffectContent(r));
        shove->hooks[0].effect.distance = 1;
        shove->hooks[0].effect.from = "target";
        CHECK(!game::ValidateEffectContent(r));
        shove->hooks[0].effect.from = "caster";
        CHECK(game::ValidateEffectContent(r));
    }
    RemoveCheckpointFiles(checkpoint);
}

/// ChainBouncesFromDeadVictimsCell: a lethal hit still bounces from the cell the victim died on
/// (FGridPosition outlives FDead).
void ChainBouncesFromDeadVictimsCell() {
    FTempoFixture fixture{"chain-dead"};
    const auto token = fixture.StartTurn();
    auto& r = fixture.registry();
    GiveHooks(r, fixture.actor, {OnHitHook("test.chain", "chain", 3, 3)}, "skill.strike");
    r.get<game::FGridPosition>(fixture.foe2) = {6, 4}; // distance 1 from the victim's cell
    r.get<game::FHealth>(fixture.foe).current = 1;

    const auto result = fixture.Tackle(token, fixture.foe, "skill.strike");
    CHECK(result.reject == ECommandReject::None);
    CHECK(r.all_of<game::FDead>(fixture.foe));
    CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4}));
    CHECK(r.get<game::FHealth>(fixture.foe2).current == 47);
    CHECK(CountLines(Log(r).lines, "knocked out!") == 1);
    CHECK(CountLines(Log(r).lines, "is caught in the chain: -3 HP.") == 1);
}

/// ChainDoesNotRetriggerHooks: the bounce damage goes through ApplyDamageToUnit directly — the victim's
/// side never gets an OnHit/OnDamageTaken dispatch (a probe hook on the bounced unit stays silent).
void ChainDoesNotRetriggerHooks() {
    FTempoFixture fixture{"chain-isolated"};
    const auto token = fixture.StartTurn();
    auto& r = fixture.registry();
    GiveHooks(r, fixture.actor, {OnHitHook("test.chain", "chain", 3, 3)}, "skill.strike");
    r.get<game::FGridPosition>(fixture.foe2) = {6, 4};
    GiveHooks(r, fixture.foe2, {LogProbeHook("test.probe", "CHAIN-RETRIGGERED")}, "skill.strike");

    const auto result = fixture.Tackle(token, fixture.foe, "skill.strike");
    CHECK(result.reject == ECommandReject::None);
    CHECK(r.get<game::FHealth>(fixture.foe2).current == 47); // the bounce landed
    CHECK(CountLines(Log(r).lines, "CHAIN-RETRIGGERED") == 0);
    CHECK(CountLines(Log(r).lines, "-> chain") == 1); // exactly one dispatch: the attacker's hook
}

// ---------------------------------------------------------------------------------------------
// skill_ap_cap (WP-16 §4: D-1)
// ---------------------------------------------------------------------------------------------

/// ApCapValidation: the pure gate rejects a 3-AP skill under cap 2 and accepts it under cap 3; the boot
/// wrapper reads the cap from a temporary dungeon document; malformed keys fail the load.
void ApCapValidation() {
    game::gameplay::FContentRegistry content = FixtureContent();
    content.skills.rows.push_back(FakeThreeApSkill());
    CHECK(!game::gameplay::ValidateSkillApCap(2, content)); // cap 2 + a 3-AP skill = boot failure
    CHECK(game::gameplay::ValidateSkillApCap(3, content));  // cap 3 admits it

    FTempRoot temp{"ap-cap"};
    nlohmann::json dungeon = FixtureDungeon();
    dungeon["balance"]["skill_ap_cap"] = 3;
    WriteJsonFile(temp.root / "data" / "dungeon.json", dungeon);
    {
        const game::FAssetManager assets{RootsWith(temp)};
        const std::optional<game::gameplay::FTempoTuning> tuning = game::gameplay::LoadTempoTuning(assets);
        CHECK(tuning.has_value());
        CHECK(tuning->skillApCap == 3);
        CHECK(tuning->pushCollisionDamage == game::gameplay::kDefaultPushCollisionDamage);
        CHECK(game::gameplay::ValidateSkillApCap(assets, content)); // wrapper: cap 3 passes
    }
    dungeon["balance"]["skill_ap_cap"] = 2;
    WriteJsonFile(temp.root / "data" / "dungeon.json", dungeon);
    {
        const game::FAssetManager assets{RootsWith(temp)};
        CHECK(!game::gameplay::ValidateSkillApCap(assets, content)); // wrapper: cap 2 fails on the 3-AP skill
    }
    // The default when both keys are absent.
    {
        FTempRoot defaults{"ap-cap-defaults"};
        nlohmann::json plain = FixtureDungeon();
        plain["balance"].erase("skill_ap_cap");
        plain["balance"].erase("push_collision_damage");
        WriteJsonFile(defaults.root / "data" / "dungeon.json", plain);
        const game::FAssetManager assets{RootsWith(defaults)};
        const std::optional<game::gameplay::FTempoTuning> tuning = game::gameplay::LoadTempoTuning(assets);
        CHECK(tuning.has_value());
        CHECK(tuning->skillApCap == game::gameplay::kDefaultSkillApCap);
        CHECK(tuning->pushCollisionDamage == game::gameplay::kDefaultPushCollisionDamage);
    }
    // Malformed keys are refused (the boot gate stops the boot on them).
    const auto rejects = [&](const char* key, const nlohmann::json& value) {
        FTempRoot bad{"ap-cap-bad"};
        nlohmann::json document = FixtureDungeon();
        document["balance"][key] = value;
        WriteJsonFile(bad.root / "data" / "dungeon.json", document);
        const game::FAssetManager assets{RootsWith(bad)};
        CHECK(!game::gameplay::LoadTempoTuning(assets).has_value());
    };
    rejects("skill_ap_cap", 0);
    rejects("skill_ap_cap", "two");
    rejects("push_collision_damage", -1);
}

/// OneApSkillsAllowTwoPerTurn: two 1-AP skills fit one turn's 2 skill AP; a third is refused with NoAp.
void OneApSkillsAllowTwoPerTurn() {
    FTempoFixture fixture{"one-ap-pair"};
    const auto token = fixture.StartTurn();
    auto& r = fixture.registry();
    const entt::entity foe3 = fixture.MakeUnit("Dora", 6, 5, false);

    const auto first = fixture.Tackle(token, fixture.foe, "skill.shove");
    CHECK(first.reject == ECommandReject::None);
    CHECK(r.get<game::FActionPoints>(fixture.actor).skill == 1);
    const auto second = fixture.Tackle(token, fixture.foe2, "skill.shove");
    CHECK(second.reject == ECommandReject::None);
    CHECK(r.get<game::FActionPoints>(fixture.actor).skill == 0);

    const auto third = fixture.Tackle(token, foe3, "skill.shove");
    CHECK(third.reject == ECommandReject::NoAp);
    CHECK(third.reason.find("(0/1)") != std::string::npos);
    CHECK(r.get<game::FActionPoints>(fixture.actor).skill == 0);
}

/// FirebombIsTileCommitted: the shaman starter loadout arrives through `placement.skills`, and the D-16
/// hybrid default classifies the firebomb as a tile commit (target cell / zone shape — WP-11 implements
/// the rule; this test pins the data side it reads).
void FirebombIsTileCommitted() {
    const auto checkpoint = MakeCheckpointPath("firebomb-tile");
    {
        entt::registry registry;
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));

        nlohmann::json document;
        document["width"] = game::kGridWidth;
        document["height"] = game::kGridHeight;
        document["tile_size"] = 64;
        document["origin_x"] = 0;
        document["origin_y"] = 0;
        document["terrain"] = nlohmann::json::array();
        for (int y = 0; y < game::kGridHeight; ++y) {
            document["terrain"].push_back(std::string(static_cast<std::size_t>(game::kGridWidth), '.'));
        }
        const nlohmann::json warrior{{"id", "party.warrior"}, {"display_name", "Warrior"}, {"faction", "party"},
                                     {"grid_x", 5}, {"grid_y", 6}, {"unit", "char.warrior"}};
        const nlohmann::json shaman{
            {"id", "enemy.shaman"}, {"display_name", "Imp Hexer"}, {"faction", "enemy"},
            {"grid_x", 6}, {"grid_y", 6}, {"unit", "enemy.imp_shaman"},
            {"skills", nlohmann::json::array({nlohmann::json{{"skill", "skill.bolt"}},
                                              nlohmann::json{{"skill", "skill.imp_firebomb"}}})}};
        document["units"] = nlohmann::json::array({warrior, shaman});

        auto scene = std::make_unique<game::FScene>(registry, "TempoFirebombTest");
        scene->Load();
        const auto created = game::FEntityFactory::CreateBattlefield(*scene, document);
        CHECK(created.has_value());
        registry.ctx().erase<game::FGridTerrain>();
        registry.ctx().emplace<game::FGridTerrain>(created->terrain);
        if (auto* metrics = registry.ctx().find<game::FGridMetrics>()) {
            *metrics = created->metrics;
        } else {
            registry.ctx().emplace<game::FGridMetrics>(created->metrics);
        }
        const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
        CHECK(game::SetupCombatUnits(registry, *created, content));

        entt::entity shamanEntity = entt::null;
        for (const entt::entity entity : registry.view<game::FUnitData>()) {
            if (registry.get<game::FUnitData>(entity).definition.id == "enemy.shaman") {
                shamanEntity = entity;
            }
        }
        CHECK(shamanEntity != entt::null);
        const auto& loadout = registry.get<game::FSkillLoadout>(shamanEntity);
        CHECK(loadout.slots.size() == 2);
        CHECK(loadout.slots[0].skillId == "skill.bolt");
        CHECK(loadout.slots[1].skillId == "skill.imp_firebomb");

        const auto firebomb = game::ResolveSkillSlot(loadout.slots[1], content);
        const auto bolt = game::ResolveSkillSlot(loadout.slots[0], content);
        CHECK(firebomb.has_value() && bolt.has_value());
        // The D-16 hybrid default rule WP-11's EvaluateIntent implements: cell target or
        // spread/zone/pierce shape commits to a tile.
        const auto tileCommitted = [](const game::FResolvedSkill& skill) {
            return skill.target == "cell" || skill.shape == "spread" || skill.shape == "zone" ||
                   skill.shape == "pierce";
        };
        CHECK(tileCommitted(*firebomb));
        CHECK(!tileCommitted(*bolt));

        scene.reset();
    }
    RemoveCheckpointFiles(checkpoint);
}

// ---------------------------------------------------------------------------------------------
// Auto-battle report metrics (WP-16 §5) — numbers are printed, never gated (R3-review #1)
// ---------------------------------------------------------------------------------------------

/// One real dungeon battle driven by the full turn pipeline (the AiTests recipe): TurnSystem →
/// TurnFlowSystem → ActionResolverSystem → CombatOutcomeSystem, both sides on auto-play.
struct FAutoBattle {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    FEmberBattleState battle;
    std::filesystem::path checkpoint;
    std::filesystem::path runSave;

    struct FResult {
        bool finished{false};
        bool victory{false};
        int rounds{0};
        std::vector<std::string> lines;
    };

    FAutoBattle(std::uint32_t seed, int depth, const std::vector<std::string>& roots) {
        checkpoint = MakeCheckpointPath("matrix");
        game::FAssetManager::Initialize(registry, roots);
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        if (const auto* save = registry.ctx().find<game::FDungeonSave>()) {
            runSave = save->path;
        }
        turns.onRegister(registry);
        game::StartDungeonRun(registry, seed, true);
        auto& run = registry.ctx().get<game::FDungeonRun>();
        run.depth = depth;
        run.room = 0;
        battle.OnEnter(registry);
        registry.ctx().get<game::FCombatRuntime>().autoPlay = true;
    }

    ~FAutoBattle() {
        battle.OnExit(registry);
        RemoveCheckpointFiles(checkpoint);
        std::error_code error;
        if (!runSave.empty()) {
            std::filesystem::remove(runSave, error);
            std::filesystem::remove(runSave.string() + ".tmp", error);
        }
    }

    FAutoBattle(const FAutoBattle&) = delete;
    FAutoBattle& operator=(const FAutoBattle&) = delete;

    /// FCombatLog keeps only its last 100 lines; this rebuilds the nearly-full stream for the report by
    /// diffing each frame's tail against the previous capture. Only a same-line repeat inside one frame
    /// can make it off by one — the report tolerates that (R3-review #1: these numbers are not gates).
    static void CaptureNewLogLines(const game::FCombatLog& log, std::vector<std::string>& captured) {
        const std::vector<std::string>& lines = log.lines;
        if (captured.empty()) {
            captured = lines;
            return;
        }
        const std::string& tail = captured.back();
        for (std::size_t index = lines.size(); index-- > 0;) {
            if (lines[index] == tail) {
                captured.insert(captured.end(), lines.begin() + static_cast<std::ptrdiff_t>(index + 1), lines.end());
                return;
            }
        }
        captured.insert(captured.end(), lines.begin(), lines.end());
    }

    [[nodiscard]] FResult Play() {
        FResult result;
        std::uint64_t steps = 0;
        auto* log = registry.ctx().find<game::FCombatLog>();
        while (!registry.ctx().get<game::FBattleOutcome>().resolved && steps++ < 6000) {
            turns.update(registry, .1f);
            flow.update(registry, .1f);
            actions.update(registry, .1f);
            outcomes.update(registry, .1f);
            if (log != nullptr) {
                CaptureNewLogLines(*log, result.lines);
            }
        }
        const auto& outcome = registry.ctx().get<game::FBattleOutcome>();
        result.finished = outcome.resolved && steps < 6000;
        result.victory = outcome.victory;
        result.rounds = static_cast<int>(registry.ctx().get<game::FTurnRuntime>().controller.snapshot().roundNumber);
        return result;
    }
};

struct FTempoMatrix {
    int battles{0};
    int finished{0};
    int wins{0};
    int totalRounds{0};
    int pushes{0};
    int pulls{0};
    int locks{0};
    int firebombs{0};
    int shoveHits{0};
    int barrageHits{0};
    int lungeHits{0};
    int slams{0};
};

FTempoMatrix RunTempoMatrix(const std::vector<std::string>& roots, const std::vector<std::uint32_t>& seeds, int depth) {
    FTempoMatrix matrix;
    for (const std::uint32_t seed : seeds) {
        FAutoBattle battle{seed, depth, roots};
        const FAutoBattle::FResult result = battle.Play();
        ++matrix.battles;
        matrix.finished += result.finished ? 1 : 0;
        matrix.wins += result.victory ? 1 : 0;
        matrix.totalRounds += result.rounds;
        matrix.pushes += CountLines(result.lines, " is pushed ");
        matrix.pulls += CountLines(result.lines, " is pulled ");
        matrix.slams += CountLines(result.lines, "slams into an obstacle");
        matrix.locks += CountLines(result.lines, "/ Shackle ->");
        matrix.firebombs += CountLines(result.lines, "/ Firebomb ->");
        matrix.shoveHits += CountLines(result.lines, "/ Shove ->");
        matrix.barrageHits += CountLines(result.lines, "/ Barrage ->");
        matrix.lungeHits += CountLines(result.lines, "/ Lunge ->");
        std::cout << "[report]   seed " << seed << " depth " << depth << ": "
                  << (result.victory ? "win" : "loss") << " in " << result.rounds << " rounds\n";
    }
    return matrix;
}

/// AiUsesTempoSkills: on the shipped loadouts (the WP-16 placements), how often did the AI actually fire a
/// tempo effect? Report only — zero would be information, not a failure. Six seeds (wider than the
/// round-count matrix) because a tempo pick depends on the warrior/mage standing where the skill is legal.
void AiUsesTempoSkills() {
    const FTempoMatrix matrix = RunTempoMatrix(ShipAssetRoots(), {1u, 2u, 3u, 4u, 5u, 6u}, 3);
    std::cout << "[report] AiUsesTempoSkills (shipped loadouts, 6 seeds x depth 3): battles=" << matrix.battles
              << " finished=" << matrix.finished << " wins=" << matrix.wins
              << " pushes=" << matrix.pushes << " collisionSlams=" << matrix.slams
              << " pulls=" << matrix.pulls << " moveLocks=" << matrix.locks
              << " firebombs=" << matrix.firebombs << " | skill hits: shove=" << matrix.shoveHits
              << " barrage=" << matrix.barrageHits << " lunge=" << matrix.lungeHits << '\n';
    CHECK(matrix.battles == 6);
    CHECK(matrix.finished == matrix.battles); // harness sanity only; the effect counts are the report
}

/// RoundCountDeltaReported: the same six seeds x depth 3 with and without the four WP-16 placements (a temp
/// root shadows units.json) — rounds and wins before vs after, reported as numbers. Six seeds (the
/// AiUsesTempoSkills set): after WP-28 T2 raised the shaman's room weight the original three-sample set
/// (seeds 1-3) stopped producing a firebomb, because the weighted composition consumes the RNG stream
/// differently and those three rooms fielded no shaman that cast.
void RoundCountDeltaReported() {
    FTempRoot temp{"before-units"};
    nlohmann::json units = ReadJsonFile("assets/data/units.json");
    const std::vector<std::pair<const char*, const char*>> placements{
        {"char.warrior", "skill.shove"},
        {"char.archer", "skill.barrage"},
        {"char.mage", "skill.shackle"},
        {"enemy.imp_shaman", "skill.imp_firebomb"},
    };
    int removed = 0;
    for (nlohmann::json& unit : units["units"]) {
        for (const auto& [unitId, skillId] : placements) {
            if (unit["id"].get<std::string>() != unitId) {
                continue;
            }
            nlohmann::json& skills = unit["skills"];
            const std::size_t before = skills.size();
            skills.erase(std::remove(skills.begin(), skills.end(), std::string{skillId}), skills.end());
            removed += static_cast<int>(before - skills.size());
        }
    }
    CHECK(removed == 4); // the before run must really lack the WP-16 loadouts
    WriteJsonFile(temp.root / "data" / "units.json", units);

    const std::vector<std::string> beforeRoots{temp.root.string(), kShipAssetRoot};
    // WP-28 T2 follow-up (see the test comment): the matrix's six seeds, so the after run provably fires
    // at least one real firebomb (it is the same deterministic battle set the report above just measured).
    const std::vector<std::uint32_t> seeds{1u, 2u, 3u, 4u, 5u, 6u};
    const FTempoMatrix before = RunTempoMatrix(beforeRoots, seeds, 3);
    const FTempoMatrix after = RunTempoMatrix(ShipAssetRoots(), seeds, 3);
    std::cout << "[report] RoundCountDeltaReported (6 seeds x depth 3): before (no 1-AP tempo skills): wins "
              << before.wins << "/" << before.battles << " rounds " << before.totalRounds
              << " firebombs " << before.firebombs
              << " | after: wins " << after.wins << "/" << after.battles << " rounds " << after.totalRounds
              << " firebombs " << after.firebombs << '\n';
    CHECK(before.finished == before.battles);
    CHECK(after.finished == after.battles);
    // The two runs really differ: the shaman only has its firebomb in the after loadouts. (The party's new
    // skills are never AI-selected in either run — that is what the AiUsesTempoSkills report shows.)
    CHECK(before.firebombs == 0);
    CHECK(after.firebombs > 0);
}

} // namespace

int main() {
    // Save safety (the effect_hooks contract): ctest exports no GAME01P_SAVE_DIR for feature tests, so
    // outside build-test.ps1 a plain run would target the user's real save under %APPDATA%. Commit an
    // override before any dungeon service can touch disk.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "tempo_skills_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("tempo skills", {
        {"PushStopsAtWallUnit", PushStopsAtWallUnit},
        {"PushDirectionRules", PushDirectionRules},
        {"PushCollisionDealsOne", PushCollisionDealsOne},
        {"PushOntoTrapUsesTrapDamage", PushOntoTrapUsesTrapDamage},
        {"PullStopsAdjacentCaster", PullStopsAdjacentCaster},
        {"PushOffGridStops", PushOffGridStops},
        {"PushMovesPresentation", PushMovesPresentation},
        {"LockMoveTickFlag", LockMoveTickFlag},
        {"LockMoveDoesNotSkipTurn", LockMoveDoesNotSkipTurn},
        {"LockMoveExpiresAfterConsume", LockMoveExpiresAfterConsume},
        {"MoveLockValueIsHalved", MoveLockValueIsHalved},
        {"ChainBouncesNearestSameSide", ChainBouncesNearestSameSide},
        {"ChainKindValidatedAtBoot", ChainKindValidatedAtBoot},
        {"ChainBouncesFromDeadVictimsCell", ChainBouncesFromDeadVictimsCell},
        {"ChainDoesNotRetriggerHooks", ChainDoesNotRetriggerHooks},
        {"ApCapValidation", ApCapValidation},
        {"OneApSkillsAllowTwoPerTurn", OneApSkillsAllowTwoPerTurn},
        {"FirebombIsTileCommitted", FirebombIsTileCommitted},
        {"AiUsesTempoSkills", AiUsesTempoSkills},
        {"RoundCountDeltaReported", RoundCountDeltaReported},
    });
}
