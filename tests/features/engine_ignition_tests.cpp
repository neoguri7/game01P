// WP-25 engine-ignition tests: the pre-hit status trigger, the rune-pair requirement, once-per-run + the
// seed guard, determinism, boot validation of the optional engine table, the journal/run-log feedback and
// the deliberate absence of a battle event.
//
// Structure:
//   * one real-resolver test (PreHitStatusTriggers) — the listener runs through the actual OnHit seam of
//     ActionResolverSystem, so the "pre-hit state" contract is pinned end to end;
//   * unit-level CheckEngineIgnition tests for the decision table (slot rule, party guard, seed guard);
//   * data/boot tests — the shipped seeds vs runes.json, the modified-document boot failures, the run log
//     line, and the headless (no run log module) call.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
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
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEngineIgnition.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FEngineContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace {

using game::ECommandReject;
using game::FCommandResult;
using game::FTurnToken;

// ---------------------------------------------------------------------------------------------
// Filesystem / data helpers (the tempo_skills_tests recipe)
// ---------------------------------------------------------------------------------------------

/// Always the override build-test.ps1 sets (WP-00b), never the user's real save.
std::filesystem::path SaveDirRoot() {
    if (const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
        overrideDir != nullptr && *overrideDir != '\0') {
        return std::filesystem::path(overrideDir);
    }
    return std::filesystem::current_path() / "out" / "test-saves" / "engine_ignition_tests";
}

std::filesystem::path MakeCheckpointPath(std::string_view label) {
    const std::filesystem::path root = SaveDirRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    static int counter = 0;
    return root / ("ignition-" + std::string{label} + "-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                   std::to_string(counter++) + ".json");
}

void RemoveCheckpointFiles(const std::filesystem::path& checkpoint) {
    std::error_code error;
    std::filesystem::remove(checkpoint, error);
    std::filesystem::remove(checkpoint.string() + ".run.json", error);
    std::filesystem::remove(checkpoint.string() + ".run.json.tmp", error);
}

/// A unique throwaway run-log directory under the save dir (never %APPDATA%).
std::filesystem::path MakeRunLogDir(std::string_view label) {
    const std::filesystem::path root = SaveDirRoot() / "engine_ignition_logs";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    static int counter = 0;
    return root / ("log-" + std::string{label} + "-" + std::to_string(counter++) + "-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

/// A unique throwaway asset root with a data/ subdirectory, removed when it goes out of scope.
struct FTempRoot {
    std::filesystem::path root;

    explicit FTempRoot(std::string_view label) {
        static int counter = 0;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("game01p-wp25-" + std::string{label} + "-" + std::to_string(ticks) + "-" + std::to_string(counter++));
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

/// The temp root first, then the frozen fixture and the shipped assets (per-file fallback).
[[nodiscard]] std::vector<std::string> RootsWith(const FTempRoot& temp) {
    std::vector<std::string> roots{temp.root.string()};
    const std::vector<std::string> base = TestAssetRoots();
    roots.insert(roots.end(), base.begin(), base.end());
    return roots;
}

// ---------------------------------------------------------------------------------------------
// Rule helpers
// ---------------------------------------------------------------------------------------------

[[nodiscard]] game::FSkillSlot Slot(std::string skillId, std::vector<std::string> runes) {
    return game::FSkillSlot{std::move(skillId), std::move(runes)};
}

void GiveStatus(entt::registry& registry, entt::entity unit, std::string statusId, int magnitude) {
    auto* active = registry.try_get<game::FActiveStatuses>(unit);
    CHECK(active != nullptr);
    active->effects.push_back(game::FStatusEffect{std::move(statusId), 3, magnitude, false});
}

[[nodiscard]] bool HasStatus(entt::registry& registry, entt::entity unit, std::string_view statusId) {
    const auto* active = registry.try_get<game::FActiveStatuses>(unit);
    if (active == nullptr) return false;
    return std::ranges::any_of(active->effects, [&](const game::FStatusEffect& effect) {
        return std::string_view{effect.statusId} == statusId;
    });
}

[[nodiscard]] bool Ignited(entt::registry& registry, const std::string& engineId) {
    const auto* stats = registry.ctx().find<game::FEngineStats>();
    return stats != nullptr && std::ranges::find(stats->ignited, engineId) != stats->ignited.end();
}

const game::FCombatLog& Log(entt::registry& registry) { return registry.ctx().get<game::FCombatLog>(); }

[[nodiscard]] int CountLines(const std::vector<std::string>& lines, std::string_view needle) {
    return static_cast<int>(std::ranges::count_if(
        lines, [&](const std::string& line) { return line.find(needle) != std::string::npos; }));
}

[[nodiscard]] int EngineIgnitionLines(const game::FRunLog& log) {
    int count = 0;
    for (const auto& event : log.events) {
        if (event.is_object() && event.value("t", std::string{}) == "engine_ignition") ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------------------------
// Hand-built battle fixture (the battle_command_tests recipe, with a run + the fire pair by default)
// ---------------------------------------------------------------------------------------------

struct FIgnitionFixture {
    std::filesystem::path checkpoint;
    game_test::FTestRegistry loaded;
    entt::entity actor{entt::null};
    entt::entity foe{entt::null};

    explicit FIgnitionFixture(std::string_view label, std::vector<game::FSkillSlot> actorSlots)
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
        r.ctx().emplace<game::FBattleEvents>();
        r.ctx().emplace<game::FCommandResults>();
        r.ctx().emplace<game::FCommandOutbox>();
        // The run: the seed guard's input and the run-log gate. A bare run suffices — ignition reads only
        // seed/depth/room from it (no save, no room generation).
        auto& run = r.ctx().emplace<game::FDungeonRun>();
        run.seed = 4242u;
        run.depth = 2;
        run.room = 1;
        actor = MakeUnit("Adam", 5, 5, true, std::move(actorSlots));
        foe = MakeUnit("Bela", 5, 4, false, {});
        r.emplace<game::FTurnParticipant>(actor);
    }

    ~FIgnitionFixture() { RemoveCheckpointFiles(checkpoint); }

    FIgnitionFixture(const FIgnitionFixture&) = delete;
    FIgnitionFixture& operator=(const FIgnitionFixture&) = delete;

    entt::registry& registry() { return loaded.registry; }

    entt::entity MakeUnit(std::string_view tag, int x, int y, bool party, std::vector<game::FSkillSlot> slots) {
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
        r.emplace<game::FSkillLoadout>(unit, game::FSkillLoadout{std::move(slots)});
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

    /// Direct push (no meta) + one resolver drain; returns the recorded result.
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

    /// The next swing of the same turn (the fixture's skill pool is 2 and strike costs 2).
    void RefillSkillAp() { registry().get<game::FActionPoints>(actor).skill = 2; }

    [[nodiscard]] const game::gameplay::FContentRegistry& Content() {
        return loaded.registry.ctx().get<game::gameplay::FContentRegistry>();
    }
};

// ---------------------------------------------------------------------------------------------
// Seed content
// ---------------------------------------------------------------------------------------------

/// UsesExistingRunes: the two shipped engines stand entirely on runes that exist in the shipped
/// runes.json (the WP-16-era freeze id that never existed must not come back), and the statuses they
/// trigger on exist too.
void UsesExistingRunes() {
    const game::FAssetManager assets{ShipAssetRoots()};
    const std::optional<game::gameplay::FContentRegistry> content = game::gameplay::FContentRegistry::load(assets);
    CHECK(content.has_value());
    // WP-22: the engine table grew from the WP-25 seed pair to four archetypes (shock and the
    // ember_kindled alternative path were authored in assets/data/engine_ignition.json).
    CHECK(content->engines.rows.size() == 4);

    std::vector<std::string> ids;
    for (const game::gameplay::FEngineContent& engine : content->engines.rows) {
        CHECK(!engine.requiresRunes.empty());
        CHECK(engine.triggerKind == game::gameplay::kEngineTriggerStatusOnTarget);
        CHECK(!engine.log.empty());
        for (const std::string& runeId : engine.requiresRunes) {
            CHECK(content->runes.find(runeId) != nullptr);
        }
        CHECK(content->statuses.find(engine.triggerStatusId) != nullptr);
        ids.push_back(engine.id);
    }
    std::ranges::sort(ids);
    CHECK((ids == std::vector<std::string>{"engine.ember", "engine.ember_kindled", "engine.frost",
                                           "engine.shock"}));
}

/// AbsentDocumentBootsWithoutEngines: the engine table is the one optional table — a root without the
/// document (the frozen fixture, or a mod one-root boot) loads with an empty table and no ignition, and
/// the silent `resolvePath` probe must keep a legitimate engine-less boot out of the error log.
void AbsentDocumentBootsWithoutEngines() {
    const game::FAssetManager fixtureOnly{{kTestFixtureRoot}};
    const std::optional<game::gameplay::FContentRegistry> content =
        game::gameplay::FContentRegistry::load(fixtureOnly);
    CHECK(content.has_value());
    CHECK(content->engines.rows.empty());
}

// ---------------------------------------------------------------------------------------------
// Pre-hit semantics through the real resolver
// ---------------------------------------------------------------------------------------------

/// PreHitStatusTriggers: the trigger reads the state BEFORE the hit lands. Hit 1 applies burn (fire rune)
/// after OnHit — judging after the application would ignite on hit 1 and say nothing beyond the rune pair.
/// Hit 2 sees the burn pre-hit and ignites. The frost case: freeze is authored magnitude 0 — presence
/// alone must satisfy the trigger.
void PreHitStatusTriggers() {
    // Ember.
    {
        FIgnitionFixture fixture{"prehit-ember", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
        auto& r = fixture.registry();
        const FTurnToken token = fixture.StartTurn();

        CHECK(fixture.Tackle(token, fixture.foe, "skill.strike").reject == ECommandReject::None);
        CHECK(!Ignited(r, "engine.ember"));                 // pre-hit state: no burn yet
        CHECK(HasStatus(r, fixture.foe, "status.burn"));    // applied after the silent ignition check

        fixture.RefillSkillAp();
        CHECK(fixture.Tackle(token, fixture.foe, "skill.strike").reject == ECommandReject::None);
        CHECK(Ignited(r, "engine.ember"));                  // pre-hit state: burn carried in
        CHECK(CountLines(Log(r).lines, "Adam: Ember ignition!") == 1);
    }
    // Frost: cold+frostbite socketed, the target already carries freeze at magnitude 0.
    {
        FIgnitionFixture fixture{"prehit-frost", {Slot("skill.strike", {"rune.cold", "rune.frostbite"})}};
        auto& r = fixture.registry();
        const FTurnToken token = fixture.StartTurn();
        GiveStatus(r, fixture.foe, "status.freeze", 0);

        CHECK(fixture.Tackle(token, fixture.foe, "skill.strike").reject == ECommandReject::None);
        CHECK(Ignited(r, "engine.frost"));
        CHECK(!Ignited(r, "engine.ember"));
        CHECK(CountLines(Log(r).lines, "Adam: Frost ignition!") == 1);
    }
}

/// BurnRefreshStillFiresOnce: re-applying burn (갱신 — statuses never stack) is not a new ignition.
void BurnRefreshStillFiresOnce() {
    FIgnitionFixture fixture{"burn-refresh", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
    auto& r = fixture.registry();
    const FTurnToken token = fixture.StartTurn();

    CHECK(fixture.Tackle(token, fixture.foe, "skill.strike").reject == ECommandReject::None); // applies burn
    fixture.RefillSkillAp();
    CHECK(fixture.Tackle(token, fixture.foe, "skill.strike").reject == ECommandReject::None); // ignites
    CHECK(Ignited(r, "engine.ember"));

    fixture.RefillSkillAp(); // third hit re-applies (refreshes) burn → still exactly one ignition
    CHECK(fixture.Tackle(token, fixture.foe, "skill.strike").reject == ECommandReject::None);
    CHECK(HasStatus(r, fixture.foe, "status.burn"));
    CHECK(CountLines(Log(r).lines, "Ember ignition!") == 1);
}

// ---------------------------------------------------------------------------------------------
// Once per run + the seed guard
// ---------------------------------------------------------------------------------------------

void FiresOncePerRun() {
    FIgnitionFixture fixture{"once-per-run", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
    auto& r = fixture.registry();
    GiveStatus(r, fixture.foe, "status.burn", 3);

    CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);
    CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 0);
    CHECK(CountLines(Log(r).lines, "Ember ignition!") == 1);
}

/// SeedGuardResetsStats: a different run seed is a new run — the record resets and adopts the seed.
void SeedGuardResetsStats() {
    FIgnitionFixture fixture{"seed-guard", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
    auto& r = fixture.registry();
    auto& run = r.ctx().get<game::FDungeonRun>();
    GiveStatus(r, fixture.foe, "status.burn", 3);

    CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);
    CHECK(r.ctx().get<game::FEngineStats>().runSeed == run.seed);

    run.seed += 1; // a new expedition can ignite the same engine again
    CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);
    CHECK(r.ctx().get<game::FEngineStats>().runSeed == run.seed);
    CHECK(r.ctx().get<game::FEngineStats>().ignited.size() == 1);
}

/// SameSeedKeepsStats: the same seed means the same run (a Continue, or the next battle) — the record
/// survives a fresh battle context and suppresses a second line.
void SameSeedKeepsStats() {
    FIgnitionFixture fixture{"same-seed", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
    auto& r = fixture.registry();
    GiveStatus(r, fixture.foe, "status.burn", 3);
    CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);

    // The next battle in the same run: the battle-scoped contexts are replaced, the seed is not.
    r.ctx().erase<game::FCombatLog>();
    r.ctx().emplace<game::FCombatLog>();
    CHECK(r.ctx().get<game::FDungeonRun>().seed == 4242u);
    CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 0);
    CHECK(CountLines(Log(r).lines, "Ember ignition!") == 0); // nothing re-logged in the new battle
}

// ---------------------------------------------------------------------------------------------
// The decision table
// ---------------------------------------------------------------------------------------------

/// NoIgnitionWithoutCombo: missing runes, missing pre-hit status, or a stronger min_magnitude each keep
/// the engine silent.
void NoIgnitionWithoutCombo() {
    // The pair is incomplete: only the element rune.
    {
        FIgnitionFixture fixture{"no-combo-runes", {Slot("skill.strike", {"rune.fire"})}};
        GiveStatus(fixture.registry(), fixture.foe, "status.burn", 3);
        CHECK(game::CheckEngineIgnition(fixture.registry(), fixture.actor, fixture.foe,
                                        fixture.Content(), "skill.strike") == 0);
    }
    // The pair is complete but the target carries no trigger status.
    {
        FIgnitionFixture fixture{"no-combo-status", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
        CHECK(game::CheckEngineIgnition(fixture.registry(), fixture.actor, fixture.foe,
                                        fixture.Content(), "skill.strike") == 0);
    }
    // min_magnitude is a real gate: a hand-raised threshold rejects a weaker burn and accepts a stronger one.
    {
        FIgnitionFixture fixture{"no-combo-min", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
        auto& r = fixture.registry();
        game::gameplay::FContentRegistry content = fixture.Content(); // a copy: the shipped row stays untouched
        CHECK(!content.engines.rows.empty());
        CHECK(content.engines.rows[0].id == "engine.ember");
        content.engines.rows[0].minMagnitude = 5;
        GiveStatus(r, fixture.foe, "status.burn", 4);
        CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, content, "skill.strike") == 0);
        r.get<game::FActiveStatuses>(fixture.foe).effects[0].magnitude = 5;
        CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, content, "skill.strike") == 1);
    }
}

/// PartyOwnerOnly: the enemy can carry the same runes, but its hits never ignite (WP-23 elites stay
/// silent). The party's identical inputs fire.
void PartyOwnerOnly() {
    FIgnitionFixture fixture{"party-only", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
    auto& r = fixture.registry();
    const entt::entity enemy =
        fixture.MakeUnit("Foe actor", 4, 5, false, {Slot("skill.strike", {"rune.fire", "rune.ember"})});
    GiveStatus(r, fixture.actor, "status.burn", 3); // the party member is the target here

    CHECK(game::CheckEngineIgnition(r, enemy, fixture.actor, fixture.Content(), "skill.strike") == 0);

    GiveStatus(r, fixture.foe, "status.burn", 3);
    CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);
}

/// FirstMatchingSlotWins: the acting skill id appears twice; the FIRST slot decides the rune set (the
/// resolver's own rule), so the second slot's pair is invisible to the engine.
void FirstMatchingSlotWins() {
    // First slot = fire pair: the burning target ignites ember even though the second slot could not fire.
    {
        FIgnitionFixture fixture{"first-slot-ember",
                                 {Slot("skill.strike", {"rune.fire", "rune.ember"}),
                                  Slot("skill.strike", {"rune.cold", "rune.frostbite"})}};
        auto& r = fixture.registry();
        GiveStatus(r, fixture.foe, "status.burn", 3);
        GiveStatus(r, fixture.foe, "status.freeze", 0); // both triggers hold; only the first slot is read
        CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);
        CHECK(Ignited(r, "engine.ember"));
        CHECK(!Ignited(r, "engine.frost"));
    }
    // Reversed: the first slot is the cold pair, so the burning target ignites nothing.
    {
        FIgnitionFixture fixture{"first-slot-frost",
                                 {Slot("skill.strike", {"rune.cold", "rune.frostbite"}),
                                  Slot("skill.strike", {"rune.fire", "rune.ember"})}};
        auto& r = fixture.registry();
        GiveStatus(r, fixture.foe, "status.burn", 3);
        CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 0);
    }
}

/// Deterministic: one slot can satisfy both pairs (the direct-call path skips build validation); with both
/// triggers present the engines fire in data order, and two boots on the same seed produce that same order
/// — no RNG anywhere in the verdict.
void Deterministic() {
    const auto fire = [](std::string_view label) {
        FIgnitionFixture fixture{label,
                                 {Slot("skill.strike", {"rune.fire", "rune.ember", "rune.cold", "rune.frostbite"})}};
        auto& r = fixture.registry();
        GiveStatus(r, fixture.foe, "status.burn", 3);
        GiveStatus(r, fixture.foe, "status.freeze", 0);
        CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 2);
        return r.ctx().get<game::FEngineStats>().ignited;
    };
    const std::vector<std::string> first = fire("determinism-a");
    const std::vector<std::string> second = fire("determinism-b");
    CHECK(first == second);
    CHECK((first == std::vector<std::string>{"engine.ember", "engine.frost"})); // data order
}

// ---------------------------------------------------------------------------------------------
// Boot validation and feedback
// ---------------------------------------------------------------------------------------------

/// BadTriggerFailsBoot: a PRESENT engine document is validated like the required tables — a missing
/// required key (loader), unknown trigger kind, unknown rune/status ids, negative min_magnitude, an empty
/// log, an empty rune list and a duplicate id (the loader's row identity check) each stop
/// FContentRegistry::load.
void BadTriggerFailsBoot() {
    const nlohmann::json base = ReadJsonFile("assets/data/engine_ignition.json");
    const auto loads = [&base](const auto& edit) {
        FTempRoot temp{"engine-boot"};
        nlohmann::json document = base;
        edit(document);
        WriteJsonFile(temp.root / "data" / "engine_ignition.json", document);
        return game::gameplay::FContentRegistry::load(game::FAssetManager{RootsWith(temp)}).has_value();
    };

    CHECK(loads([](nlohmann::json&) {})); // the unmodified copy boots
    CHECK(!loads([](nlohmann::json& d) { d["engines"][0]["trigger"]["kind"] = "status_on_attacker"; }));
    CHECK(!loads([](nlohmann::json& d) { d["engines"][0]["requires_runes"] = {"rune.fire", "rune.freeze"}; }));
    CHECK(!loads([](nlohmann::json& d) { d["engines"][0].erase("requires_runes"); })); // required keys, by name
    CHECK(!loads([](nlohmann::json& d) { d["engines"][0].erase("trigger"); }));
    CHECK(!loads([](nlohmann::json& d) { d["engines"][0].erase("log"); }));
    CHECK(!loads([](nlohmann::json& d) { d["engines"][0]["trigger"]["status_id"] = "status.nope"; }));
    CHECK(!loads([](nlohmann::json& d) { d["engines"][0]["trigger"]["min_magnitude"] = -1; }));
    CHECK(!loads([](nlohmann::json& d) { d["engines"][0]["log"] = ""; }));
    CHECK(!loads([](nlohmann::json& d) { d["engines"][1] = d["engines"][0]; })); // duplicate id
}

/// NoBattleEventForIgnition: ignition is journal + run-log feedback only — it queues no battle event, so
/// the WP-05 presentation gate cannot be delayed by a firing (W6 ★7).
void NoBattleEventForIgnition() {
    FIgnitionFixture fixture{"no-event", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
    auto& r = fixture.registry();
    GiveStatus(r, fixture.foe, "status.burn", 3);
    auto& events = r.ctx().get<game::FBattleEvents>();
    const std::size_t before = events.pending.size();
    CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);
    CHECK(events.pending.size() == before);
}

/// IgnitionLoggedToRunLog: exactly one `engine_ignition` line per firing with t/depth/room/round/engine,
/// schema-valid; a second attempt adds none. The headless case (no run-log module set up) must not crash
/// and must still update the stats.
void IgnitionLoggedToRunLog() {
    {
        FIgnitionFixture fixture{"run-log", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
        auto& r = fixture.registry();
        const std::filesystem::path directory = MakeRunLogDir("line");
        game::InitializeRunLog(r, directory.string());

        // The round comes from WP-19a's battle mirror; with no battle running the fixture supplies one.
        auto& stats = r.ctx().emplace<game::FRoomBattleStats>();
        stats.active = true;
        stats.rounds = 3;
        GiveStatus(r, fixture.foe, "status.burn", 3);
        CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);

        const auto* log = r.ctx().find<game::FRunLog>();
        CHECK(log != nullptr);
        CHECK(EngineIgnitionLines(*log) == 1);
        for (const auto& event : log->events) {
            if (!event.is_object() || event.value("t", std::string{}) != "engine_ignition") continue;
            CHECK(game::FRunEventSchemaValid(event));
            CHECK(event["depth"] == 2); // the fixture run
            CHECK(event["room"] == 1);
            CHECK(event["round"] == 3); // FRoomBattleStats, not the turn runtime
            CHECK(event["engine"] == "engine.ember");
        }

        CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 0);
        CHECK(EngineIgnitionLines(*log) == 1); // a second hit adds none

        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    // Headless: no run-log module was ever set up — no crash, stats still updated.
    {
        FIgnitionFixture fixture{"headless", {Slot("skill.strike", {"rune.fire", "rune.ember"})}};
        auto& r = fixture.registry();
        CHECK(!r.ctx().contains<game::FRunLog>()); // nothing was started
        GiveStatus(r, fixture.foe, "status.burn", 3);
        CHECK(game::CheckEngineIgnition(r, fixture.actor, fixture.foe, fixture.Content(), "skill.strike") == 1);
        CHECK(Ignited(r, "engine.ember"));
    }
}

} // namespace

int main() {
    // Save safety: since WP-00b CMake gives every ctest feature test GAME01P_SAVE_DIR=<build>/test-saves/<name>
    // (CMakeLists.txt:137-140), so this fallback only covers running this executable directly, outside
    // ctest — a plain run would otherwise target the user's real save under %APPDATA%. Commit an override
    // before any dungeon service can touch disk.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "engine_ignition_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("engine ignition", {
        {"UsesExistingRunes", UsesExistingRunes},
        {"AbsentDocumentBootsWithoutEngines", AbsentDocumentBootsWithoutEngines},
        {"PreHitStatusTriggers", PreHitStatusTriggers},
        {"BurnRefreshStillFiresOnce", BurnRefreshStillFiresOnce},
        {"FiresOncePerRun", FiresOncePerRun},
        {"SeedGuardResetsStats", SeedGuardResetsStats},
        {"SameSeedKeepsStats", SameSeedKeepsStats},
        {"NoIgnitionWithoutCombo", NoIgnitionWithoutCombo},
        {"PartyOwnerOnly", PartyOwnerOnly},
        {"FirstMatchingSlotWins", FirstMatchingSlotWins},
        {"Deterministic", Deterministic},
        {"BadTriggerFailsBoot", BadTriggerFailsBoot},
        {"NoBattleEventForIgnition", NoBattleEventForIgnition},
        {"IgnitionLoggedToRunLog", IgnitionLoggedToRunLog},
    });
}
