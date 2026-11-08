// WP-21b L2 promotion (promotion_tests): the `progression.json` axes and the `promotions` meta-save block —
// new-boss-milestone-only accrual (D-22), the active-party scope, the capped-axis overflow conversion, the
// shipped both-off contract (active slots 4 / socket bonus 0), save round trip + restart persistence, boot
// validation of the progression table, and the read-only (disabled) session. No real battles: the
// BossDefeated / DungeonCleared events are appended directly (the WP-15 cross contract, standalone green).
// Every scratch file lives under `out/`; the flag-on cases put a temp asset root FIRST (it overrides
// `data/progression.json` only — roster/unlocks/dungeon still resolve from the frozen fixture + shipped
// roots). GAME01P_SAVE_DIR falls back to `out/test-saves` on a direct run (the preparation_tests pattern).

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/dungeon/FRunStart.h"
#include "gameplay/meta/FMetaProgress.h"
#include "gameplay/meta/FMetaSave.h"

namespace {

using nlohmann::json;

// ---------------------------------------------------------------------------------------------
// Scratch dirs, file helpers (the meta_save_tests pattern)
// ---------------------------------------------------------------------------------------------

/// A per-test scratch directory under `out/`, removed when the test ends (pass or fail; a failed removal
/// is ignored). Every fixture owns one, so a ctest run leaves no dirs behind.
struct FScratchDir {
    std::filesystem::path path;

    explicit FScratchDir(const char* stem) {
        static std::uint32_t serial = 0;
        path = std::filesystem::path("out") /
            (std::string(stem) + "-" + std::to_string(++serial) + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code error;
        std::filesystem::create_directories(path, error);
        CHECK(!error);
    }
    ~FScratchDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error); // best effort: a locked file must not fail the test
    }
    FScratchDir(const FScratchDir&) = delete;
    FScratchDir& operator=(const FScratchDir&) = delete;
};

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

json ReadJson(const std::filesystem::path& path) { return json::parse(ReadText(path)); }

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    CHECK(file.good());
}

/// The temp asset root FIRST (it overrides `data/progression.json`), then the frozen fixture and the
/// shipped assets — the roots every other meta test boots with.
std::vector<std::string> PrependAssetRoot(const std::filesystem::path& root) {
    std::vector<std::string> roots{root.string()};
    const std::vector<std::string> base = TestAssetRoots();
    roots.insert(roots.end(), base.begin(), base.end());
    return roots;
}

/// The pinned values of the temp-root progression table: data, not balance — the tests that need a
/// specific value own their own document (the shipped file is a playtest variable and may move).
constexpr int kTempSlotCap = 5;
constexpr int kTempBossesPerPromotion = 2;
constexpr int kTempSealsPerOverflow = 1;

void WriteProgressionRaw(const std::filesystem::path& dataDir, const json& document) {
    std::error_code error;
    std::filesystem::create_directories(dataDir, error);
    CHECK(!error);
    WriteText(dataDir / "progression.json", document.dump());
}

void WriteProgressionFile(const std::filesystem::path& dataDir, bool slotPromotion, bool socketPromotion) {
    WriteProgressionRaw(dataDir,
                        json{{"schema_version", 1},
                             {"slot_cap", kTempSlotCap},
                             {"slot_promotion", slotPromotion},
                             {"socket_promotion", socketPromotion},
                             {"promotion_rewards", {{"bosses_per_promotion", kTempBossesPerPromotion},
                                                    {"seals_per_overflow", kTempSealsPerOverflow}}}});
}

/// A minimal pre-seeded meta save: the loader merges the shipped roster in on load, so a promotions map
/// alone is a valid schema-v1 document (the additive path).
void WriteMetaPreset(const std::filesystem::path& metaPath, const json& promotions) {
    WriteText(metaPath, json{{"schema_version", 1}, {"promotions", promotions}}.dump());
}

// ---------------------------------------------------------------------------------------------
// The booted registry: assets + content + dungeon, with its own scratch save dir
// ---------------------------------------------------------------------------------------------

struct FMetaFixture {
    FScratchDir scratch; // declared first: destroyed last, after the registry released its files
    const std::filesystem::path& dir;
    entt::registry registry;
    std::string checkpoint;
    std::string meta;

    explicit FMetaFixture(const char* stem, std::vector<std::string> roots = TestAssetRoots())
        : scratch(stem), dir(scratch.path) {
        checkpoint = (dir / "checkpoint.json").string();
        meta = (dir / "meta_save.json").string();
        game::FAssetManager::Initialize(registry, std::move(roots));
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
    }
    FMetaFixture(const FMetaFixture&) = delete;
    FMetaFixture& operator=(const FMetaFixture&) = delete;

    /// The explicit path pins the meta file to this fixture's dir (never the env / preference rules).
    bool Init() { return game::InitializeMetaProgress(registry, meta); }
    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
};

void EnsureRun(FMetaFixture& fixture, std::uint32_t seed = 4242) {
    if (!fixture.registry.ctx().contains<game::FDungeonRun>()) {
        game::InstallDungeonRunAt(fixture.registry, seed, 0);
    }
}

void PlaceRun(FMetaFixture& fixture, int clearedIndex, int room) {
    EnsureRun(fixture);
    auto& run = fixture.run();
    run.depth = clearedIndex + 1;
    run.room = room;
}

/// Simulates the real append site: BossDefeated is appended while cleared is still depth - 1 (the battle
/// just ended; the camp step has not run yet). The meta listener pays the milestone and steps promotions
/// only when the ledger key is new (WP-21 §4).
void FireBossDefeat(FMetaFixture& fixture, int clearedIndex, int ordinal) {
    PlaceRun(fixture, clearedIndex, ordinal);
    fixture.run().cleared = clearedIndex;
    game::AppendRunEvent(fixture.registry, game::ERunEventKind::BossDefeated, "enemy.guardian");
}

/// Simulates ContinueReward: `cleared` is set to depth BEFORE the DungeonCleared append (the listener
/// derives the key from the event's depth).
void FireDungeonClear(FMetaFixture& fixture, int clearedIndex) {
    PlaceRun(fixture, clearedIndex, 0);
    fixture.run().cleared = clearedIndex + 1;
    game::AppendRunEvent(fixture.registry, game::ERunEventKind::DungeonCleared);
}

struct FBossHit {
    int clearedIndex{0};
    int ordinal{0};
};

/// One member's promotion expressed in accrual steps: a tier-up counts as `bosses_per_promotion` steps.
/// The metric is exact whatever the shipped value is (progression.json is a playtest variable, not frozen
/// fixture data), so an accrual assertion never moves with the balance.
int PromotionSteps(const game::FMetaProgression& progression, const game::FMetaPromotion& promotion) {
    return promotion.tier * progression.bossesPerPromotion + promotion.progress;
}

std::optional<FBossHit> FindBossSlot(FMetaFixture& fixture, std::string_view kind) {
    const auto* sequence = game::FindRunSequence(fixture.registry);
    CHECK(sequence != nullptr);
    for (int index = 0; index < sequence->TotalDungeons(); ++index) {
        const auto position = sequence->PositionOf(index);
        for (int ordinal = 0; ordinal < sequence->BattleCountAt(position); ++ordinal) {
            const auto* slot = sequence->BattleSlotAt(position, ordinal);
            if (slot != nullptr && slot->boss == kind) return FBossHit{index, ordinal};
        }
    }
    return std::nullopt;
}

/// The first `count` boss slots in sequence order (mid or final).
std::vector<FBossHit> FindBossSlots(FMetaFixture& fixture, int count) {
    const auto* sequence = game::FindRunSequence(fixture.registry);
    CHECK(sequence != nullptr);
    std::vector<FBossHit> slots;
    for (int index = 0; index < sequence->TotalDungeons() && static_cast<int>(slots.size()) < count; ++index) {
        const auto position = sequence->PositionOf(index);
        for (int ordinal = 0; ordinal < sequence->BattleCountAt(position) && static_cast<int>(slots.size()) < count;
             ++ordinal) {
            const auto* slot = sequence->BattleSlotAt(position, ordinal);
            if (slot != nullptr && !slot->boss.empty()) slots.push_back(FBossHit{index, ordinal});
        }
    }
    return slots;
}

std::string MilestoneKey(FMetaFixture& fixture, int clearedIndex, const char* kind) {
    const auto* sequence = game::FindRunSequence(fixture.registry);
    CHECK(sequence != nullptr);
    const auto position = sequence->PositionOf(clearedIndex);
    return sequence->themes[position.theme].id + "/" + std::to_string(position.dungeonInTheme) + "#" + kind;
}

/// The persisted ledger of rank 1: does it contain the milestone key?
bool GrantedContains(const std::filesystem::path& metaPath, std::string_view key) {
    const auto document = ReadJson(metaPath);
    const auto ladder = document.find("ladder");
    if (ladder == document.end()) return false;
    const auto rank = ladder->find("1");
    if (rank == ladder->end()) return false;
    const auto granted = rank->find("granted");
    if (granted == rank->end()) return false;
    for (const auto& entry : *granted) {
        if (entry.get<std::string>() == key) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Accrual: new boss milestones only
// ---------------------------------------------------------------------------------------------

void PromotionOnlyOnNewBossMilestone() {
    FMetaFixture fixture("promotion-first-boss");
    CHECK(fixture.Init());
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);
    EnsureRun(fixture);
    CHECK(fixture.run().party.size() == 4); // "each of the 4 run members" is really exercised
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());

    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    CHECK(game::MetaSeals(fixture.registry) == 2); // the boss's own grant (the milestone fired)
    for (const game::FRunMember& member : fixture.run().party) {
        // One accrual step per new boss milestone: with the shipped bosses_per_promotion >= 2 that is
        // `progress == 1`; the step metric stays exact if the playtest value moves.
        CHECK(PromotionSteps(*progression, game::MetaPromotionOf(fixture.registry, member.id)) == 1);
    }

    const int sealsAfterBoss = game::MetaSeals(fixture.registry);
    FireDungeonClear(fixture, boss->clearedIndex);
    CHECK(game::FindMetaSave(fixture.registry)->stats.clears == 1); // the clear really was processed
    CHECK(game::MetaSeals(fixture.registry) > sealsAfterBoss);     // and granted its own milestone
    for (const game::FRunMember& member : fixture.run().party) {
        // DungeonCleared adds 0 promotions.
        CHECK(PromotionSteps(*progression, game::MetaPromotionOf(fixture.registry, member.id)) == 1);
    }
}

void RepeatBossDoesNotPromote() {
    FMetaFixture fixture("promotion-repeat-boss");
    CHECK(fixture.Init());
    EnsureRun(fixture);
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());

    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    CHECK(game::MetaSeals(fixture.registry) == 2);
    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, boss->clearedIndex, "boss")));
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);
    const std::string afterFirst = ReadText(fixture.meta);

    // The same (rank, dungeon) boss again: the ledger already holds `#boss` (D-22).
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    CHECK(ReadText(fixture.meta) == afterFirst); // no change -> no save at all
    CHECK(game::MetaSeals(fixture.registry) == 2);
    for (const game::FRunMember& member : fixture.run().party) {
        // Still exactly one accrual in total: the repeat added nothing.
        CHECK(PromotionSteps(*progression, game::MetaPromotionOf(fixture.registry, member.id)) == 1);
    }
}

void D21LowerStartCannotFarmPromotions() {
    FMetaFixture fixture("promotion-d21-farm");
    // A checkpoint at 2 cleared dungeons makes a start at 0 a genuine D-21 choice below the checkpoint.
    WriteText(fixture.checkpoint, json{{"schema_version", 1}, {"cleared", 2}}.dump());
    CHECK(game::InitializeDungeon(fixture.registry, fixture.checkpoint));
    CHECK(fixture.registry.ctx().get<game::FDungeonCheckpoint>().cleared == 2);
    CHECK(fixture.Init());
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);

    // The first run starts at the checkpoint and defeats its boss: the milestone enters the ledger.
    game::StartRunAt(fixture.registry, 7001, 2);
    CHECK(fixture.run().depth == 3);
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    CHECK(boss->clearedIndex == 2); // the deeper boss sits at dungeon 2
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    CHECK(game::MetaSeals(fixture.registry) == 2);
    for (const game::FRunMember& member : fixture.run().party) {
        CHECK(PromotionSteps(*progression, game::MetaPromotionOf(fixture.registry, member.id)) == 1);
    }

    // D-21: a NEW run started at a LOWER dungeon (0 < 2) walks up and re-defeats the boss...
    game::StartRunAt(fixture.registry, 7002, 0);
    CHECK(fixture.run().depth == 1); // started at dungeon 0, below the checkpoint
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    // ...and the ledger blocks it: 0 progress, no seals.
    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, boss->clearedIndex, "boss")));
    CHECK(game::MetaSeals(fixture.registry) == 2);
    for (const game::FRunMember& member : fixture.run().party) {
        CHECK(PromotionSteps(*progression, game::MetaPromotionOf(fixture.registry, member.id)) == 1); // unchanged
    }
}

void PromotionCountsActiveParty() {
    FMetaFixture fixture("promotion-active-party");
    CHECK(fixture.Init());
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);
    EnsureRun(fixture);
    CHECK(fixture.run().party.size() == 4);
    // The content has no fifth selectable member, so bench one: a roster member that is NOT in the party
    // must not accrue (the scope is run.party, not the catalog).
    const std::string removedId = fixture.run().party.back().id;
    fixture.run().party.pop_back();

    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);

    CHECK(fixture.run().party.size() == 3);
    for (const game::FRunMember& member : fixture.run().party) {
        CHECK(PromotionSteps(*progression, game::MetaPromotionOf(fixture.registry, member.id)) == 1);
    }
    const game::FMetaPromotion removed = game::MetaPromotionOf(fixture.registry, removedId);
    CHECK(removed.progress == 0);
    CHECK(removed.tier == 0);
    const auto* save = game::FindMetaSave(fixture.registry);
    CHECK(save != nullptr);
    CHECK(save->promotions.find(removedId) == save->promotions.end()); // never even recorded
}

// ---------------------------------------------------------------------------------------------
// Tier and cap rules (temp-root values are pinned by the test)
// ---------------------------------------------------------------------------------------------

void TierRisesAtBossesPerPromotion() {
    FScratchDir rootScratch("promotion-tier-root");
    WriteProgressionFile(rootScratch.path / "data", /*slotPromotion*/ false, /*socketPromotion*/ false);
    FMetaFixture fixture("promotion-tier", PrependAssetRoot(rootScratch.path));
    CHECK(fixture.Init());
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);
    const int need = progression->bossesPerPromotion;
    CHECK(need == kTempBossesPerPromotion); // the temp root pins the value under test

    const std::vector<FBossHit> bosses = FindBossSlots(fixture, need);
    CHECK(bosses.size() == static_cast<std::size_t>(need)); // the fixture content carries enough bosses

    FireBossDefeat(fixture, bosses[0].clearedIndex, bosses[0].ordinal);
    for (const game::FRunMember& member : fixture.run().party) {
        const game::FMetaPromotion promotion = game::MetaPromotionOf(fixture.registry, member.id);
        CHECK(promotion.progress == 1);
        CHECK(promotion.tier == 0);
    }

    FireBossDefeat(fixture, bosses[1].clearedIndex, bosses[1].ordinal);
    for (const game::FRunMember& member : fixture.run().party) {
        const game::FMetaPromotion promotion = game::MetaPromotionOf(fixture.registry, member.id);
        CHECK(promotion.progress == 0); // the tier-up resets progress...
        CHECK(promotion.tier == 1);     // ...after bosses_per_promotion new bosses
    }
}

void CappedMemberConvertsPerMilestone() {
    FScratchDir rootScratch("promotion-overflow-root");
    WriteProgressionFile(rootScratch.path / "data", /*slotPromotion*/ true, /*socketPromotion*/ false);
    FMetaFixture fixture("promotion-overflow", PrependAssetRoot(rootScratch.path));
    // Two members preset at the tier that caps the lit (slot) axis (cap 5 -> tier 1 caps), two at tier 0.
    WriteMetaPreset(fixture.meta, json{{"party.warrior", {{"progress", 0}, {"tier", 1}}},
                                       {"party.archer", {{"progress", 0}, {"tier", 1}}}});
    CHECK(fixture.Init());
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);
    CHECK(progression->slotPromotion);
    CHECK(progression->slotCap - game::kBaseActiveSkillSlots == 1); // tier 1 caps the slot axis
    CHECK(game::MetaPromotionOf(fixture.registry, "party.warrior").tier == 1); // the preset loaded
    CHECK(game::MetaPromotionOf(fixture.registry, "party.archer").tier == 1);

    EnsureRun(fixture);
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);

    // The boss's own seals (the D-14 grant) + one overflow conversion per capped member; nothing else.
    CHECK(progression->sealsPerOverflow == kTempSealsPerOverflow);
    CHECK(game::MetaSeals(fixture.registry) == 2 + 2 * progression->sealsPerOverflow);
    for (const char* id : {"party.warrior", "party.archer"}) {
        const game::FMetaPromotion promotion = game::MetaPromotionOf(fixture.registry, id);
        CHECK(promotion.progress == 0); // converted, not accrued
        CHECK(promotion.tier == 1);
    }
    for (const char* id : {"party.mage", "party.healer"}) {
        const game::FMetaPromotion promotion = game::MetaPromotionOf(fixture.registry, id);
        CHECK(promotion.progress == 1); // the uncapped members accrue
        CHECK(promotion.tier == 0);
    }
}

// ---------------------------------------------------------------------------------------------
// The shipped both-off contract
// ---------------------------------------------------------------------------------------------

void FlagsOffAccruesWithoutConversion() {
    FMetaFixture fixture("promotion-flags-off");
    WriteMetaPreset(fixture.meta, json{{"party.mage", {{"progress", 0}, {"tier", 3}}}});
    CHECK(fixture.Init());
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);
    CHECK(!progression->slotPromotion);   // shipped: the HUD/battle contract (Skill1..Skill4)
    CHECK(!progression->socketPromotion); // shipped
    const int need = progression->bossesPerPromotion;
    CHECK(need >= 1);
    CHECK(game::MetaPromotionOf(fixture.registry, "party.mage").tier == 3); // the preset loaded

    EnsureRun(fixture);
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);

    // Both axes off: nothing converts — the balance holds only the boss's own seals.
    CHECK(game::MetaSeals(fixture.registry) == 2);
    // Every member advanced exactly one step (a tier-up counts as one step): a high tier must not be
    // converted into seals.
    for (const game::FRunMember& member : fixture.run().party) {
        const int presetSteps = member.id == "party.mage" ? 3 * need : 0;
        CHECK(PromotionSteps(*progression, game::MetaPromotionOf(fixture.registry, member.id)) == presetSteps + 1);
    }
    // Off axes apply nothing.
    CHECK(game::MemberActiveSkillSlots(fixture.registry, "party.mage") == game::kBaseActiveSkillSlots);
    CHECK(game::MemberRuneSocketBonus(fixture.registry, "party.mage") == 0);
}

void SlotPromotionOffByDefault() {
    {
        // Shipped progression.json: the slot axis is off — a real tier 3 changes nothing.
        FMetaFixture fixture("promotion-slot-off");
        WriteMetaPreset(fixture.meta, json{{"party.mage", {{"progress", 0}, {"tier", 3}}}});
        CHECK(fixture.Init());
        const auto* progression = game::FindMetaProgression(fixture.registry);
        CHECK(progression != nullptr);
        CHECK(!progression->slotPromotion); // the shipped default (the HUD binds Skill1..Skill4 only)
        CHECK(game::MetaPromotionOf(fixture.registry, "party.mage").tier == 3); // the preset is a real tier 3
        CHECK(game::MemberActiveSkillSlots(fixture.registry, "party.mage") == game::kBaseActiveSkillSlots);
        CHECK(game::DefaultMemberLoadout(fixture.registry, "party.mage").size() ==
              static_cast<std::size_t>(game::kBaseActiveSkillSlots));
    }
    {
        // The flag on (temp root): slots == min(4 + tier, slot_cap); the default loadout follows the count.
        FScratchDir rootScratch("promotion-slot-on-root");
        WriteProgressionFile(rootScratch.path / "data", /*slotPromotion*/ true, /*socketPromotion*/ false);
        FMetaFixture fixture("promotion-slot-on", PrependAssetRoot(rootScratch.path));
        WriteMetaPreset(fixture.meta, json{{"party.mage", {{"progress", 0}, {"tier", 3}}}});
        CHECK(fixture.Init());
        const auto* progression = game::FindMetaProgression(fixture.registry);
        CHECK(progression != nullptr);
        CHECK(progression->slotPromotion);
        const int tier = game::MetaPromotionOf(fixture.registry, "party.mage").tier;
        CHECK(tier == 3);
        const int slots = game::MemberActiveSkillSlots(fixture.registry, "party.mage");
        CHECK(slots == std::min(game::kBaseActiveSkillSlots + tier, progression->slotCap));
        CHECK(game::DefaultMemberLoadout(fixture.registry, "party.mage").size() ==
              static_cast<std::size_t>(slots));
    }
}

void SocketPromotionOffByDefault() {
    {
        // Shipped progression.json: the socket axis is off.
        FMetaFixture fixture("promotion-socket-off");
        WriteMetaPreset(fixture.meta, json{{"party.mage", {{"progress", 0}, {"tier", 3}}}});
        CHECK(fixture.Init());
        const auto* progression = game::FindMetaProgression(fixture.registry);
        CHECK(progression != nullptr);
        CHECK(!progression->socketPromotion); // shipped: rune sockets stay 3 (bonus 0)
        CHECK(game::MetaPromotionOf(fixture.registry, "party.mage").tier == 3);
        CHECK(game::MemberRuneSocketBonus(fixture.registry, "party.mage") == 0);
        CHECK(game::MemberActiveSkillSlots(fixture.registry, "party.mage") == game::kBaseActiveSkillSlots);
    }
    {
        // The flag on (temp root): bonus == min(tier, 1); the slot axis stays off.
        FScratchDir rootScratch("promotion-socket-on-root");
        WriteProgressionFile(rootScratch.path / "data", /*slotPromotion*/ false, /*socketPromotion*/ true);
        FMetaFixture fixture("promotion-socket-on", PrependAssetRoot(rootScratch.path));
        WriteMetaPreset(fixture.meta, json{{"party.mage", {{"progress", 0}, {"tier", 3}}}});
        CHECK(fixture.Init());
        const auto* progression = game::FindMetaProgression(fixture.registry);
        CHECK(progression != nullptr);
        CHECK(progression->socketPromotion);
        const int tier = game::MetaPromotionOf(fixture.registry, "party.mage").tier;
        CHECK(tier == 3);
        CHECK(game::MemberRuneSocketBonus(fixture.registry, "party.mage") == std::min(tier, 1));
        CHECK(game::MemberActiveSkillSlots(fixture.registry, "party.mage") == game::kBaseActiveSkillSlots);
        CHECK(game::DefaultMemberLoadout(fixture.registry, "party.mage").size() ==
              static_cast<std::size_t>(game::kBaseActiveSkillSlots));
    }
}

// ---------------------------------------------------------------------------------------------
// Persistence, validation, read-only sessions
// ---------------------------------------------------------------------------------------------

void PromotionSavedAcrossRestart() {
    FMetaFixture fixture("promotion-restart");
    // A valid checkpoint makes the first boot write the fresh meta file (the untouched-profile shape).
    WriteText(fixture.checkpoint, json{{"schema_version", 1}, {"cleared", 1}}.dump());
    CHECK(game::InitializeDungeon(fixture.registry, fixture.checkpoint));
    CHECK(fixture.registry.ctx().get<game::FDungeonCheckpoint>().cleared == 1);
    CHECK(fixture.Init());
    CHECK(std::filesystem::exists(fixture.meta));
    CHECK(!ReadJson(fixture.meta).contains("promotions")); // empty map: the key is never written

    EnsureRun(fixture);
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);

    const std::string first = fixture.run().party[0].id;
    const game::FMetaPromotion before = game::MetaPromotionOf(fixture.registry, first);
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);
    CHECK(PromotionSteps(*progression, before) == 1); // one accrual happened
    const auto stored = ReadJson(fixture.meta);
    CHECK(stored.contains("promotions")); // written once non-empty
    CHECK(stored["promotions"].contains(first));
    CHECK(stored["promotions"][first]["progress"].get<int>() == before.progress); // the file matches memory
    CHECK(stored["promotions"][first]["tier"].get<int>() == before.tier);

    game::ShutdownMetaProgress(fixture.registry);
    CHECK(fixture.Init()); // the same file: the record survives the restart
    for (const game::FRunMember& member : fixture.run().party) {
        const game::FMetaPromotion promotion = game::MetaPromotionOf(fixture.registry, member.id);
        CHECK(promotion.progress == before.progress);
        CHECK(promotion.tier == before.tier);
    }
}

void InvalidProgressionFailsInit() {
    FScratchDir rootScratch("promotion-invalid-root");
    const auto dataDir = rootScratch.path / "data";
    FMetaFixture fixture("promotion-invalid", PrependAssetRoot(rootScratch.path));

    // slot_cap below the kBaseActiveSkillSlots contract.
    WriteProgressionRaw(dataDir, json{{"schema_version", 1},
                                      {"slot_cap", 3},
                                      {"slot_promotion", false},
                                      {"socket_promotion", false},
                                      {"promotion_rewards", {{"bosses_per_promotion", 2}, {"seals_per_overflow", 1}}}});
    CHECK(!fixture.Init());

    // bosses_per_promotion must be at least 1.
    WriteProgressionRaw(dataDir, json{{"schema_version", 1},
                                      {"slot_cap", 5},
                                      {"slot_promotion", false},
                                      {"socket_promotion", false},
                                      {"promotion_rewards", {{"bosses_per_promotion", 0}, {"seals_per_overflow", 1}}}});
    CHECK(!fixture.Init());

    // A flag must be a boolean.
    WriteProgressionRaw(dataDir, json{{"schema_version", 1},
                                      {"slot_cap", 5},
                                      {"slot_promotion", 1},
                                      {"socket_promotion", false},
                                      {"promotion_rewards", {{"bosses_per_promotion", 2}, {"seals_per_overflow", 1}}}});
    CHECK(!fixture.Init());

    // The positive control: the same registry boots once the temp document is valid.
    WriteProgressionFile(dataDir, false, false);
    CHECK(fixture.Init());
    const auto* progression = game::FindMetaProgression(fixture.registry);
    CHECK(progression != nullptr);
    CHECK(progression->slotCap == kTempSlotCap);
}

void PromotionsRoundTripAndRejectNegative() {
    game::FMetaSave save = game::DefaultMetaSave();
    save.seals = 3;
    save.promotions["party.warrior"] = {1, 2};
    save.promotions["party.mage"] = {0, 1};

    const json document = game::MetaSaveToJson(save);
    CHECK(document.contains("promotions"));
    CHECK(document["promotions"]["party.warrior"]["progress"].get<int>() == 1);
    CHECK(document["promotions"]["party.warrior"]["tier"].get<int>() == 2);

    const auto parsed = game::MetaSaveFromJson(document);
    CHECK(parsed.has_value());
    CHECK(game::MetaSaveToJson(*parsed) == document); // byte-identical round trip
    CHECK(parsed->promotions.size() == 2);
    CHECK(parsed->promotions.at("party.warrior").progress == 1);
    CHECK(parsed->promotions.at("party.warrior").tier == 2);
    CHECK(parsed->promotions.at("party.mage").progress == 0);
    CHECK(parsed->promotions.at("party.mage").tier == 1);

    // An untouched save emits no `promotions` key at all (schema v1 keeps its old shape); a missing key
    // parses as an empty map.
    CHECK(!game::MetaSaveToJson(game::DefaultMetaSave()).contains("promotions"));
    const auto bare = game::MetaSaveFromJson(json{{"schema_version", 1}});
    CHECK(bare.has_value());
    CHECK(bare->promotions.empty());

    // Negative counts are malformed, exactly like every other count in the document.
    CHECK(!game::MetaSaveFromJson(
        json{{"schema_version", 1}, {"promotions", {{"party.warrior", {{"progress", 0}, {"tier", -1}}}}}}));
    CHECK(!game::MetaSaveFromJson(
        json{{"schema_version", 1}, {"promotions", {{"party.warrior", {{"progress", -1}, {"tier", 0}}}}}}));
    // A non-object promotions block / entry is rejected too.
    CHECK(!game::MetaSaveFromJson(json{{"schema_version", 1}, {"promotions", json::array()}}));
    CHECK(!game::MetaSaveFromJson(json{{"schema_version", 1}, {"promotions", {{"party.warrior", 3}}}}));
}

void MetaDisabledDoesNotPromote() {
    FMetaFixture fixture("promotion-disabled");
    const std::string future = R"({"schema_version": 2, "seals": 99})";
    WriteText(fixture.meta, future);
    CHECK(fixture.Init());
    CHECK(game::MetaDisabled(fixture.registry)); // a newer file: read-only this session

    EnsureRun(fixture);
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);

    for (const game::FRunMember& member : fixture.run().party) {
        const game::FMetaPromotion promotion = game::MetaPromotionOf(fixture.registry, member.id);
        CHECK(promotion.progress == 0); // accrues nothing...
        CHECK(promotion.tier == 0);
    }
    CHECK(game::MetaSeals(fixture.registry) == 0); // the newer document is not interpreted
    CHECK(ReadText(fixture.meta) == future);       // ...and writes nothing
}

} // namespace

int main() {
    // CTest sets GAME01P_SAVE_DIR per feature test; a direct run does not. Fall back to out/test-saves so
    // a run can never touch %APPDATA%\Game01P\EmberDungeon (the preparation_tests pattern).
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path =
            (std::filesystem::current_path() / "out" / "test-saves" / "promotion_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
#else
        setenv("GAME01P_SAVE_DIR", path.c_str(), 1);
#endif
    }

    return game_test::RunTests("promotion", {
        {"PromotionOnlyOnNewBossMilestone", PromotionOnlyOnNewBossMilestone},
        {"RepeatBossDoesNotPromote", RepeatBossDoesNotPromote},
        {"D21LowerStartCannotFarmPromotions", D21LowerStartCannotFarmPromotions},
        {"PromotionCountsActiveParty", PromotionCountsActiveParty},
        {"TierRisesAtBossesPerPromotion", TierRisesAtBossesPerPromotion},
        {"CappedMemberConvertsPerMilestone", CappedMemberConvertsPerMilestone},
        {"FlagsOffAccruesWithoutConversion", FlagsOffAccruesWithoutConversion},
        {"SlotPromotionOffByDefault", SlotPromotionOffByDefault},
        {"SocketPromotionOffByDefault", SocketPromotionOffByDefault},
        {"PromotionSavedAcrossRestart", PromotionSavedAcrossRestart},
        {"InvalidProgressionFailsInit", InvalidProgressionFailsInit},
        {"PromotionsRoundTripAndRejectNegative", PromotionsRoundTripAndRejectNegative},
        {"MetaDisabledDoesNotPromote", MetaDisabledDoesNotPromote},
    });
}
