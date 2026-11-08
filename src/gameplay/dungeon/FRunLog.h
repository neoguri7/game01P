#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

namespace game {

/// The run log's schema version. Every line of one run's file carries this version in its `log_session`
/// header; a compatible optional key never needs a version bump (WP-19a §schema).
inline constexpr int kRunLogSchemaVersion = 1;

/// How many per-run log files a directory keeps. Pruning runs once, when the directory is first resolved:
/// the newest kRunLogRetention `*.jsonl` files survive by last_write_time, the rest are deleted unparsed.
inline constexpr std::size_t kRunLogRetention = 200;

/// Per-battle accumulation for one room (WP-19a). It lives in the gameplay layer (this header) so the
/// frame-by-frame accumulation code, which needs the ECS battle context, can live in its system header
/// without making gameplay depend on ecs/systems.
///
/// Keys are the unit's `FUnitData.definition.id` — the same id `CaptureRunParty` matches against
/// `FRunMember::id`. `hpStart` is snapshotted when a new battle session appears, `hpMin` is the frame
/// minimum; the room's HP loss is `hpStart - hpMin`, before CaptureRunParty's max/3 clamp.
struct FRoomBattleStats {
    int rounds{0};
    bool ko{false};
    std::map<std::string, int> hpStart;
    std::map<std::string, int> hpMin;
    std::map<std::string, int> skillsUsed;
    std::uint64_t lastSeq{0};
    std::uint64_t sessionId{0};
    bool active{false};
};

/// The one fatal enemy intent facts set the run-end screen may show (WP-19a E8). Facts only: what was
/// declared at commit, what was expected at execution, and what actually happened. Written into
/// `run_end.fatal_intent` only when `present`.
struct FRunFatalIntent {
    bool present{false};
    std::string skillId;
    int expectedAtCommit{0};
    int expectedAtExecution{0};
    int actualDamage{0};
    bool hit{false};
};

/// One run's structured log: the JSONL lines kept in memory (the whole log across Continue), the file
/// identity, and the run-scoped facts the recorder and the run-end report need.
struct FRunLog {
    std::string logId;         // "run-<seed>-<n>"; empty until the first write
    std::string path;          // "<dir>/<log_id>.jsonl"; empty = file writing is off for this run
    std::string runStartedAt;  // UTC ISO
    std::uint32_t seed{0};
    std::uint64_t runStartFrame{0};
    std::uint64_t runStartMs{0};
    int lastEnteredDepth{-1};  // the last depth a dungeon_enter line was written for
    bool fileEnabled{true};
    bool headerWritten{false};   // this process wrote its log_session header already
    bool runStartWritten{false};
    int fileWarnings{0};         // stays 1: the first failure turns file writing off for this run
    std::vector<nlohmann::json> events;
    FRunFatalIntent fatalIntent;
};

/// Module wiring state (registry context): the directory override, whether it was resolved/pruned, and the
/// once-only registration guard. It survives a run restart; FRunLog itself does not.
struct FRunLogConfig {
    std::string directory;  // explicit override (tests); empty = derive from FDungeonSave::path
    bool resolved{false};
    bool pruned{false};
    bool initialized{false};
};

/// The M-FUN observation set (R3-review #1: values, never pass/fail thresholds).
struct FRunReport {
    int rooms{0};
    double avgRounds{0.0};
    int damageFreeRooms{0};
    int koRooms{0};
    double whiffRate{0.0};
    double decisionsPerDungeon{0.0};
    std::int64_t framesToFirstBattle{0};
    std::int64_t msToFirstBattle{0};
};

/// Registers the recorder's run-event listeners and the `log` save section, and (when `directory` is not
/// empty) points the module at an explicit log directory — the test hook. Safe to call twice on one
/// registry: listeners and the section are registered once, a new directory re-resolves and re-prunes.
void InitializeRunLog(entt::registry& registry, std::string directory = {});

/// Starts the per-run log: a fresh log id, the `log_session` header and the `run_start` line. Called at the
/// end of InstallDungeonRun, before the run is first saved (the save section carries the log id).
void BeginRunLog(entt::registry& registry);

/// `dungeon_enter` at most once per depth this log entered (BuildDungeonBattlefield's Battle guard).
void LogDungeonEnter(entt::registry& registry);

/// `room_start` for the current run room (after the room name is known).
void LogRoomStart(entt::registry& registry);

/// Appends one event. Without a run (or before the module is initialized) it is a harmless no-op — WP-25
/// calls this directly for `engine_ignition`. An event without a string `t` is dropped (LOG_ERROR); a
/// schema-invalid event logs LOG_ERROR in debug builds but is still recorded. File failures never throw.
void LogRunEvent(entt::registry& registry, nlohmann::json event);

/// The v1 required-key table (`t` plus one row per type). Unknown `t` is invalid.
[[nodiscard]] bool FRunEventSchemaValid(const nlohmann::json& event);

/// The report over the in-memory lines (whole run, including lines reloaded on Continue).
[[nodiscard]] FRunReport FRunReportSummarize(const FRunLog& log);

/// The report as the `run_end.report` object.
[[nodiscard]] nlohmann::json FRunReportToJson(const FRunReport& report);

/// Continue support: point `log` at its existing file, reload its lines into memory (unparsable lines are
/// skipped) and install it in the context. Never throws.
void AttachRunLogToFile(entt::registry& registry, FRunLog log);

[[nodiscard]] std::string RunLogUtcTimestamp();
[[nodiscard]] std::uint64_t RunLogFrameIndex(const entt::registry& registry);
[[nodiscard]] std::uint64_t RunLogMilliseconds();

} // namespace game
