// WP-19a: the run log schema (JSONL), its recording points, the battle accumulation, the
// report metrics and the minimal run-end text.
//
// Every log file this suite writes stays in a per-test directory under `out/` (or the committed
// GAME01P_SAVE_DIR override) — TestsNeverTouchRealSave asserts it.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/EngineState.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "states/FEmberRunEndState.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

// --- scratch directories -----------------------------------------------------------------------

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string UniqueDir(const char* stem) {
    const std::filesystem::path directory = std::filesystem::path("out") / UniqueStem(stem);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory.string();
}

bool Under(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    return !relative.empty() && relative.begin()->string() != "..";
}

// --- a booted registry with its own save slot and run-log directory ----------------------------

struct FBoot {
    std::string directory;
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;

    explicit FBoot(std::string dir) : directory(std::move(dir)) {
        std::filesystem::create_directories(directory);
        checkpoint = directory + "/checkpoint.json";
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
    }

    FBoot(const FBoot&) = delete;
    FBoot& operator=(const FBoot&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
    const game::FRunLog& log() { return registry.ctx().get<game::FRunLog>(); }
    std::filesystem::path logDir() const { return std::filesystem::path(runSave).parent_path() / "run_logs"; }
};

// --- JSON / file helpers -----------------------------------------------------------------------

std::vector<json> EventsOfType(const game::FRunLog& log, std::string_view type) {
    std::vector<json> lines;
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        const auto found = event.find("t");
        if (found != event.end() && found->is_string() && found->get<std::string>() == type) {
            lines.push_back(event);
        }
    }
    return lines;
}

std::vector<json> ReadJsonLines(const std::string& path) {
    std::ifstream file(path);
    CHECK(file.good());
    std::vector<json> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        lines.push_back(json::parse(line));
    }
    return lines;
}

int JsonlFileCount(const std::filesystem::path& directory) {
    std::error_code error;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jsonl") ++count;
    }
    return count;
}

std::string Joined(const std::vector<std::string>& lines) {
    std::string joined;
    for (const auto& line : lines) joined += line + "\n";
    return joined;
}

// --- battle fixture helpers --------------------------------------------------------------------

entt::entity AddPartyEntity(
    entt::registry& registry, const std::string& id, int health, int maximum) {
    const entt::entity entity = registry.create();
    registry.emplace<game::FPartyMember>(entity);
    registry.emplace<game::FUnitData>(
        entity, game::FUnitData{game::FUnitDefinition{id, id, game::EUnitFaction::Party}});
    registry.emplace<game::FHealth>(entity, game::FHealth{health, maximum});
    registry.emplace<game::FSkillLoadout>(entity);
    registry.emplace<game::FSpeed>(entity, game::FSpeed{10});
    registry.emplace<game::FTurnParticipant>(entity);
    return entity;
}

void AddLivingEnemy(entt::registry& registry) {
    const entt::entity entity = registry.create();
    registry.emplace<game::FEnemy>(entity);
    registry.emplace<game::FHealth>(entity, game::FHealth{10, 10});
}

void StartBattleSession(entt::registry& registry, std::uint32_t seed = 7) {
    if (!registry.ctx().contains<game::FTurnRuntime>()) registry.ctx().emplace<game::FTurnRuntime>();
    if (!registry.ctx().contains<game::FBattleOutcome>()) registry.ctx().emplace<game::FBattleOutcome>();
    if (!registry.ctx().contains<game::FEventBus>()) registry.ctx().emplace<game::FEventBus>();
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.start(registry, seed).has_value());
}

/// One simulated frame: `rounds` FRoundStarted events for the current session, then the outcome system.
void BattleFrame(entt::registry& registry, int rounds = 0) {
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    const game::FTurnSnapshot snapshot = runtime.controller.snapshot();
    auto& bus = registry.ctx().get<game::FEventBus>();
    for (int index = 0; index < rounds; ++index) bus.queueFrame(game::FRoundStarted{snapshot});
    game::ecs::CombatOutcomeSystem{}.update(registry, 0.f);
    bus.beginFrame();
}

// ---------------------------------------------------------------------------------------------
// Schema + file tests
// ---------------------------------------------------------------------------------------------

void RunStartLogged() {
    FBoot boot(UniqueDir("run-log-start"));
    game::StartRun(boot.registry, kSeed, true);
    const auto& log = boot.log();
    CHECK(!log.logId.empty());
    CHECK(log.logId.rfind("run-4242-", 0) == 0);
    const auto expectedPath =
        (std::filesystem::path(boot.directory) / "run_logs" / (log.logId + ".jsonl")).string();
    CHECK(log.path == expectedPath);
    CHECK(std::filesystem::exists(expectedPath));

    const auto starts = EventsOfType(log, "run_start");
    CHECK(starts.size() == 1u);
    CHECK(starts[0]["log_id"] == log.logId);
    CHECK(starts[0]["seed"] == kSeed);
    CHECK(starts[0]["depth"] == 1);
    CHECK(starts[0]["frame"].is_number());
    CHECK(starts[0]["ms"].is_number());
    CHECK(starts[0]["party"].is_array());
    CHECK(starts[0]["party"].size() == 4u);
    CHECK(starts[0]["party"][0]["id"] == "party.warrior");
    CHECK(starts[0]["party"][0]["skills"].is_array());
    CHECK(!starts[0]["party"][0]["skills"].empty());  // fallback = the content unit's skill list
    CHECK(starts[0]["party"][0]["runes"].is_array());
    CHECK(starts[0]["party"][0]["runes"].empty());
}

void LogPerRunFile() {
    FBoot boot(UniqueDir("run-log-file"));
    game::StartRun(boot.registry, 777, true);
    const std::string firstId = boot.log().logId;
    const std::string firstPath = boot.log().path;
    CHECK(std::filesystem::exists(firstPath));
    CHECK(JsonlFileCount(boot.logDir()) == 1);

    // The same seed in the same directory is a new run: a new file, never an append.
    game::StartRun(boot.registry, 777, true);
    CHECK(boot.log().logId == "run-777-2");
    CHECK(boot.log().path != firstPath);
    CHECK(std::filesystem::exists(boot.log().path));
    CHECK(JsonlFileCount(boot.logDir()) == 2);
    CHECK(!firstId.empty());
}

void LogRetentionPrunesOldFiles() {
    const std::string directory = UniqueDir("run-log-retention");
    for (int index = 1; index <= 205; ++index) {
        const auto path = std::filesystem::path(directory) / ("run-1-" + std::to_string(index) + ".jsonl");
        std::ofstream(path) << "{}\n";
        std::filesystem::last_write_time(path, std::filesystem::file_time_type{} + std::chrono::seconds(index));
    }
    entt::registry registry;
    game::InitializeRunLog(registry, directory);
    CHECK(JsonlFileCount(directory) == 200);
    for (int index = 1; index <= 5; ++index) {
        CHECK(!std::filesystem::exists(std::filesystem::path(directory) / ("run-1-" + std::to_string(index) + ".jsonl")));
    }
    CHECK(std::filesystem::exists(std::filesystem::path(directory) / "run-1-6.jsonl"));
    CHECK(std::filesystem::exists(std::filesystem::path(directory) / "run-1-205.jsonl"));
}

void AppendFailureDisablesFileOnly() {
    FBoot boot(UniqueDir("run-log-append-fail"));
    const auto blocker = std::filesystem::path(boot.directory) / "blocker";
    {
        std::ofstream file(blocker);
        file << "not a directory";
    }
    game::InitializeRunLog(boot.registry, (blocker / "run_logs").string());
    game::StartRun(boot.registry, kSeed, true);

    const auto& log = boot.log();
    CHECK(log.fileWarnings == 1);  // exactly one warning flag: later failures do not warn again
    CHECK(!log.fileEnabled);
    CHECK(EventsOfType(log, "run_start").size() == 1u);  // the in-memory log keeps working

    game::AppendRunEvent(boot.registry, game::ERunEventKind::RewardPicked, "rune.fire", 0);
    CHECK(boot.log().fileWarnings == 1);
    CHECK(EventsOfType(boot.log(), "offer").size() == 1u);
}

void ContinuedAppendsSameFile() {
    const std::string directory = UniqueDir("run-log-continue");
    std::string logId;
    std::string logPath;
    {
        FBoot boot(directory);
        game::StartRun(boot.registry, kSeed, true);
        CHECK(!game::BuildDungeonBattlefield(boot.registry).empty());
        game::AppendRunEvent(boot.registry, game::ERunEventKind::RoomCleared);
        CHECK(game::SaveDungeonRun(boot.registry));
        logId = boot.log().logId;
        logPath = boot.log().path;
        CHECK(std::filesystem::exists(logPath));
    }
    {
        FBoot boot(directory);
        CHECK(game::ContinueDungeonRun(boot.registry));
        CHECK(boot.log().logId == logId);
        CHECK(boot.log().path == logPath);
        CHECK(EventsOfType(boot.log(), "run_start").size() == 1u);  // the old history reloaded
        CHECK(JsonlFileCount(boot.logDir()) == 1);

        // The first write after a continue starts a new session header in the same file.
        game::AppendRunEvent(boot.registry, game::ERunEventKind::RewardPicked, "rune.fire", 0);
        CHECK(boot.log().path == logPath);
        CHECK(JsonlFileCount(boot.logDir()) == 1);
        const auto lines = ReadJsonLines(logPath);
        int sessions = 0;
        int offers = 0;
        int starts = 0;
        for (const auto& line : lines) {
            if (line["t"] == "log_session") ++sessions;
            if (line["t"] == "offer") ++offers;
            if (line["t"] == "run_start") ++starts;
        }
        CHECK(sessions == 2);
        CHECK(starts == 1);
        CHECK(offers == 1);
    }
}

void SchemaKeysPerType() {
    const std::map<std::string, std::vector<std::string>> required = {
        {"log_session", {"t", "schema_version", "log_id", "log_started_at"}},
        {"run_start", {"t", "log_id", "seed", "depth", "frame", "ms", "party"}},
        {"dungeon_enter", {"t", "depth"}},
        {"room_start", {"t", "depth", "room", "name", "frame"}},
        {"room_end", {"t", "depth", "room", "result", "rounds", "hp_loss", "skills_used", "intents"}},
        {"offer", {"t", "depth", "room", "picked", "skipped", "duplicate", "refund"}},
        {"event", {"t", "depth", "room", "event", "choice"}},
        {"camp", {"t", "depth", "id", "amount"}},  // member is optional (W6 forward compatibility)
        {"run_end", {"t", "result", "depth", "dungeons_cleared", "reason", "frames_to_first_battle",
                     "ms_to_first_battle", "report"}},
        {"dungeon_clear", {"t", "depth"}},
        {"engine_ignition", {"t", "depth", "room", "round", "engine"}},
    };

    FBoot boot(UniqueDir("run-log-schema"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    CHECK(!game::BuildDungeonBattlefield(registry).empty());

    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, 0));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ChooseEventLoot).has_value());
    CHECK(!game::BuildDungeonBattlefield(registry).empty());

    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, -1));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(!game::BuildDungeonBattlefield(registry).empty());

    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, -1));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Camp);

    boot.run().materials = 10;
    CHECK(!game::UpgradeDungeonStat(registry, 0, 0).has_value());
    game::LogRunEvent(registry, json{{"t", "engine_ignition"}, {"depth", 1}, {"room", 0}, {"round", 1},
                                     {"engine", "ember"}});
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::AbandonRun).has_value());

    const auto& log = boot.log();
    for (const auto& [type, keys] : required) {
        const auto lines = EventsOfType(log, type);
        CHECK(!lines.empty());
        CHECK(game::FRunEventSchemaValid(lines.front()));
        for (const auto& key : keys) {
            auto broken = lines.front();
            broken.erase(key);
            CHECK(!game::FRunEventSchemaValid(broken));
        }
    }
    CHECK(!game::FRunEventSchemaValid(json{{"t", "not_a_type"}}));
    CHECK(!game::FRunEventSchemaValid(json{{"no_t", 1}}));
    for (const auto& event : log.events) CHECK(game::FRunEventSchemaValid(event));
}

// ---------------------------------------------------------------------------------------------
// Report + decisions
// ---------------------------------------------------------------------------------------------

void ReportSummarize() {
    game::FRunLog log;
    log.events = {
        json{{"t", "run_start"}, {"frame", 100}, {"ms", 1000}},
        json{{"t", "dungeon_enter"}, {"depth", 1}},
        json{{"t", "room_start"}, {"depth", 1}, {"room", 0}, {"frame", 160}, {"ms", 1600}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array()}, {"intents", {{"committed", 5}, {"whiffed", 1}}}, {"ko", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "win"}, {"rounds", 2},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 5}}})},
             {"intents", {{"committed", 3}, {"whiffed", 0}}}, {"ko", true}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 0}},
        json{{"t", "event"}, {"depth", 1}, {"room", 1}},
        json{{"t", "camp"}, {"depth", 1}, {"id", "upgrade.attack"}, {"amount", 2}},
        json{{"t", "dungeon_enter"}, {"depth", 2}},
    };
    const game::FRunReport report = game::FRunReportSummarize(log);
    CHECK(report.rooms == 2);
    CHECK(std::abs(report.avgRounds - 3.0) < 1e-9);
    CHECK(report.damageFreeRooms == 1);
    CHECK(report.koRooms == 1);
    CHECK(std::abs(report.whiffRate - 0.125) < 1e-9);
    CHECK(std::abs(report.decisionsPerDungeon - 1.5) < 1e-9);
    CHECK(report.framesToFirstBattle == 60);
    CHECK(report.msToFirstBattle == 600);
}

void OfferCampEventLogged() {
    FBoot boot(UniqueDir("run-log-decisions"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    CHECK(!game::BuildDungeonBattlefield(registry).empty());

    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    const std::string picked = registry.ctx().get<game::FRewards>().offer.candidates.at(0);
    CHECK(game::ResolveRuneOffer(registry, 0));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ChooseEventRest).has_value());

    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, -1));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, -1));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Camp);
    boot.run().materials = 10;
    CHECK(!game::UpgradeDungeonStat(registry, 0, 0).has_value());

    const auto& log = boot.log();
    const auto offers = EventsOfType(log, "offer");
    CHECK(offers.size() == 3u);
    CHECK(offers[0]["picked"] == picked);
    CHECK(offers[0]["skipped"] == false);
    CHECK(offers[0]["duplicate"] == false);
    CHECK(offers[0]["refund"] == 0);
    CHECK(offers[1]["picked"] == "");
    CHECK(offers[1]["skipped"] == true);
    CHECK(offers[1]["refund"] == 1);
    CHECK(offers[1]["depth"] == 1);
    CHECK(offers[1]["room"] == 1);

    const auto events = EventsOfType(log, "event");
    CHECK(events.size() == 1u);
    CHECK(events[0]["event"] == "event.quiet_shrine");
    CHECK(events[0]["choice"] == 0);
    CHECK(events[0]["depth"] == 1);
    CHECK(events[0]["room"] == 0);

    const auto camps = EventsOfType(log, "camp");
    CHECK(camps.size() == 1u);
    CHECK(camps[0]["id"] == "upgrade.attack");
    CHECK(camps[0]["amount"] == 2);
    CHECK(camps[0]["member"] == 0);
    CHECK(camps[0]["depth"] == 1);
}

void DecisionCountIncludesEvents() {
    game::FRunLog log;
    log.events = {
        json{{"t", "dungeon_enter"}, {"depth", 1}},
        json{{"t", "offer"}, {"depth", 1}},
        json{{"t", "event"}, {"depth", 1}},
        json{{"t", "camp"}, {"depth", 1}},
        json{{"t", "dungeon_enter"}, {"depth", 2}},
    };
    const game::FRunReport report = game::FRunReportSummarize(log);
    CHECK(std::abs(report.decisionsPerDungeon - 1.5) < 1e-9);
}

void WhiffRateGuardDiv0() {
    game::FRunLog empty;
    CHECK(game::FRunReportSummarize(empty).whiffRate == 0.0);
    game::FRunLog noIntents;
    noIntents.events = {json{{"t", "room_end"}, {"rounds", 2},
                             {"intents", {{"committed", 0}, {"whiffed", 0}}}}};
    CHECK(game::FRunReportSummarize(noIntents).whiffRate == 0.0);
}

void FramesToFirstBattleCaptured() {
    game::FRunLog log;
    log.events = {
        json{{"t", "run_start"}, {"frame", 100}, {"ms", 1000}},
        json{{"t", "room_start"}, {"depth", 1}, {"room", 0}, {"frame", 220}, {"ms", 1500}},
    };
    const game::FRunReport report = game::FRunReportSummarize(log);
    CHECK(report.framesToFirstBattle == 120);
    CHECK(report.msToFirstBattle == 500);
    CHECK(game::FRunReportSummarize(game::FRunLog{}).framesToFirstBattle == 0);
}

/// The first-battle interval is only meaningful inside one process: a `log_session` header written between
/// the run start and the first battle (a continue) restarts the frame counter and the steady clock, and a
/// synthesized run_start (a pre-19a save adopted by the log) is not a real start at all. Both stay 0.
void FirstBattleIntervalIgnoresContinueAndSynthesizedStart() {
    game::FRunLog continued;
    continued.events = {
        json{{"t", "run_start"}, {"frame", 100}, {"ms", 1000}},
        json{{"t", "log_session"}, {"schema_version", 1}, {"log_id", "run-1-1"}, {"log_started_at", "2026-11-08T00:00:00Z"}},
        json{{"t", "room_start"}, {"depth", 1}, {"room", 0}, {"frame", 10}, {"ms", 20}},
    };
    const game::FRunReport continuedReport = game::FRunReportSummarize(continued);
    CHECK(continuedReport.framesToFirstBattle == 0);
    CHECK(continuedReport.msToFirstBattle == 0);

    game::FRunLog synthesized;
    synthesized.events = {
        json{{"t", "run_start"}, {"frame", 0}, {"ms", 0}, {"synthesized", true}},
        json{{"t", "room_start"}, {"depth", 2}, {"room", 1}, {"frame", 900}, {"ms", 12000}},
    };
    const game::FRunReport synthesizedReport = game::FRunReportSummarize(synthesized);
    CHECK(synthesizedReport.framesToFirstBattle == 0);
    CHECK(synthesizedReport.msToFirstBattle == 0);

    // A real run start with no session boundary in between still reports the interval.
    game::FRunLog fresh;
    fresh.events = {
        json{{"t", "log_session"}, {"schema_version", 1}, {"log_id", "run-1-1"}, {"log_started_at", "2026-11-08T00:00:00Z"}},
        json{{"t", "run_start"}, {"frame", 40}, {"ms", 400}},
        json{{"t", "room_start"}, {"depth", 1}, {"room", 0}, {"frame", 90}, {"ms", 1400}},
    };
    const game::FRunReport freshReport = game::FRunReportSummarize(fresh);
    CHECK(freshReport.framesToFirstBattle == 50);
    CHECK(freshReport.msToFirstBattle == 1000);
}

// ---------------------------------------------------------------------------------------------
// Battle accumulation
// ---------------------------------------------------------------------------------------------

void RoundsCountedAcrossTurns() {
    FBoot boot(UniqueDir("run-log-rounds"));
    game::StartRun(boot.registry, kSeed, true);
    auto& registry = boot.registry;
    AddPartyEntity(registry, "party.warrior", 30, 30);
    AddLivingEnemy(registry);
    StartBattleSession(registry);
    const std::uint64_t session = registry.ctx().get<game::FTurnRuntime>().controller.snapshot().sessionId;
    CHECK(session != 0);

    BattleFrame(registry, 1);
    BattleFrame(registry, 1);
    BattleFrame(registry, 1);
    auto* stats = registry.ctx().find<game::FRoomBattleStats>();
    CHECK(stats != nullptr);
    CHECK(stats->rounds == 3);
    CHECK(stats->sessionId == session);

    // finishSession resets roundNumber to 0 but keeps the session id: the count is event-based, not a read
    // of the controller's current round.
    registry.ctx().get<game::FTurnRuntime>().controller.stop();
    CHECK(registry.ctx().get<game::FTurnRuntime>().controller.snapshot().roundNumber == 0);
    BattleFrame(registry, 0);
    CHECK(stats->rounds == 3);
}

void HpLossIgnoresClamp() {
    FBoot boot(UniqueDir("run-log-hp"));
    game::StartRun(boot.registry, kSeed, true);
    auto& registry = boot.registry;
    const entt::entity warrior = AddPartyEntity(registry, "party.warrior", 30, 30);
    AddLivingEnemy(registry);
    StartBattleSession(registry);
    BattleFrame(registry, 1);

    auto* stats = registry.ctx().find<game::FRoomBattleStats>();
    CHECK(stats != nullptr && stats->hpMin.at("party.warrior") == 30);
    registry.get<game::FHealth>(warrior).current = 5;  // below max/3 = 10
    BattleFrame(registry, 1);
    CHECK(stats->hpMin.at("party.warrior") == 5);

    game::CaptureRunParty(registry);
    CHECK(boot.run().party[0].health == 10);  // CaptureRunParty clamps to max/3

    game::AppendRunEvent(registry, game::ERunEventKind::RoomCleared);
    const auto rooms = EventsOfType(boot.log(), "room_end");
    CHECK(rooms.size() == 1u);
    CHECK(rooms[0]["hp_loss"].size() == 1u);
    CHECK(rooms[0]["hp_loss"][0]["id"] == "party.warrior");
    CHECK(rooms[0]["hp_loss"][0]["loss"] == 25);  // 30 - 5, not 30 - 10
}

void SkillsCountedFromCommandResults() {
    FBoot boot(UniqueDir("run-log-skills"));
    game::StartRun(boot.registry, kSeed, true);
    auto& registry = boot.registry;
    AddPartyEntity(registry, "party.warrior", 30, 30);
    AddLivingEnemy(registry);
    StartBattleSession(registry);
    const std::uint64_t session = registry.ctx().get<game::FTurnRuntime>().controller.snapshot().sessionId;

    const auto skillResult = [](const std::string& skill, std::uint64_t seq, game::ECommandReject reject) {
        game::FBattleCommand command;
        game::FActionRequest request;
        request.skillId = skill;
        command.action = request;
        command.seq = seq;
        command.source = game::ECommandSource::Player;
        return game::FCommandResult{command, reject, {}};
    };
    game::FCommandResults results;
    results.sessionId = session;
    game::FBattleCommand move;
    move.action = game::FMoveRequest{};
    move.seq = 1;
    results.recent.push_back(game::FCommandResult{move, game::ECommandReject::None, {}});
    results.recent.push_back(skillResult("skill.strike", 2, game::ECommandReject::None));
    results.recent.push_back(skillResult("skill.rejected", 0, game::ECommandReject::NoAp));
    results.recent.push_back(skillResult("skill.counter", 3, game::ECommandReject::None));
    registry.ctx().emplace<game::FCommandResults>(std::move(results));
    BattleFrame(registry, 1);

    auto* stats = registry.ctx().find<game::FRoomBattleStats>();
    CHECK(stats != nullptr);
    CHECK(stats->skillsUsed.size() == 2u);
    CHECK(stats->skillsUsed.at("skill.strike") == 1);
    CHECK(stats->skillsUsed.at("skill.counter") == 1);
    CHECK(stats->skillsUsed.find("skill.rejected") == stats->skillsUsed.end());
    CHECK(stats->lastSeq == 3);

    // A new battle session resets the cursor and the counts.
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    runtime.controller.stop();
    CHECK(runtime.controller.start(registry, 8).has_value());
    const std::uint64_t nextSession = runtime.controller.snapshot().sessionId;
    CHECK(nextSession != session);
    game::FCommandResults fresh;
    fresh.sessionId = nextSession;
    fresh.recent.push_back(skillResult("skill.shove", 1, game::ECommandReject::None));
    registry.ctx().erase<game::FCommandResults>();
    registry.ctx().emplace<game::FCommandResults>(std::move(fresh));
    BattleFrame(registry, 1);
    CHECK(stats->sessionId == nextSession);
    CHECK(stats->skillsUsed.size() == 1u);
    CHECK(stats->skillsUsed.at("skill.shove") == 1);
    CHECK(stats->lastSeq == 1);
}

void WipeWritesDefeatRoomEnd() {
    FBoot boot(UniqueDir("run-log-wipe"));
    game::StartRun(boot.registry, kSeed, true);
    auto& registry = boot.registry;
    auto& stats = registry.ctx().emplace<game::FRoomBattleStats>();
    stats.sessionId = 1;
    stats.active = true;
    stats.rounds = 5;
    stats.ko = true;
    stats.hpStart["party.warrior"] = 30;
    stats.hpMin["party.warrior"] = 0;
    stats.skillsUsed["skill.strike"] = 2;

    // WP-11: the battle's intent telemetry rides on the same session id.
    auto& intents = registry.ctx().emplace<game::FIntentStore>();
    intents.sessionId = 1;
    intents.stats.committed = 3;
    intents.stats.tileCommitted = 1;
    intents.stats.unitCommitted = 2;
    intents.stats.moveIntents = 1;
    intents.stats.whiffed = 1;
    intents.stats.invalidated = 0;
    intents.stats.lastExecuted.skillId = "skill.maul";
    intents.stats.lastExecuted.expectedAtCommit = 8;
    intents.stats.lastExecuted.expectedAtExecution = 6;
    intents.stats.lastExecuted.actualDamage = 6;
    intents.stats.lastExecuted.hit = true;

    CHECK(game::ResolveRoomVictory(registry, false).has_value());

    const auto rooms = EventsOfType(boot.log(), "room_end");
    CHECK(rooms.size() == 1u);
    CHECK(rooms[0]["result"] == "defeat");
    CHECK(rooms[0]["rounds"] == 5);
    CHECK(rooms[0]["ko"] == true);
    CHECK(rooms[0]["hp_loss"][0]["id"] == "party.warrior");
    CHECK(rooms[0]["hp_loss"][0]["loss"] == 30);
    const auto& roomIntents = rooms[0]["intents"];
    CHECK(roomIntents["committed"] == 3);
    CHECK(roomIntents["tileCommitted"] == 1);
    CHECK(roomIntents["unitCommitted"] == 2);
    CHECK(roomIntents["moveIntents"] == 1);
    CHECK(roomIntents["whiffed"] == 1);
    CHECK(roomIntents["invalidated"] == 0);
    CHECK(roomIntents["committed"].get<int>() ==
          roomIntents["tileCommitted"].get<int>() + roomIntents["unitCommitted"].get<int>());

    const auto ends = EventsOfType(boot.log(), "run_end");
    CHECK(ends.size() == 1u);
    CHECK(ends[0]["result"] == "defeat");
    CHECK(ends[0]["reason"] == "wipe");
    CHECK(ends[0]["report"]["rooms"] == 1);
    CHECK(ends[0]["report"]["ko_rooms"] == 1);
    CHECK(ends[0]["fatal_intent"]["skill"] == "skill.maul");
    CHECK(ends[0]["fatal_intent"]["expected_at_execution"] == 6);
    CHECK(ends[0]["fatal_intent"]["actual_damage"] == 6);
}

void IntentStatsFromAnotherBattleIgnored() {
    FBoot boot(UniqueDir("run-log-intents-other-battle"));
    game::StartRun(boot.registry, kSeed, true);
    auto& registry = boot.registry;
    auto& stats = registry.ctx().emplace<game::FRoomBattleStats>();
    stats.sessionId = 2;
    stats.active = true;
    // The store still holds the previous battle's numbers (nothing committed in this one).
    auto& intents = registry.ctx().emplace<game::FIntentStore>();
    intents.sessionId = 1;
    intents.stats.committed = 3;
    intents.stats.tileCommitted = 1;
    intents.stats.unitCommitted = 2;
    intents.stats.moveIntents = 4;
    intents.stats.whiffed = 1;
    intents.stats.lastExecuted.skillId = "skill.maul";
    intents.stats.lastExecuted.expectedAtExecution = 6;

    CHECK(game::ResolveRoomVictory(registry, false).has_value());

    const auto rooms = EventsOfType(boot.log(), "room_end");
    CHECK(rooms.size() == 1u);
    const auto& roomIntents = rooms[0]["intents"];
    CHECK(roomIntents["committed"] == 0);
    CHECK(roomIntents["tileCommitted"] == 0);
    CHECK(roomIntents["unitCommitted"] == 0);
    CHECK(roomIntents["whiffed"] == 0);
    CHECK(roomIntents["invalidated"] == 0);
    CHECK(roomIntents["moveIntents"] == 0);
    const auto ends = EventsOfType(boot.log(), "run_end");
    CHECK(ends.size() == 1u);
    CHECK(ends[0]["result"] == "defeat");
    CHECK(!ends[0].contains("fatal_intent"));
}

// ---------------------------------------------------------------------------------------------
// Run-end minimal summary
// ---------------------------------------------------------------------------------------------

void MinimalSummaryShows() {
    game::FRunLog log;
    log.events = {
        json{{"t", "run_start"}, {"frame", 10}, {"ms", 100}},
        json{{"t", "dungeon_enter"}, {"depth", 1}},
        json{{"t", "room_start"}, {"depth", 1}, {"room", 0}, {"frame", 20}, {"ms", 150}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 0}}})},
             {"intents", {{"committed", 4}, {"whiffed", 0}}}, {"ko", false}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 0}, {"picked", "rune.fire"}, {"skipped", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "win"}, {"rounds", 6},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 12}}})},
             {"intents", {{"committed", 4}, {"whiffed", 0}}}, {"ko", true}},
        json{{"t", "camp"}, {"depth", 1}, {"id", "upgrade.attack"}, {"amount", 4}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 2}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 0}}})},
             {"intents", {{"committed", 4}, {"whiffed", 0}}}, {"ko", false}},
    };
    const std::string text = Joined(BuildRunEndSummaryLines(log));
    // ① the five report metrics
    CHECK(text.find("Average rounds per room: 5.0") != std::string::npos);
    CHECK(text.find("Damage-free rooms: 2 of 3 (67%)") != std::string::npos);
    CHECK(text.find("Rooms with a KO: 1 of 3 (33%)") != std::string::npos);
    CHECK(text.find("Intent whiff rate: 0%") != std::string::npos);
    CHECK(text.find("Decisions per dungeon: 2.0") != std::string::npos);
    // ② one line per room, with the pick and the depth's camp spend
    CHECK(text.find("Depth 1 room 0") != std::string::npos);
    CHECK(text.find("picked rune.fire") != std::string::npos);
    CHECK(text.find("camp spent 4") != std::string::npos);
    // ③ the empty fatal intent statement (a log that never recorded one still says so)
    CHECK(text.find("No enemy intent was recorded.") != std::string::npos);

    // The real path: a wipe copies the battle's last executed enemy intent into run_end, so the screen
    // shows the same facts the synthetic text above pins.
    FBoot wiped(UniqueDir("run-log-summary-fatal"));
    game::StartRun(wiped.registry, kSeed, true);
    auto& stats = wiped.registry.ctx().emplace<game::FRoomBattleStats>();
    stats.sessionId = 1;
    stats.active = true;
    auto& intents = wiped.registry.ctx().emplace<game::FIntentStore>();
    intents.sessionId = 1;
    intents.stats.lastExecuted.skillId = "skill.maul";
    intents.stats.lastExecuted.expectedAtCommit = 8;
    intents.stats.lastExecuted.expectedAtExecution = 6;
    intents.stats.lastExecuted.actualDamage = 6;
    intents.stats.lastExecuted.hit = true;
    CHECK(game::ResolveRoomVictory(wiped.registry, false).has_value());
    const std::string fatal = Joined(BuildRunEndSummaryLines(*wiped.registry.ctx().find<game::FRunLog>()));
    CHECK(fatal.find("skill.maul") != std::string::npos);
    CHECK(fatal.find("declared 8 at commit") != std::string::npos);
    CHECK(fatal.find("6 at execution") != std::string::npos);
    CHECK(fatal.find("6 damage") != std::string::npos);
}

void FatalIntentIsFactual() {
    game::FRunLog log;
    log.fatalIntent = game::FRunFatalIntent{};
    log.fatalIntent.present = true;
    log.fatalIntent.skillId = "skill.maul";
    log.fatalIntent.expectedAtCommit = 8;
    log.fatalIntent.expectedAtExecution = 6;
    log.fatalIntent.actualDamage = 11;
    log.fatalIntent.hit = true;
    const std::string text = Joined(BuildRunEndSummaryLines(log));
    CHECK(text.find("skill.maul") != std::string::npos);
    CHECK(text.find("declared 8 at commit") != std::string::npos);
    CHECK(text.find("6 at execution") != std::string::npos);
    CHECK(text.find("11 damage") != std::string::npos);
    CHECK(text.find("would") == std::string::npos);
    CHECK(text.find("could") == std::string::npos);
    CHECK(text.find("should") == std::string::npos);

    const std::string none = Joined(BuildRunEndSummaryLines(game::FRunLog{}));
    CHECK(none.find("No enemy intent was recorded.") != std::string::npos);
    CHECK(none.find("Fatal intent:") == std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// Forward compatibility / failure isolation
// ---------------------------------------------------------------------------------------------

void EngineIgnitionAndCampWithoutMemberAccepted() {
    FBoot boot(UniqueDir("run-log-forward"));
    game::StartRun(boot.registry, kSeed, true);
    game::LogRunEvent(boot.registry, json{{"t", "engine_ignition"}, {"depth", 1}, {"room", 0},
                                          {"round", 1}, {"engine", "ember"}});
    const auto ignition = EventsOfType(boot.log(), "engine_ignition");
    CHECK(ignition.size() == 1u);
    CHECK(game::FRunEventSchemaValid(ignition[0]));

    // WP-10's camp purchases may omit `member`: the schema keeps it optional.
    const json campWithoutMember{{"t", "camp"}, {"depth", 1}, {"id", "upgrade.attack"}, {"amount", 2}};
    CHECK(game::FRunEventSchemaValid(campWithoutMember));
    json campWithMember = campWithoutMember;
    campWithMember["member"] = 0;
    CHECK(game::FRunEventSchemaValid(campWithMember));
    game::LogRunEvent(boot.registry, campWithoutMember);
    CHECK(EventsOfType(boot.log(), "camp").size() == 1u);
}

void LogRunEventWithoutRunLogIsNoOp() {
    entt::registry registry;
    game::LogRunEvent(registry, json{{"t", "engine_ignition"}, {"depth", 1}, {"room", 0}, {"round", 1},
                                     {"engine", "ember"}});
    CHECK(!registry.ctx().contains<game::FRunLog>());
    CHECK(!registry.ctx().contains<game::FRunLogConfig>());
}

void FailureDoesNotCrashLogging() {
    FBoot boot(UniqueDir("run-log-failure"));
    const auto blocker = std::filesystem::path(boot.directory) / "blocker";
    {
        std::ofstream file(blocker);
        file << "not a directory";
    }
    game::InitializeRunLog(boot.registry, (blocker / "run_logs").string());
    game::StartRun(boot.registry, kSeed, true);
    const auto& log = boot.log();
    const std::size_t before = log.events.size();

    // A missing `t` is dropped; a schema-invalid event is recorded (debug builds log an error).
    game::LogRunEvent(boot.registry, json{{"no_t", 1}});
    CHECK(boot.log().events.size() == before);
    game::LogRunEvent(boot.registry, json{{"t", "room_start"}});
    CHECK(boot.log().events.size() == before + 1);

    // The run keeps playing even though the file never opens.
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(EventsOfType(boot.log(), "room_end").size() == 1u);
    CHECK(boot.log().fileWarnings == 1);
}

void TestsNeverTouchRealSave() {
    const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
    CHECK(overrideDir != nullptr && *overrideDir != '\0');
    const std::filesystem::path overrideRoot(overrideDir);

    FBoot boot(UniqueDir("run-log-save-safety"));
    game::StartRun(boot.registry, kSeed, true);
    const auto& log = boot.log();
    CHECK(!log.path.empty());
    const std::filesystem::path path = std::filesystem::absolute(log.path).lexically_normal();
    CHECK(Under(path, std::filesystem::absolute(overrideRoot)) ||
          Under(path, std::filesystem::absolute("out")));
    CHECK(Under(path, std::filesystem::absolute(boot.directory)));
}

} // namespace

int main() {
    // Save safety (same pattern as effect_hooks_tests): a plain `ctest` exports no GAME01P_SAVE_DIR, so
    // commit an override before any dungeon service can touch the real save under %APPDATA%.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "run_log_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("run log", {
        {"RunStartLogged", RunStartLogged},
        {"LogPerRunFile", LogPerRunFile},
        {"LogRetentionPrunesOldFiles", LogRetentionPrunesOldFiles},
        {"AppendFailureDisablesFileOnly", AppendFailureDisablesFileOnly},
        {"ContinuedAppendsSameFile", ContinuedAppendsSameFile},
        {"SchemaKeysPerType", SchemaKeysPerType},
        {"ReportSummarize", ReportSummarize},
        {"OfferCampEventLogged", OfferCampEventLogged},
        {"RoundsCountedAcrossTurns", RoundsCountedAcrossTurns},
        {"HpLossIgnoresClamp", HpLossIgnoresClamp},
        {"SkillsCountedFromCommandResults", SkillsCountedFromCommandResults},
        {"DecisionCountIncludesEvents", DecisionCountIncludesEvents},
        {"WhiffRateGuardDiv0", WhiffRateGuardDiv0},
        {"FramesToFirstBattleCaptured", FramesToFirstBattleCaptured},
        {"FirstBattleIntervalIgnoresContinueAndSynthesizedStart", FirstBattleIntervalIgnoresContinueAndSynthesizedStart},
        {"MinimalSummaryShows", MinimalSummaryShows},
        {"FatalIntentIsFactual", FatalIntentIsFactual},
        {"FailureDoesNotCrashLogging", FailureDoesNotCrashLogging},
        {"EngineIgnitionAndCampWithoutMemberAccepted", EngineIgnitionAndCampWithoutMemberAccepted},
        {"LogRunEventWithoutRunLogIsNoOp", LogRunEventWithoutRunLogIsNoOp},
        {"WipeWritesDefeatRoomEnd", WipeWritesDefeatRoomEnd},
        {"IntentStatsFromAnotherBattleIgnored", IntentStatsFromAnotherBattleIgnored},
        {"TestsNeverTouchRealSave", TestsNeverTouchRealSave},
    });
}
