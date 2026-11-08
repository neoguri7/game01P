// WP-20 meta save: DMetaSave round trip + corruption, the seal ledger (성공 시만·마일스톤당 최초 1회·
// 재생 차단), the checkpoint import, the listener wiring, the shipped roster/unlock catalogs, save-dir
// isolation, the corruption/future-version policies and the title info line.
//
// Every scratch file this suite writes stays in a per-test directory under `out/` (the committed
// GAME01P_SAVE_DIR override from CMake is only used where the test exercises the env resolution itself).
// The WP-15 cross contract (BossDefeated) is exercised by appending the event directly — this suite is
// standalone-green without the producer.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/EngineState.h"
#include "core/JsonLoader.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/meta/FMetaProgress.h"
#include "gameplay/meta/FMetaSave.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FTitleMenuView.h"

namespace {

using nlohmann::json;

// ---------------------------------------------------------------------------------------------
// Scratch directories, env, file helpers
// ---------------------------------------------------------------------------------------------

/// A per-test scratch directory under `out/`, removed when the test ends (pass or fail; failures to remove
/// are ignored). Every FMetaFixture and every bare scratch user owns one, so a ctest run leaves no dirs.
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

constexpr const char* kSaveDirEnv = "GAME01P_SAVE_DIR";

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

std::optional<std::string> GetEnv(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return std::nullopt;
    std::string result(value);
    std::free(value);
    return result;
#else
    if (const char* value = std::getenv(name)) return std::string(value);
    return std::nullopt;
#endif
}

/// Restores GAME01P_SAVE_DIR when the test ends, so the lane's inherited value survives the process.
struct FSaveDirEnvGuard {
    std::optional<std::string> previous{GetEnv(kSaveDirEnv)};

    ~FSaveDirEnvGuard() {
        if (previous) SetEnv(kSaveDirEnv, previous->c_str());
        else UnsetEnv(kSaveDirEnv);
    }
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

// ---------------------------------------------------------------------------------------------
// The booted registry: assets + content + dungeon, with its own scratch save dir
// ---------------------------------------------------------------------------------------------

struct FMetaFixture {
    FScratchDir scratch; // declared first: destroyed last, after the registry released its files
    const std::filesystem::path& dir;
    entt::registry registry;
    std::string checkpoint;
    std::string meta;

    explicit FMetaFixture(const char* stem) : scratch(stem), dir(scratch.path) {
        checkpoint = (dir / "checkpoint.json").string();
        meta = (dir / "meta_save.json").string();
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
    }
    FMetaFixture(const FMetaFixture&) = delete;
    FMetaFixture& operator=(const FMetaFixture&) = delete;

    /// The default boot for ledger/IO tests: the explicit path pins the file to this fixture's dir.
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
/// just ended; the camp step has not run yet).
void FireBossDefeat(FMetaFixture& fixture, int clearedIndex, int ordinal) {
    PlaceRun(fixture, clearedIndex, ordinal);
    fixture.run().cleared = clearedIndex;
    game::AppendRunEvent(fixture.registry, game::ERunEventKind::BossDefeated, "enemy.guardian");
}

/// Simulates ContinueReward: `cleared` is set to depth BEFORE the DungeonCleared append. The listener
/// must not care — the ledger key derives from the event's depth.
void FireDungeonClear(FMetaFixture& fixture, int clearedIndex) {
    PlaceRun(fixture, clearedIndex, 0);
    fixture.run().cleared = clearedIndex + 1;
    game::AppendRunEvent(fixture.registry, game::ERunEventKind::DungeonCleared);
}

game::FRunSequence::FPosition PositionAt(FMetaFixture& fixture, int clearedIndex) {
    const auto* sequence = game::FindRunSequence(fixture.registry);
    CHECK(sequence != nullptr);
    return sequence->PositionOf(clearedIndex);
}

std::string MilestoneKey(FMetaFixture& fixture, int clearedIndex, const char* kind) {
    const auto* sequence = game::FindRunSequence(fixture.registry);
    CHECK(sequence != nullptr);
    const auto position = sequence->PositionOf(clearedIndex);
    return sequence->themes[position.theme].id + "/" + std::to_string(position.dungeonInTheme) + "#" + kind;
}

struct FBossHit {
    int clearedIndex{0};
    int ordinal{0};
};

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

/// The persisted ledger of one rank: does it contain the milestone key?
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

int GrantedCount(const std::filesystem::path& metaPath) {
    const auto document = ReadJson(metaPath);
    const auto ladder = document.find("ladder");
    if (ladder == document.end()) return 0;
    const auto rank = ladder->find("1");
    if (rank == ladder->end()) return 0;
    const auto granted = rank->find("granted");
    if (granted == rank->end()) return 0;
    return static_cast<int>(granted->size());
}

bool HasCorruptBackup(const std::filesystem::path& dir) {
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().filename().string().rfind("meta_save.json.corrupt-", 0) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// R1: the seal ledger (D-14/D-22)
// ---------------------------------------------------------------------------------------------

void RoundTripAndCorruption() {
    game::FMetaSave save = game::DefaultMetaSave();
    save.seals = 7;
    save.unlocks = {"skill.hook", "rune.inferno"};
    save.roster = {{"party.warrior", "Alden", "Stubborn", 2, true, false},
                   {"party.rogue_stub", "stub", "", 0, false, true}};
    auto& lane = save.ladder["1"];
    lane.bestDungeon = 5;
    lane.checkpoint = 5;
    lane.granted = {"theme.ember_crypt/2#boss", "theme.ember_crypt/2#clear"};
    lane.grand = 1;
    save.stats = {12, 3, 9, 1, 1};

    const auto document = game::MetaSaveToJson(save);
    const auto parsed = game::MetaSaveFromJson(document);
    CHECK(parsed.has_value());
    CHECK(game::MetaSaveToJson(*parsed) == document); // byte-identical round trip
    CHECK(parsed->seals == 7);
    CHECK(parsed->unlocks.size() == 2);
    CHECK(parsed->roster.size() == 2);
    CHECK(parsed->roster[0].id == "party.warrior" && parsed->roster[0].enabled);
    CHECK(parsed->roster[1].locked);
    CHECK(parsed->ladder.at("1").granted.size() == 2);
    CHECK(parsed->stats.runs == 12 && parsed->stats.clears == 3 && parsed->stats.bestDepth == 9);
    CHECK(parsed->stats.cycleBest == 1 && parsed->stats.grand == 1);

    // Corruption/malformed documents are rejected (the caller backs the file up and resets).
    CHECK(!game::MetaSaveFromJson(json::parse("[]")));
    CHECK(!game::MetaSaveFromJson(json::object()));
    CHECK(!game::MetaSaveFromJson(json{{"schema_version", 2}})); // exact version; future is the caller's branch
    CHECK(!game::MetaSaveFromJson(json{{"schema_version", 1}, {"seals", -1}}));
    CHECK(!game::MetaSaveFromJson(json{{"schema_version", 1}, {"seals", "7"}}));
    CHECK(!game::MetaSaveFromJson(json{{"schema_version", 1}, {"unlocks", {"skill.hook", "skill.hook"}}}));
    CHECK(!game::MetaSaveFromJson(
        json{{"schema_version", 1}, {"roster", json::array({json{{"id", "party.warrior"}}})}})); // neither enabled nor locked
    CHECK(!game::MetaSaveFromJson(
        json{{"schema_version", 1}, {"ladder", json::object({{"0", json::object()}})}})); // rank 0
}

void SealEconomy() {
    FMetaFixture fixture("seal-economy");
    CHECK(fixture.Init());
    CHECK(game::MetaSeals(fixture.registry) == 0);

    game::GrantSeals(fixture.registry, 1, "dungeon clear"); // the D-14 clear amount
    CHECK(game::MetaSeals(fixture.registry) == 1);
    game::GrantSeals(fixture.registry, 2, "boss breakthrough"); // the D-14 boss amount
    CHECK(game::MetaSeals(fixture.registry) == 3);
    game::GrantSeals(fixture.registry, 0, "run ended (defeat grants nothing)");
    CHECK(game::MetaSeals(fixture.registry) == 3);

    CHECK(ReadJson(fixture.meta)["seals"].get<int>() == 3); // and it persisted through the same path
}

void FirstClearOnlyGrantsOnce() {
    FMetaFixture fixture("first-clear");
    CHECK(fixture.Init());
    FireDungeonClear(fixture, 0);
    CHECK(game::MetaSeals(fixture.registry) == 1);
    FireDungeonClear(fixture, 0); // the same milestone, again
    CHECK(game::MetaSeals(fixture.registry) == 1);
    CHECK(GrantedCount(fixture.meta) == 1);
}

void CycleDoesNotRegrant() {
    FMetaFixture fixture("cycle-regrant");
    CHECK(fixture.Init());
    const auto* sequence = game::FindRunSequence(fixture.registry);
    CHECK(sequence != nullptr);
    const int total = sequence->TotalDungeons();
    CHECK(total > 0);

    FireDungeonClear(fixture, 0);
    CHECK(game::MetaSeals(fixture.registry) == 1);

    // depth = total + 1 -> PositionOf(total) = cycle 1, theme 0 / dungeon 0: the same dungeon again.
    FireDungeonClear(fixture, total);
    CHECK(game::MetaSeals(fixture.registry) == 1); // D-22 "반복 클리어 0" — the key carries no cycle
    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, 0, "clear")));

    const auto document = ReadJson(fixture.meta);
    CHECK(document["stats"]["clears"].get<int>() == 2);    // statistics count repeats...
    CHECK(document["stats"]["cycle_best"].get<int>() == 1); // ...and the cycle is recorded
    CHECK(document["ladder"]["1"]["granted"].size() == 1);
}

void FailureGrantsNothing() {
    FMetaFixture fixture("failure-grants");
    CHECK(fixture.Init());
    game::StartRunAt(fixture.registry, 777, 0);
    const auto result = game::ResolveRoomVictory(fixture.registry, false); // a wipe
    CHECK(result.has_value());
    CHECK(game::MetaSeals(fixture.registry) == 0);
    CHECK(GrantedCount(fixture.meta) == 0);
    CHECK(ReadJson(fixture.meta)["stats"]["runs"].get<int>() == 1); // the loss is counted, never granted
}

void AbandonGrantsNothing() {
    FMetaFixture fixture("abandon-grants");
    CHECK(fixture.Init());
    game::StartRunAt(fixture.registry, 778, 0);
    const auto result = game::ApplyRunIntent(fixture.registry, game::ERunIntent::AbandonRun);
    CHECK(result.has_value());
    CHECK(game::MetaSeals(fixture.registry) == 0);
    CHECK(ReadJson(fixture.meta)["stats"]["runs"].get<int>() == 1);
}

void BossClearGrantsOneAndTwo() {
    FMetaFixture fixture("boss-plus-clear");
    CHECK(fixture.Init());
    auto boss = FindBossSlot(fixture, "mid");
    if (!boss) boss = FindBossSlot(fixture, "final");
    CHECK(boss.has_value());

    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    CHECK(game::MetaSeals(fixture.registry) == 2);
    FireDungeonClear(fixture, boss->clearedIndex);
    // The ledger entries are separate, so the boss dungeon pays +2 and +1 = +3 (D-14, 원장 분리).
    CHECK(game::MetaSeals(fixture.registry) == 3);
    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, boss->clearedIndex, "boss")));
    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, boss->clearedIndex, "clear")));
}

void ClearAndBossKeysNameSameDungeon() {
    FMetaFixture fixture("same-dungeon-key");
    CHECK(fixture.Init());
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());

    // The regression: at BossDefeated time cleared == depth - 1, at DungeonCleared time cleared == depth.
    // Both keys must derive from the EVENT depth and name the SAME dungeon.
    PlaceRun(fixture, boss->clearedIndex, boss->ordinal);
    fixture.run().cleared = boss->clearedIndex;
    game::AppendRunEvent(fixture.registry, game::ERunEventKind::BossDefeated, "enemy.guardian");
    fixture.run().cleared = boss->clearedIndex + 1;
    game::AppendRunEvent(fixture.registry, game::ERunEventKind::DungeonCleared);

    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, boss->clearedIndex, "boss")));
    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, boss->clearedIndex, "clear")));
    CHECK(GrantedCount(fixture.meta) == 2); // no orphan key at cleared + 1
    CHECK(game::MetaSeals(fixture.registry) == 3);
}

void ReplayAfterUnsavedRunDoesNotRegrant() {
    FMetaFixture fixture("replay-no-regrant");
    CHECK(fixture.Init());
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());

    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    FireDungeonClear(fixture, boss->clearedIndex);
    CHECK(game::MetaSeals(fixture.registry) == 3);

    // The run save failed / the player continued: the room is replayed and the same victory fires again.
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    FireDungeonClear(fixture, boss->clearedIndex);
    CHECK(game::MetaSeals(fixture.registry) == 3); // the ledger blocks the replay
    CHECK(GrantedCount(fixture.meta) == 2);
}

void RankIsOneUntilWp18() {
    FMetaFixture fixture("rank-one");
    CHECK(fixture.Init());
    FireDungeonClear(fixture, 0);
    const auto ladder = ReadJson(fixture.meta)["ladder"];
    CHECK(ladder.is_object());
    CHECK(ladder.size() == 1); // W8 uses ladder["1"] only; rank 2+ arrives with WP-18's catalog
    CHECK(ladder.contains("1"));
    CHECK(!ladder.contains("2"));
}

void GrandSetOnFinalBossOnly() {
    FMetaFixture fixture("grand-final");
    CHECK(fixture.Init());
    const auto mid = FindBossSlot(fixture, "mid");
    const auto final = FindBossSlot(fixture, "final");
    CHECK(mid.has_value());
    CHECK(final.has_value());

    FireBossDefeat(fixture, mid->clearedIndex, mid->ordinal);
    auto document = ReadJson(fixture.meta);
    CHECK(document["stats"]["grand"].get<int>() == 0);  // a mid breakthrough is no grand milestone
    CHECK(document["ladder"]["1"]["grand"].get<int>() == 0);

    FireBossDefeat(fixture, final->clearedIndex, final->ordinal);
    document = ReadJson(fixture.meta);
    CHECK(document["stats"]["grand"].get<int>() == 1);
    CHECK(document["ladder"]["1"]["grand"].get<int>() == 1);
    // D-19: the final-boss breakthrough is a milestone record, not a run end.
    CHECK(fixture.registry.ctx().contains<game::FDungeonRun>());
    CHECK(fixture.run().phase == game::EDungeonPhase::Battle);

    // A replay of the same final breakthrough counts in the statistics but not in the ledger.
    FireBossDefeat(fixture, final->clearedIndex, final->ordinal);
    document = ReadJson(fixture.meta);
    CHECK(document["stats"]["grand"].get<int>() == 2);
    CHECK(document["ladder"]["1"]["grand"].get<int>() == 1);
}

void BossDefeatedExternalAppend() {
    FMetaFixture fixture("boss-external");
    CHECK(fixture.Init());
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    // No producer exists yet (WP-15 is another lane): appending the event directly is the standalone
    // contract — the subscriber pays +2 from the event alone.
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    CHECK(game::MetaSeals(fixture.registry) == 2);
    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, boss->clearedIndex, "boss")));
}

// ---------------------------------------------------------------------------------------------
// Wiring: registration once, survival across runs, order independence
// ---------------------------------------------------------------------------------------------

void ListenerRegisteredOnce() {
    FMetaFixture fixture("listener-once");
    CHECK(fixture.Init());
    const auto count = fixture.registry.ctx().get<game::FRunEventListeners>().listeners.size();
    CHECK(fixture.Init()); // a second initialize replaces the store but never adds a listener
    CHECK(fixture.registry.ctx().get<game::FRunEventListeners>().listeners.size() == count);
    FireDungeonClear(fixture, 0);
    CHECK(game::MetaSeals(fixture.registry) == 1); // exactly one delivery
}

void MetaListenerSurvivesNewRun() {
    FMetaFixture fixture("listener-new-run");
    CHECK(fixture.Init());
    game::StartRunAt(fixture.registry, 11, 0);
    FireDungeonClear(fixture, 0);
    CHECK(game::MetaSeals(fixture.registry) == 1);

    // A new run resets the event log only (FDungeonRun.cpp:398-402); the listener stays subscribed.
    game::InstallDungeonRunAt(fixture.registry, 22, 0);
    PlaceRun(fixture, 1, 0);
    game::AppendRunEvent(fixture.registry, game::ERunEventKind::DungeonCleared);
    CHECK(game::MetaSeals(fixture.registry) == 2); // a different dungeon grants again through the same listener
    CHECK(GrantedCount(fixture.meta) == 2);
}

void ListenerOrderIndependent() {
    FMetaFixture fixture("listener-order");
    int before = 0;
    int after = 0;
    // FGameplayServices::Initialize already registered WP-19a's run-log listener; stack two probes around
    // the meta registration and require the grant to be identical regardless of position.
    game::RegisterRunEventListener(fixture.registry, [&before](const game::FRunEvent&) { ++before; });
    CHECK(fixture.Init());
    game::RegisterRunEventListener(fixture.registry, [&after](const game::FRunEvent&) { ++after; });
    FireDungeonClear(fixture, 0);
    CHECK(before == 1);
    CHECK(after == 1);
    CHECK(game::MetaSeals(fixture.registry) == 1);
}

// ---------------------------------------------------------------------------------------------
// Records: best values and success milestones only
// ---------------------------------------------------------------------------------------------

void LadderRecordsBest() {
    FMetaFixture fixture("ladder-best");
    CHECK(fixture.Init());
    FireDungeonClear(fixture, 2); // dungeon 3
    auto document = ReadJson(fixture.meta);
    CHECK(document["ladder"]["1"]["checkpoint"].get<int>() == 3);
    CHECK(document["ladder"]["1"]["best_dungeon"].get<int>() == 3);
    CHECK(document["stats"]["best_depth"].get<int>() == 3);

    FireDungeonClear(fixture, 4); // dungeon 5: both records rise
    FireDungeonClear(fixture, 1); // dungeon 2: a lower clear never lowers a record
    document = ReadJson(fixture.meta);
    CHECK(document["ladder"]["1"]["checkpoint"].get<int>() == 5);
    CHECK(document["ladder"]["1"]["best_dungeon"].get<int>() == 5);
    CHECK(document["stats"]["best_depth"].get<int>() == 5);
    CHECK(game::MetaSeals(fixture.registry) == 3); // three distinct dungeon positions = three firsts
}

void SuccessMilestones() {
    FMetaFixture fixture("success-milestones");
    CHECK(fixture.Init());
    EnsureRun(fixture);
    PlaceRun(fixture, 0, 0);

    const auto before = ReadText(fixture.meta);
    game::AppendRunEvent(fixture.registry, game::ERunEventKind::RoomCleared);
    CHECK(game::MetaSeals(fixture.registry) == 0);
    CHECK(ReadText(fixture.meta) == before); // a no-meta event never rewrites the file

    FireDungeonClear(fixture, 0);
    CHECK(game::MetaSeals(fixture.registry) == 1); // clear = +1
    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    CHECK(game::MetaSeals(fixture.registry) == 3); // breakthrough = +2
}

// ---------------------------------------------------------------------------------------------
// checkpoint.json import (B4)
// ---------------------------------------------------------------------------------------------

void CheckpointV1ImportsIntoMeta() {
    FMetaFixture fixture("import-v1");
    const std::string original = R"({"schema_version": 1, "cleared": 3})";
    WriteText(fixture.checkpoint, original);
    CHECK(game::InitializeDungeon(fixture.registry, fixture.checkpoint));

    CHECK(fixture.Init());
    const auto* save = game::FindMetaSave(fixture.registry);
    CHECK(save != nullptr);
    CHECK(save->ladder.at("1").checkpoint == 3);
    CHECK(save->ladder.at("1").bestDungeon == 3); // the title line is right for existing players
    CHECK(ReadText(fixture.checkpoint) == original); // the original is never modified
    CHECK(ReadJson(fixture.meta)["ladder"]["1"]["checkpoint"].get<int>() == 3); // and the import persisted
}

void ImportOnlyWhenMetaMissing() {
    FMetaFixture fixture("import-only-missing");
    WriteText(fixture.meta,
              R"({"schema_version": 1, "seals": 2, "ladder": {"1": {"checkpoint": 5, "best_dungeon": 5}}, "stats": {}})");
    WriteText(fixture.checkpoint, R"({"schema_version": 1, "cleared": 3})");
    CHECK(game::InitializeDungeon(fixture.registry, fixture.checkpoint));

    CHECK(fixture.Init());
    const auto* save = game::FindMetaSave(fixture.registry);
    CHECK(save != nullptr);
    CHECK(save->ladder.at("1").checkpoint == 5); // the existing meta wins; no re-import
    CHECK(ReadJson(fixture.meta)["ladder"]["1"]["checkpoint"].get<int>() == 5);
}

void ImportRunsAfterDungeonInit() {
    // The order the composition root uses: InitializeDungeon installs FDungeonCheckpoint, then meta imports.
    FMetaFixture fixture("import-order");
    WriteText(fixture.checkpoint, R"({"schema_version": 1, "cleared": 4})");
    CHECK(game::InitializeDungeon(fixture.registry, fixture.checkpoint));
    CHECK(fixture.Init());
    CHECK(game::FindMetaSave(fixture.registry)->ladder.at("1").checkpoint == 4);

    // The opposite order imports nothing (and there is no late import): the wiring requirement is pinned.
    entt::registry early;
    game::FAssetManager::Initialize(early, TestAssetRoots());
    CHECK(game::gameplay::FGameplayServices::Initialize(early));
    const FScratchDir earlyScratch("import-order-early");
    const auto& earlyDir = earlyScratch.path;
    CHECK(game::InitializeMetaProgress(early, (earlyDir / "meta_save.json").string()));
    CHECK(game::FindMetaSave(early) != nullptr);
    CHECK(game::FindMetaSave(early)->ladder.empty()); // no checkpoint existed when meta initialized
    CHECK(game::InitializeDungeon(early, (earlyDir / "checkpoint.json").string()));
    CHECK(game::FindMetaSave(early)->ladder.empty()); // and the import never runs late either
}

void CheckpointCorruptImportsZero() {
    FMetaFixture fixture("import-corrupt");
    WriteText(fixture.checkpoint, "{ not json");
    CHECK(game::InitializeDungeon(fixture.registry, fixture.checkpoint));
    CHECK(!fixture.registry.ctx().get<game::FDungeonCheckpoint>().error.empty());

    CHECK(fixture.Init());
    const auto* save = game::FindMetaSave(fixture.registry);
    CHECK(save != nullptr);
    CHECK(save->ladder.empty()); // a corrupt checkpoint imports nothing (0 stays 0 — there is no ladder)
    CHECK(ReadText(fixture.checkpoint) == "{ not json"); // the unreadable original is untouched
}

// ---------------------------------------------------------------------------------------------
// Schema formats + the "options only" whitelist
// ---------------------------------------------------------------------------------------------

json LadderDoc(const json& ladder) {
    return json::object({{"schema_version", 1}, {"ladder", ladder}});
}

void GrantedListParses() {
    const auto parse = [](const json& granted) {
        return game::MetaSaveFromJson(
            LadderDoc(json::object({{"1", json::object({{"granted", granted}})}})));
    };
    for (const char* key : {"theme.ember_crypt/0#clear", "theme.ember_crypt/12#boss", "a/b/3#clear", "x/0#boss"}) {
        CHECK(parse(json::array({key})).has_value());
    }
    for (const char* key :
         {"", "theme/#clear", "theme/01#clear", "theme/1#other", "theme/1#Clear", "theme/-1#clear",
          "theme/1#clear/", "#clear", "theme/1"}) {
        CHECK(!parse(json::array({key})));
    }
    CHECK(!parse(json::array({"theme/1#clear", "theme/1#clear"}))); // duplicates are malformed
}

void LadderKeysPositiveIntegers() {
    const auto parse = [](const std::string& rank) {
        return game::MetaSaveFromJson(LadderDoc(json::object({{rank, json::object()}})));
    };
    for (const char* key : {"1", "42", "999"}) CHECK(parse(key).has_value());
    for (const char* key : {"0", "02", "", "x", "1.5", "-1"}) CHECK(!parse(key));
}

/// Recursive key walk: every key must be in the whitelist (`rankKeys`: the ladder's direct children are
/// rank keys instead). A key like "attack"/"health"/"bonus"/"stat" throws — that is the NoPowerFields
/// contract (Q18: 메타는 옵션을 팔고 파워를 팔지 않는다).
void ScanWhitelist(const json& value, const std::string& path, const std::set<std::string>& allowed, bool rankKeys) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (rankKeys && path == "ladder") {
                CHECK(game::IsValidMetaLadderRankKey(it.key())); // rank keys are dynamic, not whitelisted
            } else if (!allowed.contains(it.key())) {
                throw std::runtime_error("forbidden key '" + it.key() + "' at " + path);
            }
            const std::string child = path.empty() ? it.key() : path + "/" + it.key();
            ScanWhitelist(it.value(), child, allowed, rankKeys);
        }
    } else if (value.is_array()) {
        for (const auto& entry : value) ScanWhitelist(entry, path + "[]", allowed, rankKeys);
    }
}

void NoPowerFields() {
    const std::set<std::string> metaKeys = {
        "schema_version", "saved_at", "seals", "unlocks", "roster", "ladder", "stats",
        "id", "name", "trait", "medals", "enabled", "locked",
        "best_dungeon", "checkpoint", "granted", "grand",
        "runs", "clears", "best_depth", "cycle_best"};
    const std::set<std::string> rosterKeys = {
        "schema_version", "roster", "_comment", "id", "name", "trait", "medals", "enabled", "locked"};
    const std::set<std::string> unlockKeys = {"schema_version", "unlocks", "_comment", "id", "kind", "cost", "seal_gate"};

    game::FMetaSave save = game::DefaultMetaSave();
    save.seals = 3;
    save.unlocks = {"skill.hook"};
    save.roster = {{"party.warrior", "Alden", "Stubborn", 1, true, false},
                   {"party.rogue_stub", "stub", "", 0, false, true}};
    auto& lane = save.ladder["1"];
    lane.bestDungeon = 2;
    lane.checkpoint = 2;
    lane.granted = {"theme.ember_crypt/1#clear"};
    lane.grand = 0;
    save.stats = {1, 2, 3, 4, 5};
    const auto document = game::MetaSaveToJson(save);
    CHECK(document.contains("ladder") && document.contains("roster") && document.contains("stats")); // not vacuous
    ScanWhitelist(document, "", metaKeys, true);

    const auto roster = ReadJson("assets/data/roster.json");
    CHECK(roster.contains("roster") && !roster["roster"].empty());
    ScanWhitelist(roster, "", rosterKeys, false);
    const auto unlocks = ReadJson("assets/data/unlocks.json");
    CHECK(unlocks.contains("unlocks") && !unlocks["unlocks"].empty());
    ScanWhitelist(unlocks, "", unlockKeys, false);
}

// ---------------------------------------------------------------------------------------------
// Shipped catalogs (ship roots; content cross-checks do not gate the boot — W7 P22-7 convention)
// ---------------------------------------------------------------------------------------------

void UnlockCatalogValid() {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    CHECK(game::gameplay::FGameplayServices::Initialize(registry));
    const auto& assets = registry.ctx().get<game::FAssetManager>();
    const auto unlocksPath = assets.resolvePath("data/unlocks");
    CHECK(unlocksPath.has_value());
    const auto unlocks = game::FJsonLoader::Load(*unlocksPath, 1);
    CHECK(unlocks.has_value());
    const auto rosterPath = assets.resolvePath("data/roster");
    CHECK(rosterPath.has_value());
    const auto roster = game::FJsonLoader::Load(*rosterPath, 1);
    CHECK(roster.has_value());
    std::set<std::string> rosterIds;
    for (const auto& row : roster->at("roster")) rosterIds.insert(row.at("id").get<std::string>());

    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    bool sawSkill = false;
    bool sawRune = false;
    bool sawRoster = false;
    for (const auto& entry : unlocks->at("unlocks")) {
        const auto id = entry.at("id").get<std::string>();
        const auto kind = entry.at("kind").get<std::string>();
        if (kind == "skill") {
            sawSkill = true;
            CHECK(content.skills.find(id) != nullptr);
            // The unlock must be an OPTION beyond the default loadout: every pool that authors the skill
            // must place it at index >= 4 (FBattleSetup.cpp kDefaultLoadoutSlots = 4).
            bool authored = false;
            for (const auto& unit : content.units.rows) {
                const auto found = std::ranges::find(unit.skillIds, id);
                if (found == unit.skillIds.end()) continue;
                authored = true;
                CHECK(static_cast<std::size_t>(std::distance(unit.skillIds.begin(), found)) >= 4);
            }
            CHECK(authored);
        } else if (kind == "rune") {
            sawRune = true;
            CHECK(content.runes.find(id) != nullptr);
        } else if (kind == "roster") {
            sawRoster = true;
            CHECK(rosterIds.contains(id));
        } else if (kind == "event") {
            // No event catalog exists in this base (WP-09 adds it): an event entry may only arrive with it.
            CHECK(id.starts_with("event."));
        } else {
            CHECK(false);
        }
    }
    CHECK(sawSkill && sawRune && sawRoster); // the shipped catalog exercises every kind it can validate
}

void RosterStubLocked() {
    const auto roster = ReadJson("assets/data/roster.json");
    CHECK(roster["schema_version"].get<int>() == 1);
    std::set<std::string> ids;
    int enabled = 0;
    int locked = 0;
    for (const auto& row : roster.at("roster")) {
        const auto id = row.at("id").get<std::string>();
        CHECK(!id.empty());
        CHECK(ids.insert(id).second);
        const bool isEnabled = row.value("enabled", false);
        const bool isLocked = row.value("locked", false);
        CHECK(isEnabled != isLocked); // exactly one of enabled/locked
        // The HUD font is Latin-1 only (no Hangul, no U+2014): every shipped name must be printable ASCII
        // so WP-21 never draws a missing-glyph box for a stub row.
        const auto name = row.at("name").get<std::string>();
        CHECK(!name.empty());
        for (const char c : name) CHECK(c >= 0x20 && c < 0x7F);
        if (isLocked) ++locked;
        else ++enabled;
    }
    for (const char* id : {"party.warrior", "party.archer", "party.mage", "party.healer"}) {
        CHECK(ids.contains(id)); // the shipped party is present...
    }
    CHECK(enabled >= 4);
    CHECK(locked >= 4); // ...and the locked identity stubs are the WP-21/23 acquisition surface
}

void CheckpointAndLadderAgreeAfterRealClear() {
    // The only test where DungeonCleared comes from the REAL producer (FRunFlow ContinueReward): the dual
    // record (checkpoint.json and ladder["1"].checkpoint) is written from the same event and must agree.
    FMetaFixture fixture("real-clear");
    CHECK(fixture.Init());
    game::StartRunAt(fixture.registry, 4321, 0);
    auto& run = fixture.run();
    const auto* sequence = game::FindRunSequence(fixture.registry);
    CHECK(sequence != nullptr);
    const auto position = sequence->PositionOf(0);
    run.room = sequence->BattleCountAt(position) - 1; // the dungeon's last battle ordinal
    CHECK(run.phase == game::EDungeonPhase::Battle);
    CHECK(sequence->GapEvent(position, run.room) == nullptr); // nothing between the last battle and the camp

    CHECK(game::ResolveRoomVictory(fixture.registry, true).has_value());
    CHECK(run.phase == game::EDungeonPhase::Reward);
    // RollRoomOffer may have opened an offer (a boss room can chain a second one): skip through the real API.
    for (int guard = 0; guard < 4; ++guard) {
        const auto* rewards = fixture.registry.ctx().find<game::FRewards>();
        if (rewards == nullptr || !rewards->offer.active) break;
        CHECK(game::ResolveRuneOffer(fixture.registry, -1));
    }
    CHECK(game::ApplyRunIntent(fixture.registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(run.phase == game::EDungeonPhase::Camp);

    const int cleared = fixture.registry.ctx().get<game::FDungeonCheckpoint>().cleared;
    CHECK(cleared == 1);
    const auto* save = game::FindMetaSave(fixture.registry);
    CHECK(save != nullptr);
    CHECK(save->ladder.at("1").checkpoint == cleared);   // in memory
    CHECK(save->ladder.at("1").bestDungeon == cleared);
    const auto document = ReadJson(fixture.meta);        // and persisted
    CHECK(document["ladder"]["1"]["checkpoint"].get<int>() == cleared);
    CHECK(ReadJson(fixture.checkpoint)["cleared"].get<int>() == cleared); // checkpoint.json on disk agrees
    CHECK(game::MetaSeals(fixture.registry) == 1);        // +1 for the clear (no boss producer in this flow)
    CHECK(GrantedContains(fixture.meta, MilestoneKey(fixture, 0, "clear")));
}

void LadderCheckpointCappedLikeCheckpointFile() {
    // FRunFlow stores min(cleared, 999) in checkpoint.json; depth reaches 1000 in LeaveCamp. The ladder
    // mirrors the cap, so the dual record cannot diverge at the ceiling.
    FMetaFixture fixture("ladder-cap");
    CHECK(fixture.Init());
    FireDungeonClear(fixture, 999); // event.depth == 1000
    const auto document = ReadJson(fixture.meta);
    CHECK(document["ladder"]["1"]["checkpoint"].get<int>() == 999);
    CHECK(document["ladder"]["1"]["best_dungeon"].get<int>() == 999);
    CHECK(document["stats"]["best_depth"].get<int>() == 1000); // the statistic keeps the real depth
}

void NewCatalogRosterRowsMergeIntoSave() {
    FMetaFixture fixture("roster-merge");
    const auto catalog = ReadJson("assets/data/roster.json").at("roster");
    CHECK(catalog.size() > 2);

    // An older save: only the first two catalog members, with progress the merge must not touch.
    game::FMetaSave older = game::DefaultMetaSave();
    for (std::size_t index = 0; index < 2; ++index) {
        game::FMetaRosterRow row;
        row.id = catalog[index].at("id").get<std::string>();
        row.name = "Renamed" + std::to_string(index);
        row.trait = "Earned";
        row.medals = 5 + static_cast<int>(index);
        row.enabled = true;
        older.roster.push_back(row);
    }
    older.seals = 4;
    WriteText(fixture.meta, game::MetaSaveToJson(older).dump());

    CHECK(fixture.Init());
    const auto* save = game::FindMetaSave(fixture.registry);
    CHECK(save != nullptr);
    CHECK(!game::MetaDisabled(fixture.registry));
    CHECK(save->seals == 4);
    CHECK(save->roster.size() == catalog.size()); // every catalog id is now present
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        CHECK(save->roster[index].id == catalog[index].at("id").get<std::string>()); // existing first, rest in catalog order
    }
    for (std::size_t index = 0; index < 2; ++index) { // existing rows untouched
        CHECK(save->roster[index].name == "Renamed" + std::to_string(index));
        CHECK(save->roster[index].trait == "Earned");
        CHECK(save->roster[index].medals == 5 + static_cast<int>(index));
    }
    CHECK(!HasCorruptBackup(fixture.dir)); // an additive catalog change is not corruption

    // Persisted once on load: the file itself now lists every catalog member.
    const auto stored = ReadJson(fixture.meta);
    CHECK(stored.at("roster").size() == catalog.size());
    CHECK(stored.at("roster")[0].at("name").get<std::string>() == "Renamed0");
}

void UnlockIdsUnique() {
    const auto unlocks = ReadJson("assets/data/unlocks.json");
    CHECK(unlocks["schema_version"].get<int>() == 1);
    std::set<std::string> ids;
    for (const auto& row : unlocks.at("unlocks")) {
        const auto id = row.at("id").get<std::string>();
        const auto kind = row.at("kind").get<std::string>();
        CHECK(!id.empty());
        CHECK(ids.insert(id).second);
        CHECK(kind == "skill" || kind == "rune" || kind == "event" || kind == "roster");
        CHECK(row.at("cost").get<int>() >= 0);
        CHECK(row.at("seal_gate").get<int>() >= 0);
    }
    CHECK(!ids.empty());
}

void MissingUnlocksCatalogFailsInit() {
    const FScratchDir rootScratch("missing-unlocks");
    const auto& root = rootScratch.path;
    std::error_code code;
    std::filesystem::create_directories(root / "data", code);
    CHECK(!code);
    WriteText(root / "data" / "roster.json",
              R"({"schema_version": 1, "roster": [{"id": "party.warrior", "name": "Alden", "enabled": true}]})");

    entt::registry registry;
    game::FAssetManager::Initialize(registry, {root.string()});
    CHECK(!game::InitializeMetaProgress(registry, (root / "meta_save.json").string())); // boot stop

    // The control: with the catalog written the same registry boots — the failure was the missing file.
    WriteText(root / "data" / "unlocks.json",
              R"({"schema_version": 1, "unlocks": [{"id": "rune.power", "kind": "rune", "cost": 1, "seal_gate": 0}]})");
    CHECK(game::InitializeMetaProgress(registry, (root / "meta_save.json").string()));
    CHECK(game::FindMetaSave(registry) != nullptr);
}

void InvalidRosterFailsInit() {
    const FScratchDir rootScratch("invalid-roster");
    const auto& root = rootScratch.path;
    std::error_code code;
    std::filesystem::create_directories(root / "data", code);
    CHECK(!code);
    // Duplicate roster ids: shipped-content corruption, a boot stop (not a save reset).
    WriteText(root / "data" / "roster.json",
              R"({"schema_version": 1, "roster": [
                  {"id": "party.warrior", "name": "Alden", "enabled": true},
                  {"id": "party.warrior", "name": "Rowan", "locked": true}]})");
    WriteText(root / "data" / "unlocks.json",
              R"({"schema_version": 1, "unlocks": [{"id": "rune.power", "kind": "rune", "cost": 1, "seal_gate": 0}]})");

    entt::registry registry;
    game::FAssetManager::Initialize(registry, {root.string()});
    CHECK(!game::InitializeMetaProgress(registry, (root / "meta_save.json").string()));
}

// ---------------------------------------------------------------------------------------------
// Storage resolution (the save-dir isolation contract)
// ---------------------------------------------------------------------------------------------

/// A preference-directory probe that must never be called when a rule above it applies. It captures its
/// counter by reference (std::function copies the callable, the reference survives the copy).
struct FPrefProbe {
    int calls{0};

    auto callback() {
        return [this]() -> std::optional<std::filesystem::path> {
            ++calls;
            return std::nullopt;
        };
    }
};

void MetaUsesIsolatedDir() {
    FSaveDirEnvGuard guard;
    const FScratchDir envScratch("meta-isolated");
    const auto& envDir = envScratch.path;
    SetEnv(kSaveDirEnv, envDir.string().c_str());

    FMetaFixture fixture("meta-isolated-registry");
    FPrefProbe probe;
    CHECK(game::InitializeMetaProgress(fixture.registry, {}, probe.callback()));
    CHECK(probe.calls == 0); // an override set: the preference directory is never consulted
    CHECK(game::MetaSavePath(fixture.registry) == envDir / "meta_save.json");
    game::GrantSeals(fixture.registry, 1, "isolated write");
    CHECK(std::filesystem::exists(envDir / "meta_save.json"));
}

void MetaBesideCheckpointInTests() {
    FSaveDirEnvGuard guard;
    UnsetEnv(kSaveDirEnv);

    FMetaFixture fixture("meta-beside"); // the injected checkpoint makes the save dir explicit
    FPrefProbe probe;
    CHECK(game::InitializeMetaProgress(fixture.registry, {}, probe.callback()));
    CHECK(probe.calls == 0); // the checkpoint directory rule sits above the preference fallback
    CHECK(game::MetaSavePath(fixture.registry).lexically_normal() ==
          (fixture.dir / "meta_save.json").lexically_normal());
    CHECK(std::filesystem::exists(fixture.dir / "meta_save.json")); // the import persisted it
}

void OverrideNeverFallsBack() {
    FSaveDirEnvGuard guard;
    const FScratchDir dirScratch("meta-override-bad");
    const auto& dir = dirScratch.path;
    const auto blocker = dir / "blocker";
    WriteText(blocker, "a file, not a directory");
    SetEnv(kSaveDirEnv, (blocker / "child").string().c_str());

    FMetaFixture fixture("meta-override-bad-registry");
    FPrefProbe probe;
    CHECK(game::InitializeMetaProgress(fixture.registry, {}, probe.callback())); // storage-class failure: boot continues
    CHECK(game::MetaDisabled(fixture.registry));
    CHECK(probe.calls == 0); // never the user's real preference directory
    CHECK(game::MetaSavePath(fixture.registry).empty());
    game::GrantSeals(fixture.registry, 3, "disabled session");
    CHECK(game::MetaSeals(fixture.registry) == 0); // grants are logged only

    // The pure resolver shows the same contract without a boot.
    CHECK(!game::ResolveMetaSavePath(
        std::optional<std::filesystem::path>(blocker / "child"), fixture.checkpoint, probe.callback()));
    CHECK(probe.calls == 0);
}

// ---------------------------------------------------------------------------------------------
// Corruption + future-version policy
// ---------------------------------------------------------------------------------------------

void CorruptMetaBackedUpThenDefaults() {
    FMetaFixture fixture("corrupt-meta");
    const std::string garbage = "{ definitely not a meta document";
    WriteText(fixture.meta, garbage);
    CHECK(fixture.Init());

    CHECK(HasCorruptBackup(fixture.dir)); // the original survived as the backup
    std::filesystem::path backup;
    for (const auto& entry : std::filesystem::directory_iterator(fixture.dir)) {
        if (entry.path().filename().string().rfind("meta_save.json.corrupt-", 0) == 0) backup = entry.path();
    }
    CHECK(ReadText(backup) == garbage);
    const auto fresh = ReadJson(fixture.meta); // ...and a fresh valid file replaced it
    CHECK(fresh["schema_version"].get<int>() == 1);
    CHECK(fresh["seals"].get<int>() == 0);
    CHECK(!game::MetaDisabled(fixture.registry));
    game::GrantSeals(fixture.registry, 1, "post-reset grant");
    CHECK(ReadJson(fixture.meta)["seals"].get<int>() == 1);

#ifdef _WIN32
    // A backup rename that fails leaves the file alone and turns the session read-only (R7). Holding the
    // file open blocks the rename on Windows (no FILE_SHARE_DELETE).
    game::ShutdownMetaProgress(fixture.registry);
    WriteText(fixture.meta, garbage);
    {
        std::ofstream lock(fixture.meta, std::ios::binary | std::ios::app);
        CHECK(lock.good());
        CHECK(fixture.Init());
        CHECK(game::MetaDisabled(fixture.registry));
        game::GrantSeals(fixture.registry, 5, "read-only session");
        CHECK(game::MetaSeals(fixture.registry) == 0);
        CHECK(ReadText(fixture.meta) == garbage); // the un-backed-up file is never overwritten
    }
#endif
}

void FutureVersionNotOverwrittenEvenAfterGrant() {
    FMetaFixture fixture("future-meta");
    const std::string future = R"({"schema_version": 2, "seals": 99, "unlocks": ["skill.hook"]})";
    WriteText(fixture.meta, future);
    CHECK(fixture.Init());
    CHECK(game::MetaDisabled(fixture.registry));
    CHECK(game::MetaSummaryLine(fixture.registry) == "meta disabled");

    const auto boss = FindBossSlot(fixture, "mid");
    CHECK(boss.has_value());
    FireBossDefeat(fixture, boss->clearedIndex, boss->ordinal);
    game::GrantSeals(fixture.registry, 5, "must not persist");
    CHECK(ReadText(fixture.meta) == future); // byte for byte untouched...
    CHECK(!HasCorruptBackup(fixture.dir)); // ...and not renamed either
}

void FutureVersionSessionReadOnly() {
    FMetaFixture fixture("future-meta-readonly");
    const std::string future = R"({"schema_version": 2, "seals": 4})";
    WriteText(fixture.meta, future);
    CHECK(fixture.Init());
    CHECK(game::MetaDisabled(fixture.registry));
    CHECK(game::MetaSeals(fixture.registry) == 0); // the newer document is not interpreted
    game::GrantSeals(fixture.registry, 5, "no");
    CHECK(game::MetaSeals(fixture.registry) == 0);
    CHECK(!game::HasMetaUnlock(fixture.registry, "skill.hook"));
    game::GrantMetaUnlock(fixture.registry, "skill.hook");
    CHECK(!game::HasMetaUnlock(fixture.registry, "skill.hook"));
    CHECK(ReadText(fixture.meta) == future);
}

void RunSaveUntouchedByMetaCorruption() {
    FMetaFixture fixture("run-vs-meta");
    game::StartRunAt(fixture.registry, 1234, 0); // writes the run save next to the checkpoint
    const auto runSave = fixture.registry.ctx().get<game::FDungeonSave>().path;
    CHECK(std::filesystem::exists(runSave));
    const auto before = ReadText(runSave);

    WriteText(fixture.meta, "corrupt!");
    CHECK(fixture.Init());
    CHECK(ReadText(runSave) == before); // meta corruption resets meta only — never the run save
    CHECK(std::filesystem::exists(runSave));
    CHECK(ReadJson(fixture.meta)["schema_version"].get<int>() == 1); // the meta file itself was reset
}

// ---------------------------------------------------------------------------------------------
// Shutdown / re-initialize
// ---------------------------------------------------------------------------------------------

void ShutdownWithoutInitIsNoop() {
    entt::registry virgin;
    game::ShutdownMetaProgress(virgin); // no store: no-op, no throw
    CHECK(game::MetaSeals(virgin) == 0);
    CHECK(game::MetaDisabled(virgin));
    CHECK(game::MetaSummaryLine(virgin).empty());
    CHECK(game::FindMetaSave(virgin) == nullptr);
    CHECK(game::MetaSavePath(virgin).empty());

    FMetaFixture fixture("shutdown-reinit");
    CHECK(fixture.Init());
    const auto listeners = fixture.registry.ctx().get<game::FRunEventListeners>().listeners.size();
    game::ShutdownMetaProgress(fixture.registry);
    CHECK(game::FindMetaSave(fixture.registry) == nullptr);
    CHECK(game::MetaSummaryLine(fixture.registry).empty());
    CHECK(fixture.Init()); // re-initialize: the listener marker survives, so no second listener
    CHECK(fixture.registry.ctx().get<game::FRunEventListeners>().listeners.size() == listeners);
    FireDungeonClear(fixture, 0);
    CHECK(game::MetaSeals(fixture.registry) == 1);
}

// ---------------------------------------------------------------------------------------------
// The title information line (view-level: it must reach the draw path)
// ---------------------------------------------------------------------------------------------

/// The full title boot: assets + content + dungeon + art + ImGui, as the game's composition root does it.
struct FTitleFixture {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, &SDL_DestroySurface};
    FRendererPtr renderer{nullptr};
    entt::registry registry;
    std::string checkpoint;
    std::string meta;
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{nullptr, &ImGui::DestroyContext};

    FTitleFixture(const std::filesystem::path& dir, int checkpointCleared) {
        checkpoint = (dir / "checkpoint.json").string();
        meta = (dir / "meta_save.json").string();
        WriteText(checkpoint, json{{"schema_version", 1}, {"cleared", checkpointCleared}}.dump());
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        surface.reset(SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888));
        CHECK(surface != nullptr);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        CHECK(renderer != nullptr);
        auto& window = registry.ctx().emplace<game::FEngineWindowState>();
        window.width = 1280;
        window.height = 800;
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        CHECK(game::InitializeDungeonArt(registry));
        context.reset(ImGui::CreateContext());
        CHECK(context != nullptr);
        auto& io = ImGui::GetIO();
        io.DisplaySize = {1280.f, 800.f};
        io.DeltaTime = 1.f / 60.f;
        io.Fonts->Build();
        game::InitializeDungeonUi(registry);
    }
};

/// Draws the real title menu for three frames (the first two settle ImGui) and returns the frame-2 text log.
std::string DrawTitleAndCapture(entt::registry& registry, const std::filesystem::path& logPath) {
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        if (frame == 2) ImGui::LogToFile(0, logPath.string().c_str());
        game::DrawTitleMenu(registry);
        if (frame == 2) ImGui::LogFinish();
        ImGui::Render();
    }
    return ReadText(logPath);
}

void TitleInfoLineDrawsMetaSummary() {
    const FScratchDir dirScratch("title-meta");
    const auto& dir = dirScratch.path;
    FTitleFixture fixture(dir, 4);
    CHECK(game::InitializeMetaProgress(fixture.registry, fixture.meta));
    game::GrantSeals(fixture.registry, 7, "title test");
    CHECK(game::MetaSeals(fixture.registry) == 7);
    // Pin the format without depending on the separator's encoding (the runtime target compiles with
    // /utf-8, this test target does not): prefix + payload + suffix.
    const std::string summary = game::MetaSummaryLine(fixture.registry);
    CHECK(summary.starts_with("Seals 7 "));
    CHECK(summary.find("Best dungeon 4") != std::string::npos);
    CHECK(summary.ends_with(" Clears 0"));

    const auto log = DrawTitleAndCapture(fixture.registry, dir / "title.log");
    CHECK(log.find("Seals 7") != std::string::npos); // the summary really reaches the draw path
    CHECK(log.find("Best dungeon 4") != std::string::npos);

    // A disabled session says so instead of the numbers.
    game::ShutdownMetaProgress(fixture.registry);
    WriteText(fixture.meta, json{{"schema_version", 2}}.dump());
    CHECK(game::InitializeMetaProgress(fixture.registry, fixture.meta));
    CHECK(game::MetaDisabled(fixture.registry));
    const auto disabledLog = DrawTitleAndCapture(fixture.registry, dir / "title-disabled.log");
    CHECK(disabledLog.find("meta disabled") != std::string::npos);
    CHECK(disabledLog.find("Seals ") == std::string::npos);

    // Never initialized: meta draws nothing at all.
    game::ShutdownMetaProgress(fixture.registry);
    const auto emptyLog = DrawTitleAndCapture(fixture.registry, dir / "title-empty.log");
    CHECK(emptyLog.find("Seals") == std::string::npos);
    CHECK(emptyLog.find("meta disabled") == std::string::npos);
}

} // namespace

int main() {
    return game_test::RunTests("meta save", {
        {"RoundTripAndCorruption", RoundTripAndCorruption},
        {"SealEconomy", SealEconomy},
        {"FirstClearOnlyGrantsOnce", FirstClearOnlyGrantsOnce},
        {"CycleDoesNotRegrant", CycleDoesNotRegrant},
        {"FailureGrantsNothing", FailureGrantsNothing},
        {"AbandonGrantsNothing", AbandonGrantsNothing},
        {"BossClearGrantsOneAndTwo", BossClearGrantsOneAndTwo},
        {"ClearAndBossKeysNameSameDungeon", ClearAndBossKeysNameSameDungeon},
        {"ReplayAfterUnsavedRunDoesNotRegrant", ReplayAfterUnsavedRunDoesNotRegrant},
        {"RankIsOneUntilWp18", RankIsOneUntilWp18},
        {"GrandSetOnFinalBossOnly", GrandSetOnFinalBossOnly},
        {"BossDefeatedExternalAppend", BossDefeatedExternalAppend},
        {"ListenerRegisteredOnce", ListenerRegisteredOnce},
        {"MetaListenerSurvivesNewRun", MetaListenerSurvivesNewRun},
        {"ListenerOrderIndependent", ListenerOrderIndependent},
        {"LadderRecordsBest", LadderRecordsBest},
        {"SuccessMilestones", SuccessMilestones},
        {"CheckpointV1ImportsIntoMeta", CheckpointV1ImportsIntoMeta},
        {"ImportOnlyWhenMetaMissing", ImportOnlyWhenMetaMissing},
        {"ImportRunsAfterDungeonInit", ImportRunsAfterDungeonInit},
        {"CheckpointCorruptImportsZero", CheckpointCorruptImportsZero},
        {"GrantedListParses", GrantedListParses},
        {"LadderKeysPositiveIntegers", LadderKeysPositiveIntegers},
        {"NoPowerFields", NoPowerFields},
        {"UnlockCatalogValid", UnlockCatalogValid},
        {"RosterStubLocked", RosterStubLocked},
        {"UnlockIdsUnique", UnlockIdsUnique},
        {"CheckpointAndLadderAgreeAfterRealClear", CheckpointAndLadderAgreeAfterRealClear},
        {"LadderCheckpointCappedLikeCheckpointFile", LadderCheckpointCappedLikeCheckpointFile},
        {"NewCatalogRosterRowsMergeIntoSave", NewCatalogRosterRowsMergeIntoSave},
        {"MissingUnlocksCatalogFailsInit", MissingUnlocksCatalogFailsInit},
        {"InvalidRosterFailsInit", InvalidRosterFailsInit},
        {"MetaUsesIsolatedDir", MetaUsesIsolatedDir},
        {"MetaBesideCheckpointInTests", MetaBesideCheckpointInTests},
        {"OverrideNeverFallsBack", OverrideNeverFallsBack},
        {"CorruptMetaBackedUpThenDefaults", CorruptMetaBackedUpThenDefaults},
        {"FutureVersionNotOverwrittenEvenAfterGrant", FutureVersionNotOverwrittenEvenAfterGrant},
        {"FutureVersionSessionReadOnly", FutureVersionSessionReadOnly},
        {"RunSaveUntouchedByMetaCorruption", RunSaveUntouchedByMetaCorruption},
        {"ShutdownWithoutInitIsNoop", ShutdownWithoutInitIsNoop},
        {"TitleInfoLineDrawsMetaSummary", TitleInfoLineDrawsMetaSummary},
    });
}
