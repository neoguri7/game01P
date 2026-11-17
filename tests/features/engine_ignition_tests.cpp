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
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEngineIgnition.h"
#include "gameplay/combat/FRuneword.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FEngineContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/dungeon/FRunewordInfo.h"
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

    explicit FIgnitionFixture(std::string_view label, std::vector<game::FSkillSlot> actorSlots,
                              std::vector<std::string> roots = TestAssetRoots())
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
/// document (a mod one-root boot) loads with an empty table and no ignition, and the silent `resolvePath`
/// probe must keep a legitimate engine-less boot out of the error log. The frozen fixture carries a copy
/// of the shipped document since WP-00d, so this root is built explicitly from every fixture *.json
/// except engine_ignition.json.
void AbsentDocumentBootsWithoutEngines() {
    FTempRoot temp{"no-engines"};
    const std::filesystem::path fixtureData = std::filesystem::path(kTestFixtureRoot) / "data";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(fixtureData)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        if (entry.path().filename() == "engine_ignition.json") continue;  // the absent document under test
        std::error_code error;
        std::filesystem::copy_file(entry.path(), temp.root / "data" / entry.path().filename(),
                                   std::filesystem::copy_options::overwrite_existing, error);
        CHECK(!error);
    }

    const game::FAssetManager engineLessRoot{{temp.root.string()}};
    const std::optional<game::gameplay::FContentRegistry> content =
        game::gameplay::FContentRegistry::load(engineLessRoot);
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

// ---------------------------------------------------------------------------------------------
// WP-45 runewords
// ---------------------------------------------------------------------------------------------

const char* const kEmberText = "Hitting a burning enemy deals half the burn's remaining damage to every enemy next to it";
const char* const kFrostText = "Hitting a frozen enemy freezes one enemy next to it for 1 turn";
const char* const kArcText = "Hitting a shocked enemy deals half that hit to every other shocked enemy";
const char* const kKindledText = "Killing a burning enemy moves its burn to the nearest enemy that is not burning";

/// The fixture's engine document with the section-4 name, text and effect set on each row.
[[nodiscard]] nlohmann::json RunewordEngines() {
    nlohmann::json document = ReadJsonFile(std::filesystem::path{kTestFixtureRoot} / "data" / "engine_ignition.json");
    const auto set = [&document](std::size_t index, const char* name, nlohmann::json effect, const char* text) {
        document["engines"][index]["name"] = name;
        document["engines"][index]["effect"] = std::move(effect);
        document["engines"][index]["text"] = text;
    };
    set(0, "Ember Engine", {{"kind", "detonate"}, {"percent", 50}}, kEmberText);
    set(1, "Frost Lock", {{"kind", "spread"}, {"turns", 1}}, kFrostText);
    set(2, "Arc", {{"kind", "arc"}, {"percent", 50}}, kArcText);
    set(3, "Kindled", {{"kind", "carry"}}, kKindledText);
    return document;
}

/// A temp root holding RunewordEngines(), declared before the fixture that boots from it.
struct FRunewordFixture {
    FTempRoot temp;
    FIgnitionFixture fixture;

    static std::string_view Prepare(const FTempRoot& temp, std::string_view label) {
        WriteJsonFile(temp.root / "data" / "engine_ignition.json", RunewordEngines());
        return label;
    }

    FRunewordFixture(std::string_view label, std::vector<game::FSkillSlot> actorSlots)
        : temp(label), fixture(Prepare(temp, label), std::move(actorSlots), RootsWith(temp)) {}
};

[[nodiscard]] int Hp(entt::registry& registry, entt::entity unit) { return registry.get<game::FHealth>(unit).current; }

[[nodiscard]] const game::FStatusEffect* FindStatus(entt::registry& registry, entt::entity unit, std::string_view id) {
    const auto* active = registry.try_get<game::FActiveStatuses>(unit);
    if (active == nullptr) return nullptr;
    for (const game::FStatusEffect& effect : active->effects) {
        if (std::string_view{effect.statusId} == id) return &effect;
    }
    return nullptr;
}

[[nodiscard]] std::string DisplayName(const game::gameplay::FContentRegistry& content, std::string_view runeId) {
    const auto* rune = content.runes.find(std::string{runeId});
    return rune != nullptr ? rune->displayName : std::string{runeId};
}

void RunewordEffectSchema() {
    const auto loadWith = [](const auto& edit) {
        FTempRoot temp{"rw-schema"};
        nlohmann::json document = RunewordEngines();
        edit(document);
        WriteJsonFile(temp.root / "data" / "engine_ignition.json", document);
        return game::gameplay::FContentRegistry::load(game::FAssetManager{RootsWith(temp)});
    };
    {
        const auto content = loadWith([](nlohmann::json&) {});
        CHECK(content.has_value());
        const auto* ember = content->engines.find("engine.ember");
        CHECK(ember != nullptr);
        CHECK(ember->effectKind == "detonate");
        CHECK(ember->effectPercent == 50);
        CHECK(!ember->text.empty());
        const auto* frost = content->engines.find("engine.frost");
        CHECK(frost != nullptr);
        CHECK(frost->effectTurns == 1);
    }
    {
        const game::FAssetManager plain{TestAssetRoots()};
        const auto content = game::gameplay::FContentRegistry::load(plain);
        CHECK(content.has_value());
        for (const auto& engine : content->engines.rows) CHECK(engine.effectKind.empty());
    }
    CHECK(!loadWith([](nlohmann::json& d) { d["engines"][0]["effect"]["kind"] = "boom"; }).has_value());
    CHECK(!loadWith([](nlohmann::json& d) { d["engines"][0].erase("text"); }).has_value());
    CHECK(!loadWith([](nlohmann::json& d) { d["engines"][0]["effect"]["percent"] = 0; }).has_value());
    CHECK(!loadWith([](nlohmann::json& d) { d["engines"][0]["effect"]["percent"] = 1001; }).has_value());
    CHECK(!loadWith([](nlohmann::json& d) { d["engines"][0]["effect"].erase("percent"); }).has_value());
    CHECK(!loadWith([](nlohmann::json& d) { d["engines"][1]["effect"]["turns"] = 0; }).has_value());
    CHECK(!loadWith([](nlohmann::json& d) { d["engines"][1]["effect"]["turns"] = 11; }).has_value());
    CHECK(!loadWith([](nlohmann::json& d) { d["engines"][0]["requires_runes"] = {"rune.fire"}; }).has_value());
}

void ShipRunewordData() {
    const nlohmann::json engines = ReadJsonFile("assets/data/engine_ignition.json");
    const nlohmann::json expected = RunewordEngines();
    CHECK(engines["engines"].size() == 4);
    for (std::size_t index = 0; index < 4; ++index) {
        const auto& row = engines["engines"][index];
        CHECK(row["id"] == expected["engines"][index]["id"]);
        CHECK(row["name"] == expected["engines"][index]["name"]);
        CHECK(row["effect"] == expected["engines"][index]["effect"]);
        CHECK(row["text"] == expected["engines"][index]["text"]);
    }
    const std::vector<std::string> catalysts{"rune.fire", "rune.cold", "rune.lightning", "rune.ember",
                                             "rune.frostbite", "rune.shock", "rune.duration"};
    const std::vector<std::string> retired{"rune.power", "rune.inferno", "rune.glacial"};
    const nlohmann::json runes = ReadJsonFile("assets/data/runes.json");
    int catalystCount = 0;
    int retiredCount = 0;
    for (const auto& row : runes["runes"]) {
        const std::string id = row["id"].get<std::string>();
        const std::string kind = row.value("kind", std::string{});
        if (kind == "catalyst") {
            ++catalystCount;
            CHECK(std::ranges::find(catalysts, id) != catalysts.end());
        } else if (kind == "retired") {
            ++retiredCount;
            CHECK(std::ranges::find(retired, id) != retired.end());
        } else {
            CHECK(kind.empty());
            CHECK(std::ranges::find(catalysts, id) == catalysts.end());
            CHECK(std::ranges::find(retired, id) == retired.end());
        }
    }
    CHECK(catalystCount == 7);
    CHECK(retiredCount == 3);
    const std::string unlocks = ReadJsonFile("assets/data/unlocks.json").dump();
    for (const std::string& id : retired) CHECK(unlocks.find("\"" + id + "\"") == std::string::npos);
}

void ActiveRunewordIsFirstComplete() {
    FRunewordFixture f{"rw-first", {}};
    const auto& content = f.fixture.Content();
    const auto active = [&content](std::vector<std::string> runes) -> std::string {
        const game::FSkillSlot slot = Slot("skill.strike", std::move(runes));
        const auto* engine = game::ActiveRuneword(content, slot);
        return engine != nullptr ? engine->id : std::string{};
    };
    CHECK(active({"rune.fire", "rune.ember"}) == "engine.ember");
    CHECK(active({"rune.fire", "rune.duration"}) == "engine.ember_kindled");
    CHECK(active({"rune.fire", "rune.ember", "rune.duration"}) == "engine.ember");
    CHECK(active({"rune.ember", "rune.fire"}) == "engine.ember");
    CHECK(active({"rune.fire"}).empty());
    CHECK(active({}).empty());
    FIgnitionFixture plain{"rw-first-plain", {}};
    const game::FSkillSlot slot = Slot("skill.strike", {"rune.fire", "rune.ember"});
    CHECK(game::ActiveRuneword(plain.Content(), slot) == nullptr);
}

void EmberEngineBurnsNeighbours() {
    const auto emberSlots = [] { return std::vector<game::FSkillSlot>{Slot("skill.strike", {"rune.fire", "rune.ember"})}; };
    {
        FRunewordFixture f{"rw-ember", emberSlots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity near = fx.MakeUnit("near", 5, 3, false, {});
        const entt::entity far = fx.MakeUnit("far", 5, 2, false, {});
        GiveStatus(r, fx.foe, "status.burn", 4);
        (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
        CHECK(Hp(r, near) == 50 - 6);
        CHECK(Hp(r, far) == 50);
        CHECK(CountLines(Log(r).lines, "Ember Engine scorches 1 nearby for 6") == 1);
    }
    {
        FRunewordFixture f{"rw-ember-noburn", emberSlots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity near = fx.MakeUnit("near", 5, 3, false, {});
        (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
        CHECK(Hp(r, near) == 50);
        CHECK(CountLines(Log(r).lines, "Ember Engine scorches") == 0);
    }
    {
        FRunewordFixture f{"rw-ember-alone", emberSlots()};
        auto& fx = f.fixture;
        GiveStatus(fx.registry(), fx.foe, "status.burn", 4);
        (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
        CHECK(CountLines(Log(fx.registry()).lines, "Ember Engine scorches") == 0);
    }
    {
        FRunewordFixture f{"rw-ember-half", {Slot("skill.strike", {"rune.fire"})}};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity near = fx.MakeUnit("near", 5, 3, false, {});
        GiveStatus(r, fx.foe, "status.burn", 4);
        (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
        CHECK(Hp(r, near) == 50);
    }
    {
        FRunewordFixture f{"rw-ember-enemy", emberSlots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity raider = fx.MakeUnit("Raid", 6, 3, false, {Slot("skill.strike", {"rune.fire", "rune.ember"})});
        const entt::entity near = fx.MakeUnit("near", 5, 3, false, {});
        const entt::entity third = fx.MakeUnit("third", 4, 3, false, {});
        GiveStatus(r, near, "status.burn", 4);
        game::RunewordOnHit(r, {.owner = raider, .other = near, .amount = 5, .skillId = "skill.strike"});
        CHECK(Hp(r, near) == 50);
        CHECK(Hp(r, third) == 50);
        CHECK(Hp(r, raider) == 50);
        CHECK(Hp(r, fx.foe) == 50);
    }
}

void FrostLockSpreadsFreeze() {
    const auto slots = [] { return std::vector<game::FSkillSlot>{Slot("skill.strike", {"rune.cold", "rune.frostbite"})}; };
    {
        FRunewordFixture f{"rw-frost", slots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity a = fx.MakeUnit("a", 5, 3, false, {});
        const entt::entity b = fx.MakeUnit("b", 4, 4, false, {});
        GiveStatus(r, fx.foe, "status.freeze", 0);
        (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
        const entt::entity lower = entt::to_integral(a) < entt::to_integral(b) ? a : b;
        const entt::entity upper = lower == a ? b : a;
        const auto* frozen = FindStatus(r, lower, "status.freeze");
        CHECK(frozen != nullptr);
        if (frozen != nullptr) CHECK(frozen->turnsRemaining == 1);
        CHECK(!HasStatus(r, upper, "status.freeze"));
        CHECK(CountLines(Log(r).lines, "Frost Lock spreads") == 1);
    }
    {
        FRunewordFixture f{"rw-frost-both", slots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity a = fx.MakeUnit("a", 5, 3, false, {});
        const entt::entity b = fx.MakeUnit("b", 4, 4, false, {});
        GiveStatus(r, fx.foe, "status.freeze", 0);
        GiveStatus(r, a, "status.freeze", 0);
        GiveStatus(r, b, "status.freeze", 0);
        (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
        CHECK(CountLines(Log(r).lines, "Frost Lock spreads") == 0);
    }
    {
        FRunewordFixture f{"rw-frost-immune", slots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity a = fx.MakeUnit("a", 5, 3, false, {});
        const entt::entity b = fx.MakeUnit("b", 4, 4, false, {});
        GiveStatus(r, fx.foe, "status.freeze", 0);
        const entt::entity lower = entt::to_integral(a) < entt::to_integral(b) ? a : b;
        const entt::entity upper = lower == a ? b : a;
        r.get<game::FActiveStatuses>(lower).immune.push_back("status.freeze");
        (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
        CHECK(!HasStatus(r, lower, "status.freeze"));
        CHECK(HasStatus(r, upper, "status.freeze"));
    }
}

void ArcHitsOtherShocked() {
    FRunewordFixture f{"rw-arc", {Slot("skill.strike", {"rune.lightning", "rune.shock"})}};
    auto& fx = f.fixture;
    auto& r = fx.registry();
    r.get<game::FStats>(fx.foe) = game::FStats{1, 1, 0, 0};
    const entt::entity p = fx.MakeUnit("p", 9, 9, false, {});
    const entt::entity q = fx.MakeUnit("q", 9, 8, false, {});
    GiveStatus(r, p, "status.paralyze", 50);
    GiveStatus(r, fx.foe, "status.paralyze", 50);
    (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
    const int dealt = 50 - Hp(r, fx.foe);
    CHECK(dealt >= 4);
    CHECK(50 - Hp(r, p) == dealt * 50 / 100);
    CHECK(Hp(r, q) == 50);
    CHECK(CountLines(Log(r).lines, "Arc arcs to 1 for ") == 1);
}

void KindledCarriesBurn() {
    FRunewordFixture f{"rw-kindled", {Slot("skill.strike", {"rune.fire", "rune.duration"})}};
    auto& fx = f.fixture;
    auto& r = fx.registry();
    r.get<game::FHealth>(fx.foe).current = 1;
    r.get<game::FActiveStatuses>(fx.foe).effects.push_back(game::FStatusEffect{"status.burn", 2, 7, false});
    const entt::entity n = fx.MakeUnit("n", 5, 2, false, {});
    const entt::entity burning = fx.MakeUnit("burning", 5, 3, false, {});
    const entt::entity m = fx.MakeUnit("m", 9, 9, false, {});
    r.get<game::FActiveStatuses>(burning).effects.push_back(game::FStatusEffect{"status.burn", 3, 9, false});
    // `burning` is the nearest enemy but already burns, so the burn goes to the next nearest, `n`.
    (void)fx.Tackle(fx.StartTurn(), fx.foe, "skill.strike");
    CHECK(r.all_of<game::FDead>(fx.foe));
    const auto* carried = FindStatus(r, n, "status.burn");
    CHECK(carried != nullptr);
    if (carried != nullptr) {
        CHECK(carried->turnsRemaining == 2);
        CHECK(carried->magnitude == 7);
    }
    const auto* kept = FindStatus(r, burning, "status.burn");
    CHECK(kept != nullptr);
    if (kept != nullptr) CHECK(kept->magnitude == 9);
    CHECK(!HasStatus(r, m, "status.burn"));
    CHECK(CountLines(Log(r).lines, "Kindled carries") == 1);
}

void RunewordActsOncePerSkillUse() {
    const auto slots = [] { return std::vector<game::FSkillSlot>{Slot("skill.strike", {"rune.fire", "rune.ember"})}; };
    {
        FRunewordFixture f{"rw-once", slots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity near = fx.MakeUnit("near", 5, 3, false, {});
        GiveStatus(r, fx.foe, "status.burn", 4);
        const game::FHookContext context{.owner = fx.actor, .other = fx.foe, .amount = 5, .skillId = "skill.strike"};
        game::BeginRunewordAction(r);
        game::RunewordOnHit(r, context);
        CHECK(Hp(r, near) == 50 - 6);
        game::RunewordOnHit(r, context);
        CHECK(Hp(r, near) == 50 - 6);
        game::BeginRunewordAction(r);
        game::RunewordOnHit(r, context);
        CHECK(Hp(r, near) == 50 - 12);
    }
    {
        FRunewordFixture f{"rw-once-empty", slots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        GiveStatus(r, fx.foe, "status.burn", 4);
        game::BeginRunewordAction(r);
        game::RunewordOnHit(r, {.owner = fx.actor, .other = fx.foe, .amount = 5, .skillId = "skill.strike"});
        const auto* guard = r.ctx().find<game::FRunewordGuard>();
        CHECK(guard != nullptr);
        if (guard != nullptr) CHECK(!guard->fired);
    }
    {
        FRunewordFixture f{"rw-once-resolver", slots()};
        auto& fx = f.fixture;
        auto& r = fx.registry();
        const entt::entity near = fx.MakeUnit("near", 5, 3, false, {});
        GiveStatus(r, fx.foe, "status.burn", 4);
        const FTurnToken token = fx.StartTurn();
        (void)fx.Tackle(token, fx.foe, "skill.strike");
        CHECK(Hp(r, near) == 50 - 6);
        fx.RefillSkillAp();
        (void)fx.Tackle(token, fx.foe, "skill.strike");
        CHECK(Hp(r, near) == 50 - 6 - 7);
        CHECK(CountLines(Log(r).lines, "Ember Engine scorches") == 2);
    }
}

void RetiredRuneNeverOffered() {
    FTempRoot temp{"rw-retired"};
    nlohmann::json runes = ReadJsonFile(std::filesystem::path{kTestFixtureRoot} / "data" / "runes.json");
    bool marked = false;
    for (auto& row : runes["runes"]) {
        if (row["id"] == "rune.power") {
            row["kind"] = "retired";
            marked = true;
        }
    }
    CHECK(marked);
    WriteJsonFile(temp.root / "data" / "runes.json", runes);
    const auto collect = [](FIgnitionFixture& fixture) {
        auto& r = fixture.registry();
        std::vector<std::string> seen;
        for (int room = 0; room < 200; ++room) {
            r.ctx().get<game::FDungeonRun>().room = room;
            for (const std::string& id : game::PeekRuneCandidates(r)) seen.push_back(id);
        }
        return seen;
    };
    FIgnitionFixture a{"rw-retired-a", {}, RootsWith(temp)};
    const auto seenA = collect(a);
    CHECK(!seenA.empty());
    CHECK(std::ranges::find(seenA, "rune.power") == seenA.end());
    FIgnitionFixture b{"rw-retired-b", {}};
    const auto seenB = collect(b);
    CHECK(std::ranges::find(seenB, "rune.power") != seenB.end());
}

void RunewordCardLine() {
    FRunewordFixture f{"rw-card", {}};
    auto& r = f.fixture.registry();
    auto& run = r.ctx().get<game::FDungeonRun>();
    run.runes = {};
    CHECK(game::RunewordCardLine(r, "rune.fire") == "Runeword ingredient");
    CHECK(game::RunewordCardLine(r, "rune.range").empty());
    run.runes = {"rune.ember"};
    CHECK(game::RunewordCardLine(r, "rune.fire") == std::string{"Runeword: completes Ember Engine: "} + kEmberText);
    run.runes = {"rune.duration"};
    CHECK(game::RunewordCardLine(r, "rune.fire") == std::string{"Runeword: completes Kindled: "} + kKindledText);
    run.runes = {"rune.ember", "rune.duration"};
    CHECK(game::RunewordCardLine(r, "rune.fire") == std::string{"Runeword: completes Ember Engine: "} + kEmberText);
    run.runes = {};
    game::FRunMember member;
    member.id = "m1";
    member.skills = {Slot("skill.strike", {"rune.ember"})};
    run.party = {member};
    CHECK(game::RunewordCardLine(r, "rune.fire") == std::string{"Runeword: completes Ember Engine: "} + kEmberText);
}

void RunewordSlotAndRecipeLines() {
    FRunewordFixture f{"rw-lines", {}};
    auto& r = f.fixture.registry();
    const auto& content = f.fixture.Content();
    CHECK(game::RunewordSlotLine(content, Slot("skill.strike", {"rune.fire", "rune.ember"})) ==
          std::string{"Runeword: Ember Engine: "} + kEmberText);
    CHECK(game::RunewordSlotLine(content, Slot("skill.strike", {"rune.fire"})) ==
          "Runeword 1/2: Ember Engine needs " + DisplayName(content, "rune.ember"));
    CHECK(game::RunewordSlotLine(content, Slot("skill.strike", {"rune.range"})).empty());

    auto& run = r.ctx().get<game::FDungeonRun>();
    run.runes = {};
    run.party.clear();
    CHECK(game::KnownRunewords(r).empty());
    run.runes = {"rune.shock"};
    {
        const auto known = game::KnownRunewords(r);
        CHECK(known.size() == 1);
        if (known.size() == 1) {
            CHECK(known[0].id == "engine.shock");
            CHECK(game::RunewordRecipeLine(known[0]) == "Arc: " + DisplayName(content, "rune.lightning") + " + " +
                                                          DisplayName(content, "rune.shock") + ": " + kArcText);
        }
    }
    run.runes = {"rune.fire"};
    {
        const auto known = game::KnownRunewords(r);
        CHECK(known.size() == 2);
        if (known.size() == 2) {
            CHECK(known[0].id == "engine.ember");
            CHECK(known[1].id == "engine.ember_kindled");
        }
    }
    run.runes = {};
    game::FRunMember member;
    member.id = "m1";
    member.skills = {Slot("skill.strike", {"rune.cold"})};
    run.party = {member};
    {
        const auto known = game::KnownRunewords(r);
        CHECK(known.size() == 1);
        if (known.size() == 1) CHECK(known[0].id == "engine.frost");
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
        {"RunewordEffectSchema", RunewordEffectSchema},
        {"ShipRunewordData", ShipRunewordData},
        {"ActiveRunewordIsFirstComplete", ActiveRunewordIsFirstComplete},
        {"EmberEngineBurnsNeighbours", EmberEngineBurnsNeighbours},
        {"FrostLockSpreadsFreeze", FrostLockSpreadsFreeze},
        {"ArcHitsOtherShocked", ArcHitsOtherShocked},
        {"KindledCarriesBurn", KindledCarriesBurn},
        {"RunewordActsOncePerSkillUse", RunewordActsOncePerSkillUse},
        {"RetiredRuneNeverOffered", RetiredRuneNeverOffered},
        {"RunewordCardLine", RunewordCardLine},
        {"RunewordSlotAndRecipeLines", RunewordSlotAndRecipeLines},
    });
}
