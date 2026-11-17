// WP-28 Phase A tests: the debuff sign rule through the real resolver and the AI valuation (A1), the camp
// hoard metric's purchasable-only denominator (A2) and the GAME01P_SEED parser/sequencer (A3).
//
// The debuff resolver tests run on SHIP content (the shipped statuses.json `-2` row is the subject); the
// AI test mirrors the status_rules_tests pattern (frozen fixture content, hand-built FUnitView); the camp
// tests boot the fixture roots like camp_tests and assert structure, never price constants.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "core/AssetManager.h"
#include "core/Logger.h"
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
#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiQueries.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/ai/FAiWorld.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FRuneContent.h"
#include "gameplay/data/FStatusContent.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSeed.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace {

using game::ECommandReject;
using game::FCommandResult;
using game::FTurnToken;

// --- shared paths -------------------------------------------------------------------------------

[[nodiscard]] std::filesystem::path SaveDirRoot() {
    if (const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
        overrideDir != nullptr && *overrideDir != '\0') {
        return std::filesystem::path(overrideDir);
    }
    return std::filesystem::current_path() / "out" / "test-saves" / "tuning_tests";
}

[[nodiscard]] std::string UniqueCheckpoint(std::string_view label) {
    static int counter = 0;
    const std::filesystem::path root = SaveDirRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return (root / ("tuning-" + std::string{label} + "-" + std::to_string(ticks) + "-" +
                    std::to_string(counter++) + ".json")).string();
}

void RemoveSaveFiles(const std::string& checkpoint, const std::string& runSave) {
    std::error_code error;
    std::filesystem::remove(checkpoint, error);
    std::filesystem::remove(runSave, error);
    std::filesystem::remove(runSave + ".tmp", error);
}

[[nodiscard]] std::optional<game::gameplay::FContentRegistry> LoadContent(const std::vector<std::string>& roots) {
    const game::FAssetManager assets(roots);
    return game::gameplay::FContentRegistry::load(assets);
}

/// Captures spdlog output while alive (the camp entry report line is the subject).
struct FLogCapture {
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger;

    FLogCapture() {
        logger = std::make_shared<spdlog::logger>(
            "tuning-test-capture", std::make_shared<spdlog::sinks::ostream_sink_mt>(stream));
        logger->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger);
    }
    ~FLogCapture() { SetCurrentGameLogger(nullptr); }
    [[nodiscard]] std::string Text() const { return stream.str(); }
};

// --- A1: hand-built battle on ship content (the content_a_tests fixture recipe) -----------------

[[nodiscard]] game::FSkillSlot Slot(std::string skillId, std::vector<std::string> runes = {}) {
    return game::FSkillSlot{std::move(skillId), std::move(runes)};
}

[[nodiscard]] game::FStatusEffect* FindStatus(
    entt::registry& registry, entt::entity unit, std::string_view statusId) {
    auto* active = registry.try_get<game::FActiveStatuses>(unit);
    if (active == nullptr) {
        return nullptr;
    }
    for (game::FStatusEffect& effect : active->effects) {
        if (std::string_view{effect.statusId} == statusId) {
            return &effect;
        }
    }
    return nullptr;
}

/// Content + the minimal battle contexts on an empty grid: every unit is 50 HP, the party hits for
/// `offense + power - 2` and the enemies for `6 + power - 2` (so the tests can name exact numbers).
struct FBattleFixture {
    std::string checkpoint;
    game_test::FTestRegistry loaded;
    std::string runSave;
    entt::entity actor{entt::null};
    entt::entity foe{entt::null};

    FBattleFixture(std::string_view label, const std::vector<game::FSkillSlot>& actorSlots,
                   const std::vector<game::FSkillSlot>& foeSlots)
        : checkpoint(UniqueCheckpoint(label)),
          loaded(game_test::LoadTestRegistry(ShipAssetRoots(), checkpoint)) {
        CHECK(loaded.content);
        CHECK(loaded.dungeon);
        runSave = registry().ctx().get<game::FDungeonSave>().path;
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
        // A bare run: some gameplay readers guard on it (engine seed guard, sheet refresh branch).
        auto& run = r.ctx().emplace<game::FDungeonRun>();
        run.seed = 4242u;
        run.depth = 1;
        run.room = 0;
        actor = MakeUnit("Adam", 5, 5, true, game::FStats{10, 10, 2, 2}, actorSlots);
        foe = MakeUnit("Bela", 5, 4, false, game::FStats{6, 6, 2, 2}, foeSlots);
        r.emplace<game::FTurnParticipant>(actor);
    }

    ~FBattleFixture() { RemoveSaveFiles(checkpoint, runSave); }

    FBattleFixture(const FBattleFixture&) = delete;
    FBattleFixture& operator=(const FBattleFixture&) = delete;

    entt::registry& registry() { return loaded.registry; }

    entt::entity MakeUnit(std::string_view tag, int x, int y, bool party, game::FStats stats,
                          const std::vector<game::FSkillSlot>& slots) {
        auto& r = registry();
        const entt::entity unit = r.create();
        r.emplace<game::FTag>(unit, game::FTag{std::string(tag)});
        r.emplace<game::FStats>(unit, stats);
        r.emplace<game::FHealth>(unit, game::FHealth{50, 50});
        r.emplace<game::FGridPosition>(unit, game::FGridPosition{x, y});
        r.emplace<FPosition>(unit, FPosition{0.f, 0.f});
        r.emplace<game::FActionPoints>(unit, game::FActionPoints{1, 1, 2, 2});
        r.emplace<game::FSpeed>(unit, game::FSpeed{10});
        r.emplace<game::FActiveStatuses>(unit);
        r.emplace<game::FSkillLoadout>(unit, game::FSkillLoadout{slots});
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

    /// Direct push (the pre-API path) + one resolver drain; returns the recorded result.
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

    [[nodiscard]] const game::gameplay::FContentRegistry& Content() {
        return loaded.registry.ctx().get<game::gameplay::FContentRegistry>();
    }
};

/// DebuffLowersDamageThroughResolver: rune.guard puts status.debuff (authored -2) on the attacker through
/// the real hook/resolver path; the very next strike then lands exactly 2 less. The second hit recomputes
/// the attack modifier from the live FActiveStatuses inside PreviewSkill, so this is the production
/// damage path, not a direct-formula check.
void DebuffLowersDamageThroughResolver() {
    FBattleFixture fixture{"debuff-damage", {Slot("skill.strike")}, {Slot("skill.strike", {"rune.guard"})}};
    const FTurnToken token = fixture.StartTurn();

    auto& r = fixture.registry();
    const game::gameplay::FStatusContent* definition = fixture.Content().statuses.find("status.debuff");
    CHECK(definition != nullptr);
    CHECK(definition->magnitude < 0); // the authored weakening value the sign rule must preserve

    const int foeHpBefore = r.get<game::FHealth>(fixture.foe).current;
    CHECK(fixture.Tackle(token, fixture.foe, "skill.strike").reject == ECommandReject::None);
    const int cleanDamage = foeHpBefore - r.get<game::FHealth>(fixture.foe).current;
    const game::FStatusEffect* debuff = FindStatus(r, fixture.actor, "status.debuff");
    CHECK(debuff != nullptr);
    CHECK(debuff->magnitude == definition->magnitude);

    r.get<game::FActionPoints>(fixture.actor).skill = 2; // one more strike inside the same turn
    const int hpBeforeSecond = r.get<game::FHealth>(fixture.foe).current;
    CHECK(fixture.Tackle(token, fixture.foe, "skill.strike").reject == ECommandReject::None);
    const int weakenedDamage = hpBeforeSecond - r.get<game::FHealth>(fixture.foe).current;
    CHECK(cleanDamage > 0);
    CHECK(weakenedDamage == cleanDamage + definition->magnitude); // exactly |magnitude| lower
    CHECK(weakenedDamage >= game::kMinimumDamage);                 // the damage floor still applies
}

/// DebuffMagnitudeSignRuleAndRuneDelta: the rule itself — m >= 0 keeps `max(0, m + delta)` byte-identically;
/// a negative m keeps its sign and `min(0, m - delta)` grows |m| with a positive rune delta; and a rune
/// `status_magnitude_delta` really reaches stored state through ResolveSkill -> ApplyStatus.
void DebuffMagnitudeSignRuleAndRuneDelta() {
    const std::optional<game::gameplay::FContentRegistry> content = LoadContent(ShipAssetRoots());
    CHECK(content.has_value());
    const auto* debuff = content->statuses.find("status.debuff");
    CHECK(debuff != nullptr);
    CHECK(debuff->magnitude == -2);

    CHECK(game::EffectiveStatusMagnitude(*debuff, 0) == -2);
    CHECK(game::EffectiveStatusMagnitude(*debuff, +1) == -3);
    CHECK(game::EffectiveStatusMagnitude(*debuff, +5) == -7);
    CHECK(game::EffectiveStatusMagnitude(*debuff, -1) == -1);
    CHECK(game::EffectiveStatusMagnitude(*debuff, -5) == 0); // weakened to nothing, never flipped positive
    CHECK(game::EffectiveStatusMagnitude(*debuff, -2) == 0);

    // A synthetic rune (no ship rune targets status.debuff on this freeze date): the magnitude delta flows
    // through the real resolve path and the stored effect is the stronger |m|.
    const game::gameplay::FSkillContent* skill = content->skills.find("skill.debuff");
    CHECK(skill != nullptr);
    game::gameplay::FRuneContent rune;
    rune.id = "test.rune.magnify_debuff";
    rune.statusId = "status.debuff";
    rune.statusMagnitudeDelta = 1;
    const game::FResolvedSkill resolved = game::ResolveSkill(*skill, {&rune});
    CHECK(resolved.statuses.size() == 1);
    CHECK(resolved.statuses.front().statusId == "status.debuff");
    CHECK(resolved.statuses.front().magnitudeDelta == 1);
    game::FActiveStatuses active;
    game::ApplyStatus(
        active, *debuff, resolved.statuses.front().durationDelta, resolved.statuses.front().magnitudeDelta);
    CHECK(active.effects.size() == 1);
    CHECK(active.effects.front().magnitude == -3);

    // Buff/burn/freeze: everything with magnitude >= 0 is unchanged by the fix.
    const auto* buff = content->statuses.find("status.buff");
    const auto* burn = content->statuses.find("status.burn");
    const auto* freeze = content->statuses.find("status.freeze");
    CHECK(buff != nullptr && burn != nullptr && freeze != nullptr);
    CHECK(game::EffectiveStatusMagnitude(*buff, 0) == 2);
    CHECK(game::EffectiveStatusMagnitude(*buff, +2) == 4);
    CHECK(game::EffectiveStatusMagnitude(*buff, -1) == 1);
    CHECK(game::EffectiveStatusMagnitude(*buff, -5) == 0);
    CHECK(game::EffectiveStatusMagnitude(*burn, 0) == 3);
    CHECK(game::EffectiveStatusMagnitude(*burn, +2) == 5);
    CHECK(game::EffectiveStatusMagnitude(*burn, -5) == 0);
    CHECK(game::EffectiveStatusMagnitude(*freeze, 0) == 0); // "magnitude 0 = always" stays
    CHECK(game::EffectiveStatusMagnitude(*freeze, +1) == 1);
}

// --- A1: the AI valuation ------------------------------------------------------------------------

/// The AI side (the status_rules_tests pattern): loaded content plus an empty registry/world —
/// ValueOfStatus reads only the content tables, the tuning and the target view handed to it.
struct FAiFixture {
    game::gameplay::FContentRegistry content;
    entt::registry registry;
    game::ai::FAiBlackboard blackboard;
    game::ai::FBtMemory memory;
    game::ai::FAiTuning tuning;
    game::ai::FAiWorld world;

    FAiFixture() {
        std::optional<game::gameplay::FContentRegistry> loaded = LoadContent(TestAssetRoots());
        CHECK(loaded.has_value());
        content = std::move(*loaded);
    }

    [[nodiscard]] game::ai::FBtContext Context() {
        return {registry, content, tuning, world, blackboard, memory, entt::null, {}, false};
    }
};

/// DebuffValueIsPositiveOnEnemies: |m| × gain to the caster for a hostile weakening, negative on an ally,
/// 0 for a unit that never attacks and 0 on re-application (mirrors the buff branch).
void DebuffValueIsPositiveOnEnemies() {
    FAiFixture fixture;
    const game::gameplay::FStatusContent* debuff = fixture.content.statuses.find("status.debuff");
    CHECK(debuff != nullptr);
    CHECK(debuff->magnitude == -2 && debuff->durationTurns == 3);

    game::ai::FUnitView target;
    target.hp = target.hpMax = 30;
    target.threat = 10;
    target.canDamage = true;
    auto ctx = fixture.Context();
    CHECK(game::ai::ValueOfStatus(ctx, {"status.debuff", 0, 0}, target, false) == 6); // 2 x gain 3
    CHECK(game::ai::ValueOfStatus(ctx, {"status.debuff", 0, 0}, target, true) < 0);

    target.canDamage = false;
    CHECK(game::ai::ValueOfStatus(ctx, {"status.debuff", 0, 0}, target, false) == 0);

    target.canDamage = true;
    target.effects.push_back(game::FStatusEffect{"status.debuff", 3, -2, false});
    CHECK(game::ai::ValueOfStatus(ctx, {"status.debuff", 0, 0}, target, false) == 0); // nothing to add
}

// --- A2: the camp hoard metric -------------------------------------------------------------------

struct FCampBoot {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;

    explicit FCampBoot(std::string_view label) : checkpoint(UniqueCheckpoint(label)) {
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        game::StartRun(registry, 4242u, true);
    }

    ~FCampBoot() { RemoveSaveFiles(checkpoint, runSave); }

    FCampBoot(const FCampBoot&) = delete;
    FCampBoot& operator=(const FCampBoot&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
    game::FCampProgress& progress() { return registry.ctx().get<game::FCampProgress>(); }
    game::FRewards& rewards() { return registry.ctx().get<game::FRewards>(); }
};

[[nodiscard]] game::FRunMember& Member(game::FDungeonRun& run, std::string_view id) {
    const auto found = std::ranges::find(run.party, id, &game::FRunMember::id);
    CHECK(found != run.party.end());
    return *found;
}

/// CampPurchasableCountsOnlyLiveServices: the denominator counts exactly the services whose Validate*
/// accepts them — respec appears with a node allocated, the offer disappears at the camp cap and with a
/// pool below min_legal_candidates, and a hurt member adds no service (the rest is not a priced service).
/// The counts move by exactly one per service; the price order keeps the two-cheapest sum unchanged here.
void CampPurchasableCountsOnlyLiveServices() {
    FCampBoot boot("camp-live-services");
    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp; // the metric is a camp-entry computation

    int before = 0;
    const int beforeTwo = game::CampCheapestTwoPurchasable(boot.registry, &before);
    // Fresh party: two reinforcement entries + the camp rune offer; no node is allocated for a respec.
    CHECK(before == 3);
    CHECK(beforeTwo > 0);

    game::FRunMember& warrior = Member(run, "party.warrior");
    warrior.allocatedNodes = {"node.might"};
    int withRespec = 0;
    const int respecTwo = game::CampCheapestTwoPurchasable(boot.registry, &withRespec);
    CHECK(withRespec == before + 1);
    CHECK(respecTwo == beforeTwo); // respec's flat price sits above the two reinforcement steps

    warrior.health = 1;
    int withHurt = 0;
    (void)game::CampCheapestTwoPurchasable(boot.registry, &withHurt);
    CHECK(withHurt == withRespec);  // a hurt member adds no service

    const int cap = boot.registry.ctx().get<game::FCampTables>().runeOfferCampCap;
    CHECK(cap >= 1);
    boot.progress().campDepth = static_cast<std::uint32_t>(run.depth);
    boot.progress().runeDrawsThisCamp = static_cast<std::uint32_t>(cap);
    int capped = 0;
    (void)game::CampCheapestTwoPurchasable(boot.registry, &capped);
    CHECK(capped == withRespec - 1); // used-up camp offers are not part of a minimal visit

    boot.progress().runeDrawsThisCamp = 0;
    boot.rewards().tables.minLegalCandidates = 9999; // the peek gate: no legal pool, no offer
    int emptyPool = 0;
    (void)game::CampCheapestTwoPurchasable(boot.registry, &emptyPool);
    CHECK(emptyPool == withRespec - 1);
}

/// CampPurchasableDropsUpgradesAfterRest (WP-43): once the party rested, stat upgrades are closed, so the
/// two reinforcement entries leave the count.
void CampPurchasableDropsUpgradesAfterRest() {
    FCampBoot boot("camp-after-rest");
    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    Member(run, "party.warrior").health = 1;
    int before = 0;
    (void)game::CampCheapestTwoPurchasable(boot.registry, &before);
    CHECK(!game::RestAtDungeonCamp(boot.registry).has_value());
    int after = 0;
    (void)game::CampCheapestTwoPurchasable(boot.registry, &after);
    CHECK(after == before - 2);
}

/// CampPurchasableIgnoresAffordability: the metric answers "what could be bought", never "what the purse
/// covers" — a broke party counts the same services as a rich one, and the temporary material raise is
/// restored (the run is never mutated by the report).
void CampPurchasableIgnoresAffordability() {
    FCampBoot boot("camp-affordability");
    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    Member(run, "party.warrior").allocatedNodes = {"node.might"};
    run.party[0].health = 1; // a hurt member adds no service; respec live, so all four entries can appear

    run.materials = 0;
    int poor = 0;
    const int poorTwo = game::CampCheapestTwoPurchasable(boot.registry, &poor);
    CHECK(run.materials == 0); // the affordability guard is scoped: the run's materials are restored

    run.materials = 1000000;
    int rich = 0;
    const int richTwo = game::CampCheapestTwoPurchasable(boot.registry, &rich);

    CHECK(poor == rich);
    CHECK(poor == 4); // upgrade, next upgrade, respec, offer
    CHECK(poorTwo == richTwo);
}

/// CampEntryReportAddsPurchasableCount: the log line keeps the old keys and adds `purchasable=<n>`; the
/// ratio divides by the purchasable two; nothing purchasable logs 0.00 without a division.
void CampEntryReportAddsPurchasableCount() {
    {
        FCampBoot boot("camp-report");
        auto& run = boot.run();
        run.phase = game::EDungeonPhase::Camp;
        run.materials = 7;
        FLogCapture capture;
        game::LogCampEntryReport(boot.registry);
        int count = 0;
        const int two = game::CampCheapestTwoPurchasable(boot.registry, &count);
        const std::string text = capture.Text();
        CHECK(count >= 3);
        CHECK(text.find("[camp] hoard_ratio=") != std::string::npos);
        CHECK(text.find("cheapest_two=" + std::to_string(two)) != std::string::npos);
        CHECK(text.find("purchasable=" + std::to_string(count)) != std::string::npos);
        std::ostringstream ratio;
        ratio.precision(2);
        ratio << std::fixed << (two > 0 ? static_cast<double>(run.materials) / two : 0.0);
        CHECK(text.find("hoard_ratio=" + ratio.str()) != std::string::npos);
    }
    {
        FCampBoot boot("camp-report-empty");
        auto& run = boot.run();
        run.phase = game::EDungeonPhase::Camp;
        // Test-only table edits: no reinforcement rows, no legal offer pool, no nodes, full sentinels.
        boot.registry.ctx().get<game::FDungeonGrowth>().statUpgrades.clear();
        boot.rewards().tables.minLegalCandidates = 9999;
        for (auto& member : run.party) member.health = -1;
        FLogCapture capture;
        game::LogCampEntryReport(boot.registry);
        int count = -1;
        const int two = game::CampCheapestTwoPurchasable(boot.registry, &count);
        CHECK(count == 0);
        CHECK(two == 0);
        CHECK(capture.Text().find("hoard_ratio=0.00") != std::string::npos);
        CHECK(capture.Text().find("cheapest_two=0") != std::string::npos);
        CHECK(capture.Text().find("purchasable=0") != std::string::npos);

        // Exactly one purchasable service (a node to reallocate): the denominator is that one price.
        Member(run, "party.warrior").allocatedNodes = {"node.might"};
        int single = 0;
        const int singleTwo = game::CampCheapestTwoPurchasable(boot.registry, &single);
        CHECK(single == 1);
        CHECK(singleTwo == game::CampPricesOf(boot.registry).respec);
    }
}

// --- A3: GAME01P_SEED ----------------------------------------------------------------------------

/// RunSeedParsesStrictDecimal: digits only — empty, whitespace, signs, junk and overflow are rejected;
/// 0 and the u32 maximum are valid seeds.
void RunSeedParsesStrictDecimal() {
    CHECK(!game::ParseRunSeed("").has_value());
    CHECK(!game::ParseRunSeed(" ").has_value());
    CHECK(!game::ParseRunSeed(" 7").has_value());
    CHECK(!game::ParseRunSeed("7 ").has_value());
    CHECK(!game::ParseRunSeed("+7").has_value());
    CHECK(!game::ParseRunSeed("-7").has_value());
    CHECK(!game::ParseRunSeed("7x").has_value());
    CHECK(!game::ParseRunSeed("x7").has_value());
    CHECK(!game::ParseRunSeed("0x10").has_value());
    CHECK(game::ParseRunSeed("0") == 0u);
    CHECK(game::ParseRunSeed("0007") == 7u);
    CHECK(game::ParseRunSeed("4242") == 4242u);
    CHECK(game::ParseRunSeed("4294967295") == 4294967295u);   // u32 max
    CHECK(!game::ParseRunSeed("4294967296").has_value());     // one past the max
    CHECK(!game::ParseRunSeed("99999999999999999999").has_value());
}

/// RunSeedSequencesAcrossAttempts: without the env the fallback stands for every attempt; with it the
/// first resolve is the seed and each later attempt advances by one, wrapping at the u32 ceiling.
void RunSeedSequencesAcrossAttempts() {
    using Seed = std::optional<std::uint32_t>;
    CHECK(game::RunSeedFor(std::nullopt, 77u, 0u) == 77u);
    CHECK(game::RunSeedFor(std::nullopt, 77u, 5u) == 77u);
    CHECK(game::RunSeedFor(Seed{1000u}, 77u, 0u) == 1000u);
    CHECK(game::RunSeedFor(Seed{1000u}, 77u, 1u) == 1001u);
    CHECK(game::RunSeedFor(Seed{1000u}, 77u, 3u) == 1003u);
    CHECK(game::RunSeedFor(Seed{0xFFFFFFFFu}, 77u, 1u) == 0u); // wraps
}

/// ResolveRunSeedReadsEnv: the env is read through std::getenv (SDL would cache it), an invalid value
/// falls back, a valid one advances per resolve in this process and an unset env keeps the ticks fallback.
void ResolveRunSeedReadsEnv() {
    _putenv_s("GAME01P_SEED", "junk");
    CHECK(game::ResolveRunSeed(99u) == 99u); // invalid value: warn once + fallback

    _putenv_s("GAME01P_SEED", "900000");
    const std::uint32_t first = game::ResolveRunSeed(5u);
    const std::uint32_t second = game::ResolveRunSeed(5u);
    CHECK(first != 5u);                     // the env seed, not the fallback
    CHECK(second == first + 1u);            // retry-from-checkpoint in the same process
    CHECK(first - 900000u <= 100u);         // the attempt offset is this binary's few resolves

    _putenv_s("GAME01P_SEED", "");
    CHECK(game::ResolveRunSeed(5u) == 5u);  // unset again: the drawn fallback
}

} // namespace

int main() {
    // Feature tests get no per-test save directory from CMake: isolate every save under the build tree so a
    // run can never touch %APPDATA%\Game01P\EmberDungeon.
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path =
            (std::filesystem::current_path() / "out" / "test-saves" / "tuning_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("tuning", {
        {"DebuffLowersDamageThroughResolver", DebuffLowersDamageThroughResolver},
        {"DebuffMagnitudeSignRuleAndRuneDelta", DebuffMagnitudeSignRuleAndRuneDelta},
        {"DebuffValueIsPositiveOnEnemies", DebuffValueIsPositiveOnEnemies},
        {"CampPurchasableCountsOnlyLiveServices", CampPurchasableCountsOnlyLiveServices},
        {"CampPurchasableIgnoresAffordability", CampPurchasableIgnoresAffordability},
        {"CampPurchasableDropsUpgradesAfterRest", CampPurchasableDropsUpgradesAfterRest},
        {"CampEntryReportAddsPurchasableCount", CampEntryReportAddsPurchasableCount},
        {"RunSeedParsesStrictDecimal", RunSeedParsesStrictDecimal},
        {"RunSeedSequencesAcrossAttempts", RunSeedSequencesAcrossAttempts},
        {"ResolveRunSeedReadsEnv", ResolveRunSeedReadsEnv},
    });
}
