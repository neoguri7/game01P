#include "gameplay/dungeon/FRunLog.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/Logger.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FEncounterContent.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunLogSection.h"
#include "gameplay/dungeon/FRunSequence.h"

namespace game {
namespace {

// ---------------------------------------------------------------------------------------------
// Directory, id and file plumbing
// ---------------------------------------------------------------------------------------------

FRunLogConfig& EnsureConfig(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<FRunLogConfig>()) return *existing;
    return registry.ctx().emplace<FRunLogConfig>();
}

/// Deletes the oldest `*.jsonl` files beyond kRunLogRetention. Best effort and never throws: a directory
/// that cannot be listed is simply not pruned (no warning — the first write will warn if the dir is bad).
void PruneRunLogDirectory(const std::string& directory) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) return;
    struct FEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type written;
    };
    std::vector<FEntry> entries;
    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) return;
        std::error_code entryError;
        if (!iterator->is_regular_file(entryError) || entryError) continue;
        if (iterator->path().extension() != ".jsonl") continue;
        const auto written = iterator->last_write_time(entryError);
        entries.push_back(FEntry{iterator->path(), entryError ? std::filesystem::file_time_type::min() : written});
    }
    if (entries.size() <= kRunLogRetention) return;
    std::ranges::sort(entries, [](const FEntry& left, const FEntry& right) {
        if (left.written != right.written) return left.written > right.written;
        return left.path.filename().string() > right.path.filename().string();
    });
    for (std::size_t index = kRunLogRetention; index < entries.size(); ++index) {
        std::error_code removeError;
        std::filesystem::remove(entries[index].path, removeError);
    }
}

/// Resolves (once) and prunes (once) the run-log directory. The explicit override wins; otherwise the logs
/// live beside the save slot. An empty save path leaves the directory unresolved so a later write retries.
std::string ResolveDirectory(entt::registry& registry, FRunLogConfig& config) {
    if (config.resolved) return config.directory;
    std::string directory = config.directory;
    if (directory.empty()) {
        const auto* save = registry.ctx().find<FDungeonSave>();
        if (save != nullptr && !save->path.empty()) {
            const std::filesystem::path savePath(save->path);
            if (savePath.has_parent_path()) directory = (savePath.parent_path() / "run_logs").string();
        }
    }
    if (directory.empty()) return {};
    config.directory = directory;
    config.resolved = true;
    if (!config.pruned) {
        config.pruned = true;
        PruneRunLogDirectory(directory);
    }
    return directory;
}

/// "run-<seed>-<n>": n = 1 + the highest n already present for that seed in the directory (1 when the
/// directory is empty or unreadable). File names are the only state; nothing is parsed.
std::string NextRunLogId(const std::string& directory, std::uint32_t seed) {
    int highest = 0;
    if (!directory.empty()) {
        const std::string prefix = "run-" + std::to_string(seed) + "-";
        std::error_code error;
        std::filesystem::directory_iterator iterator(directory, error);
        const std::filesystem::directory_iterator end;
        for (; !error && iterator != end; iterator.increment(error)) {
            std::error_code entryError;
            if (!iterator->is_regular_file(entryError) || entryError) continue;
            const std::string name = iterator->path().filename().string();
            if (name.size() <= prefix.size() + 6 || name.compare(0, prefix.size(), prefix) != 0) continue;
            if (name.compare(name.size() - 6, 6, ".jsonl") != 0) continue;
            int value = 0;
            const char* first = name.data() + prefix.size();
            const char* last = name.data() + name.size() - 6;
            const auto parsed = std::from_chars(first, last, value);
            if (parsed.ec == std::errc{} && parsed.ptr == last && value > highest) highest = value;
        }
    }
    return "run-" + std::to_string(seed) + "-" + std::to_string(highest + 1);
}

void DisableRunLogFile(FRunLog& log, std::string_view reason) {
    if (!log.fileEnabled) return;
    log.fileEnabled = false;
    ++log.fileWarnings;
    LOG_WARN("Run log file writing is off for '{}': {}", log.path, reason);
}

bool WriteRunLogLine(FRunLog& log, const std::string& line) {
    if (!log.fileEnabled || log.path.empty()) return false;
    std::error_code error;
    const std::filesystem::path path(log.path);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    if (!error) {
        std::ofstream file(log.path, std::ios::app);
        if (file) {
            file << line << '\n';
            file.flush();
            if (file) return true;
        }
    }
    DisableRunLogFile(log, error ? error.message() : std::string_view("the file could not be opened or written"));
    return false;
}

/// One line in memory + on disk. The `log_session` header is written first (once per process/run file).
void AppendEventLine(FRunLog& log, nlohmann::json event) {
    if (log.fileEnabled && !log.path.empty() && !log.headerWritten) {
        nlohmann::json header{{"t", "log_session"},
                              {"schema_version", kRunLogSchemaVersion},
                              {"log_id", log.logId},
                              {"log_started_at", RunLogUtcTimestamp()}};
        log.events.push_back(header);
        if (WriteRunLogLine(log, header.dump())) log.headerWritten = true;
    }
    log.events.push_back(std::move(event));
    if (log.fileEnabled && !log.path.empty()) (void)WriteRunLogLine(log, log.events.back().dump());
}

FRunLog* EnsureRunLog(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<FRunLog>()) return existing;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return nullptr;
    auto& config = EnsureConfig(registry);
    const std::string directory = ResolveDirectory(registry, config);
    FRunLog log;
    log.seed = run->seed;
    log.runStartedAt = RunLogUtcTimestamp();
    log.runStartFrame = RunLogFrameIndex(registry);
    log.runStartMs = RunLogMilliseconds();
    log.logId = NextRunLogId(directory, run->seed);
    if (!directory.empty()) log.path = (std::filesystem::path(directory) / (log.logId + ".jsonl")).string();
    registry.ctx().erase<FRunLog>();
    return &registry.ctx().emplace<FRunLog>(std::move(log));
}

void EmitRunStart(entt::registry& registry, FRunLog& log, bool synthesized) {
    log.runStartWritten = true;
    const auto* run = registry.ctx().find<FDungeonRun>();
    nlohmann::json party = nlohmann::json::array();
    if (run != nullptr) {
        const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        const nlohmann::json* definitionParty = nullptr;
        if (registry.ctx().contains<FAssetManager>()) {
            const auto& data = DungeonDefinition(registry);
            if (const auto found = data.find("party"); found != data.end() && found->is_array()) {
                definitionParty = &*found;
            }
        }
        for (std::size_t index = 0; index < run->party.size(); ++index) {
            const FRunMember& member = run->party[index];
            nlohmann::json skills = nlohmann::json::array();
            if (!member.skills.empty()) {
                for (const auto& slot : member.skills) {
                    if (!slot.skillId.empty()) skills.push_back(slot.skillId);
                }
            } else if (content != nullptr && definitionParty != nullptr && index < definitionParty->size()) {
                const auto& definition = (*definitionParty)[index];
                if (const auto unit = definition.find("unit");
                    unit != definition.end() && unit->is_string()) {
                    if (const auto* row = content->units.find(unit->get<std::string>())) {
                        skills = row->skillIds;
                    }
                }
            }
            nlohmann::json runes = nlohmann::json::array();
            for (const auto& slot : member.skills) {
                for (const auto& runeId : slot.runeIds) runes.push_back(runeId);
            }
            party.push_back({{"id", member.id}, {"skills", std::move(skills)}, {"runes", std::move(runes)}});
        }
    }
    nlohmann::json event{{"t", "run_start"},
                         {"log_id", log.logId},
                         {"seed", run != nullptr ? run->seed : log.seed},
                         {"depth", run != nullptr ? run->depth : 1},
                         {"frame", log.runStartFrame},
                         {"ms", log.runStartMs},
                         {"party", std::move(party)}};
    // A pre-19a save gets a log on its first write; its run_start is a reconstruction at continue time, not
    // a real run start, so the report must not measure an interval from it (the counters restarted).
    if (synthesized) event["synthesized"] = true;
    AppendEventLine(log, std::move(event));
}

FRunLog* EnsureStartedRunLog(entt::registry& registry) {
    FRunLog* log = EnsureRunLog(registry);
    if (log == nullptr) return nullptr;
    if (!log->runStartWritten) EmitRunStart(registry, *log, true);
    return log;
}

// ---------------------------------------------------------------------------------------------
// Event handlers (the listeners registered by InitializeRunLog)
// ---------------------------------------------------------------------------------------------

/// Builds one `room_end` line from the battle accumulation and consumes it (counters zeroed, session kept
/// so the same battle never re-snapshots after a room is already recorded).
nlohmann::json BuildRoomEnd(entt::registry& registry, const FRunEvent& event, const char* result) {
    nlohmann::json line{{"t", "room_end"}, {"depth", event.depth}, {"room", event.room}, {"result", result}};
    nlohmann::json hpLoss = nlohmann::json::array();
    nlohmann::json skillsUsed = nlohmann::json::object();
    int rounds = 0;
    bool ko = false;
    // The battle identity, captured before the stats are consumed: the intent telemetry is battle-scoped,
    // so a room must never borrow the previous battle's numbers.
    std::uint64_t battleSession = 0;
    if (auto* stats = registry.ctx().find<FRoomBattleStats>()) {
        battleSession = stats->sessionId;
        rounds = stats->rounds;
        ko = stats->ko;
        for (const auto& [id, start] : stats->hpStart) {
            const auto minimum = stats->hpMin.find(id);
            const int lowest = minimum != stats->hpMin.end() ? minimum->second : start;
            hpLoss.push_back({{"id", id}, {"loss", std::max(0, start - lowest)}});
        }
        for (const auto& [skillId, count] : stats->skillsUsed) skillsUsed[skillId] = count;
        stats->rounds = 0;
        stats->ko = false;
        stats->hpStart.clear();
        stats->hpMin.clear();
        stats->skillsUsed.clear();
        stats->lastSeq = 0;
        stats->active = false;  // the session id stays: the same battle must not re-snapshot
    }
    line["rounds"] = rounds;
    line["hp_loss"] = std::move(hpLoss);
    line["skills_used"] = std::move(skillsUsed);
    nlohmann::json intents{{"committed", 0}, {"tileCommitted", 0}, {"unitCommitted", 0},
                           {"whiffed", 0}, {"invalidated", 0}, {"moveIntents", 0}};
    // WP-11: the enemy-intent telemetry is battle-scoped like FRoomBattleStats. Both ids are turn sessions;
    // when both sides know one, they must agree. A battle that never committed leaves the store on the
    // previous session, which this rejects, so its room_end stays all zeros.
    const FIntentStats* intentStats = FindIntentStats(registry);
    const auto* intentStore = registry.ctx().find<FIntentStore>();
    const bool otherBattle = intentStore != nullptr && battleSession != 0 && intentStore->sessionId != 0 &&
                             intentStore->sessionId != battleSession;
    if (intentStats != nullptr && !otherBattle) {
        intents["committed"] = intentStats->committed;
        intents["tileCommitted"] = intentStats->tileCommitted;
        intents["unitCommitted"] = intentStats->unitCommitted;
        intents["whiffed"] = intentStats->whiffed;
        intents["invalidated"] = intentStats->invalidated;
        intents["moveIntents"] = intentStats->moveIntents;
        if (std::string_view(result) == "defeat" && !intentStats->lastExecuted.skillId.empty()) {
            if (auto* log = registry.ctx().find<FRunLog>()) {
                const auto& last = intentStats->lastExecuted;
                log->fatalIntent = FRunFatalIntent{true, last.skillId, last.expectedAtCommit,
                                                   last.expectedAtExecution, last.actualDamage, last.hit};
            }
        }
    }
    line["intents"] = std::move(intents);
    line["ko"] = ko;
    // [WP-19b] The optional debrief keys (WP-14a's room plan, never saved): a legacy room (no plan, or a
    // plan without an encounter) carries none. Optional keys need no schema change (FRunEventSchemaValid
    // stays untouched).
    if (const FRoomPlan* plan = FindRoomPlan(registry); plan != nullptr && !plan->encounterId.empty()) {
        line["encounter"] = plan->encounterId;
        line["family"] = plan->family;
        line["elite"] = plan->elite;
    }
    return line;
}

void HandleRunEnded(entt::registry& registry, FRunLog& log, const FRunEvent& event) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    const bool defeat = run != nullptr && run->phase == EDungeonPhase::Defeat;
    if (defeat) {
        // W5 review risk 2: the lost room is a room_end like any other, so the report's room count and
        // averages include it. Written first; the run_end report below then sees it.
        LogRunEvent(registry, BuildRoomEnd(registry, event, "defeat"));
    }
    const FRunReport report = FRunReportSummarize(*registry.ctx().find<FRunLog>());
    nlohmann::json line{{"t", "run_end"},
                        {"result", defeat ? "defeat" : "abandon"},
                        {"depth", event.depth},
                        {"dungeons_cleared", run != nullptr ? run->cleared : 0},
                        {"reason", defeat ? "wipe" : "abandon"},
                        {"frames_to_first_battle", report.framesToFirstBattle},
                        {"ms_to_first_battle", report.msToFirstBattle},
                        {"report", FRunReportToJson(report)}};
    // [WP-19b] Optional `cycle` (absent without a sequence or a run): the debrief's cycle line reads it.
    // RunCycle is 0 while the run is still inside the first loop, and clamps rather than divides by zero.
    if (const FRunSequence* sequence = FindRunSequence(registry); sequence != nullptr && run != nullptr) {
        line["cycle"] = sequence->RunCycle(run->cleared);
    }
    if (log.fatalIntent.present) {
        line["fatal_intent"] = {{"skill", log.fatalIntent.skillId},
                                {"declared_at_commit", log.fatalIntent.expectedAtCommit},
                                {"expected_at_execution", log.fatalIntent.expectedAtExecution},
                                {"actual_damage", log.fatalIntent.actualDamage},
                                {"hit", log.fatalIntent.hit}};
    }
    LogRunEvent(registry, std::move(line));
}

void HandleRunEvent(entt::registry& registry, const FRunEvent& event) {
    if (EnsureRunLog(registry) == nullptr) return;
    switch (event.kind) {
    case ERunEventKind::RoomCleared:
        LogRunEvent(registry, BuildRoomEnd(registry, event, "win"));
        break;
    case ERunEventKind::DungeonCleared:
        LogRunEvent(registry, nlohmann::json{{"t", "dungeon_clear"}, {"depth", event.depth}});
        break;
    case ERunEventKind::RunEnded: {
        auto* log = registry.ctx().find<FRunLog>();
        if (log != nullptr) HandleRunEnded(registry, *log, event);
        break;
    }
    case ERunEventKind::RewardPicked: {
        const auto* run = registry.ctx().find<FDungeonRun>();
        const bool skipped = event.id.empty();
        bool duplicate = false;
        if (!skipped && run != nullptr) {
            const auto occurrences = std::ranges::count(run->runes, event.id);
            duplicate = event.amount > 0 || occurrences > 1;
        }
        LogRunEvent(registry, nlohmann::json{{"t", "offer"},
                                             {"depth", event.depth},
                                             {"room", event.room},
                                             {"picked", event.id},
                                             {"skipped", skipped},
                                             {"duplicate", duplicate},
                                             {"refund", event.amount}});
        break;
    }
    case ERunEventKind::EventChosen:
        LogRunEvent(registry, nlohmann::json{{"t", "event"},
                                             {"depth", event.depth},
                                             {"room", event.room},
                                             {"event", event.id},
                                             {"choice", event.amount}});
        break;
    case ERunEventKind::CampSpent:
        LogRunEvent(registry, nlohmann::json{{"t", "camp"},
                                             {"depth", event.depth},
                                             {"id", event.id},
                                             {"amount", event.amount},
                                             {"member", event.member}});
        break;
    case ERunEventKind::BossDefeated:
        break;  // no v1 line: the boss room is a room_end like any other
    }
}

// ---------------------------------------------------------------------------------------------
// [WP-19b] Debrief scenes: the three room rules, facts only, read straight from the log lines
// ---------------------------------------------------------------------------------------------

constexpr std::string_view kSceneTitleLargestLoss = "Heaviest losses";
constexpr std::string_view kSceneTitleFirstKo = "First knockout";
constexpr std::string_view kSceneTitleFatalIntent = "The final blow";

constexpr std::string_view kSceneFactRoom = "Room";
constexpr std::string_view kSceneFactResult = "Result";
constexpr std::string_view kSceneFactRounds = "Rounds";
constexpr std::string_view kSceneFactHpLost = "HP lost";
constexpr std::string_view kSceneFactKnockout = "Knockout";
constexpr std::string_view kSceneFactSkillsUsed = "Skills used";
constexpr std::string_view kSceneFactEncounter = "Encounter";
constexpr std::string_view kSceneFactFamily = "Family";
constexpr std::string_view kSceneFactElite = "Elite";
constexpr std::string_view kSceneFactOffer = "Offer";
constexpr std::string_view kSceneFactFatalIntent = "Fatal intent";
constexpr std::string_view kSceneFactDeclaredAtCommit = "Declared at commit";
constexpr std::string_view kSceneFactExpectedAtExecution = "Expected at execution";
constexpr std::string_view kSceneFactActualDamage = "Actual damage";
constexpr std::string_view kSceneFactOutcome = "Outcome";
constexpr std::string_view kSceneFactPreviewChange = "Preview change";

constexpr std::string_view kSceneValueYes = "yes";
constexpr std::string_view kSceneValueHit = "hit";
constexpr std::string_view kSceneValueMissed = "missed";
constexpr std::string_view kSceneValueSkipped = "skipped";
constexpr std::string_view kSceneResultDefeat = "defeat";

/// The int-shaped value of a JSON number; nullopt when it is not an integer or does not fit an int. A
/// mistyped (or absurd) value only drops the fact that reads it — it never becomes a default 0.
std::optional<int> IntValue(const nlohmann::json& value) {
    if (!value.is_number_integer()) return std::nullopt;
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return std::nullopt;
        return static_cast<int>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) return std::nullopt;
    return static_cast<int>(number);
}

std::optional<int> IntMember(const nlohmann::json& object, const char* key) {
    const auto member = object.find(key);
    if (member == object.end()) return std::nullopt;
    return IntValue(*member);
}

std::optional<std::string> StringMember(const nlohmann::json& object, const char* key) {
    const auto member = object.find(key);
    if (member == object.end() || !member->is_string()) return std::nullopt;
    return member->get<std::string>();
}

std::optional<bool> BoolMember(const nlohmann::json& object, const char* key) {
    const auto member = object.find(key);
    if (member == object.end() || !member->is_boolean()) return std::nullopt;
    return member->get<bool>();
}

bool HasStringMember(const nlohmann::json& object, const char* key, std::string_view expected) {
    const auto value = StringMember(object, key);
    return value.has_value() && std::string_view(*value) == expected;
}

/// The sum of one room_end's hp_loss entries (integer `loss` values only); 0 without the array.
int RoomHpLossSum(const nlohmann::json& line) {
    const auto hpLoss = line.find("hp_loss");
    if (hpLoss == line.end() || !hpLoss->is_array()) return 0;
    int total = 0;
    for (const auto& entry : *hpLoss) {
        if (!entry.is_object()) continue;
        if (const auto loss = IntMember(entry, "loss")) total += *loss;
    }
    return total;
}

/// One room_end line with an integer depth and room: the only lines the scene rules consider.
struct FSceneRoomEnd {
    std::size_t position{0};  ///< index in log.events — the scene's chronological position
    int depth{0};
    int room{0};
    const nlohmann::json* line{nullptr};
    int hpLossSum{0};
    bool knockout{false};
    bool defeat{false};
};

/// A scene under construction: the facts so far plus the room_end position that orders it.
struct FSceneDraft {
    FRunScene scene;
    std::size_t position{0};
};

std::string_view SceneTitle(ERunSceneRule rule) {
    switch (rule) {
    case ERunSceneRule::LargestLoss:
        return kSceneTitleLargestLoss;
    case ERunSceneRule::FirstKo:
        return kSceneTitleFirstKo;
    case ERunSceneRule::FatalIntent:
        return kSceneTitleFatalIntent;
    }
    return {};
}

/// Appends a fact unless the same (label, value) pair is already in the scene; insertion order kept.
void AppendFact(FRunScene& scene, std::string_view label, std::string_view value) {
    const bool exists = std::ranges::any_of(scene.facts, [label, value](const FRunSceneFact& fact) {
        return std::string_view(fact.label) == label && std::string_view(fact.value) == value;
    });
    if (!exists) scene.facts.push_back(FRunSceneFact{std::string(label), std::string(value)});
}

/// The rules stay ascending with no duplicates (they are added in rule order, but the contract is kept
/// locally so a later caller cannot break it).
void AddSceneRule(FRunScene& scene, ERunSceneRule rule) {
    if (std::ranges::find(scene.rules, rule) != scene.rules.end()) return;
    scene.rules.push_back(rule);
    std::ranges::sort(scene.rules);
}

/// The scene of (depth, room), created on first use; merging keeps the earliest room_end position.
FSceneDraft& SceneForRoom(std::vector<FSceneDraft>& scenes, const FSceneRoomEnd& record) {
    for (FSceneDraft& draft : scenes) {
        if (draft.scene.depth != record.depth || draft.scene.room != record.room) continue;
        draft.position = std::min(draft.position, record.position);
        return draft;
    }
    FSceneDraft draft;
    draft.scene.depth = record.depth;
    draft.scene.room = record.room;
    draft.position = record.position;
    scenes.push_back(std::move(draft));
    return scenes.back();
}

/// The room_end facts (rules 1 and 2, and rule 3's room too), then the room's first offer fact.
void AppendRoomFacts(FRunScene& scene, const FSceneRoomEnd& record,
                     const std::map<std::pair<int, int>, const nlohmann::json*>& firstOffers) {
    const nlohmann::json& line = *record.line;
    AppendFact(scene, kSceneFactRoom, std::format("Depth {} room {}", record.depth, record.room));
    if (const auto result = StringMember(line, "result")) AppendFact(scene, kSceneFactResult, *result);
    if (const auto rounds = IntMember(line, "rounds")) AppendFact(scene, kSceneFactRounds, std::to_string(*rounds));
    const auto hpLoss = line.find("hp_loss");
    if (hpLoss != line.end() && hpLoss->is_array()) {
        AppendFact(scene, kSceneFactHpLost, std::to_string(record.hpLossSum));
        for (const auto& entry : *hpLoss) {
            if (!entry.is_object()) continue;
            const auto loss = IntMember(entry, "loss");
            if (!loss || *loss <= 0) continue;
            const auto id = StringMember(entry, "id");
            if (!id) continue;
            AppendFact(scene, std::format("{} ({})", kSceneFactHpLost, *id), std::to_string(*loss));
        }
    }
    if (const auto ko = BoolMember(line, "ko"); ko && *ko) AppendFact(scene, kSceneFactKnockout, kSceneValueYes);
    const auto skills = line.find("skills_used");
    if (skills != line.end() && skills->is_object() && !skills->empty()) {
        // A JSON object iterates in key order (std::map), which is the contract's order.
        std::string text;
        for (auto entry = skills->begin(); entry != skills->end(); ++entry) {
            const auto count = IntValue(entry.value());
            if (!count) continue;
            if (!text.empty()) text += ", ";
            text += std::format("{} x{}", entry.key(), *count);
        }
        if (!text.empty()) AppendFact(scene, kSceneFactSkillsUsed, text);
    }
    if (const auto encounter = StringMember(line, "encounter")) AppendFact(scene, kSceneFactEncounter, *encounter);
    if (const auto family = StringMember(line, "family")) AppendFact(scene, kSceneFactFamily, *family);
    if (const auto elite = BoolMember(line, "elite"); elite && *elite) {
        AppendFact(scene, kSceneFactElite, kSceneValueYes);
    }
    const auto offer = firstOffers.find({record.depth, record.room});
    if (offer == firstOffers.end()) return;
    if (const auto skipped = BoolMember(*offer->second, "skipped"); skipped && *skipped) {
        AppendFact(scene, kSceneFactOffer, kSceneValueSkipped);
    } else if (const auto picked = StringMember(*offer->second, "picked")) {
        AppendFact(scene, kSceneFactOffer, *picked);
    }
}

/// Rule 3's facts: the one fatal enemy intent, exactly as the log settled it (no displayed/marked claim).
void AppendFatalIntentFacts(FRunScene& scene, const nlohmann::json& fatal) {
    if (const auto skill = StringMember(fatal, "skill")) AppendFact(scene, kSceneFactFatalIntent, *skill);
    const auto declaredAtCommit = IntMember(fatal, "declared_at_commit");
    const auto expectedAtExecution = IntMember(fatal, "expected_at_execution");
    if (declaredAtCommit) AppendFact(scene, kSceneFactDeclaredAtCommit, std::to_string(*declaredAtCommit));
    if (expectedAtExecution) AppendFact(scene, kSceneFactExpectedAtExecution, std::to_string(*expectedAtExecution));
    if (const auto damage = IntMember(fatal, "actual_damage")) {
        AppendFact(scene, kSceneFactActualDamage, std::to_string(*damage));
    }
    if (const auto hit = BoolMember(fatal, "hit")) {
        AppendFact(scene, kSceneFactOutcome, *hit ? kSceneValueHit : kSceneValueMissed);
    }
    // The ONLY comparison: the two previews are both logged integers and differ. No counterfactual.
    if (declaredAtCommit && expectedAtExecution && *declaredAtCommit != *expectedAtExecution) {
        AppendFact(scene, kSceneFactPreviewChange, std::format("{} -> {}", *declaredAtCommit, *expectedAtExecution));
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

std::string RunLogUtcTimestamp() {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

std::uint64_t RunLogFrameIndex(const entt::registry& registry) {
    if (const auto* frame = registry.ctx().find<FEngineFrameState>()) return frame->frameIndex;
    return 0;
}

std::uint64_t RunLogMilliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void InitializeRunLog(entt::registry& registry, std::string directory) {
    auto& config = EnsureConfig(registry);
    if (!directory.empty()) {
        config.directory = std::move(directory);
        config.resolved = false;
        config.pruned = false;
        (void)ResolveDirectory(registry, config);
    }
    if (config.initialized) return;
    config.initialized = true;
    RegisterRunEventListener(registry, [&registry](const FRunEvent& event) { HandleRunEvent(registry, event); });
    RegisterRunLogSection(registry);
}

void BeginRunLog(entt::registry& registry) {
    if (registry.ctx().find<FDungeonRun>() == nullptr) return;
    registry.ctx().erase<FRunLog>();
    if (auto* log = EnsureRunLog(registry)) EmitRunStart(registry, *log, false);
}

void LogDungeonEnter(entt::registry& registry) {
    auto* log = EnsureStartedRunLog(registry);
    if (log == nullptr) return;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr || run->depth == log->lastEnteredDepth) return;
    log->lastEnteredDepth = run->depth;
    LogRunEvent(registry, nlohmann::json{{"t", "dungeon_enter"}, {"depth", run->depth}});
}

void LogRoomStart(entt::registry& registry) {
    if (EnsureStartedRunLog(registry) == nullptr) return;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return;
    LogRunEvent(registry, nlohmann::json{{"t", "room_start"},
                                         {"depth", run->depth},
                                         {"room", run->room},
                                         {"name", run->roomName},
                                         {"frame", RunLogFrameIndex(registry)},
                                         {"ms", RunLogMilliseconds()}});
}

void LogRunEvent(entt::registry& registry, nlohmann::json event) {
    FRunLog* log = EnsureStartedRunLog(registry);
    if (log == nullptr) return;  // no run: harmless no-op (WP-25 calls this directly)
    const auto type = event.is_object() ? event.find("t") : event.end();
    if (type == event.end() || !type->is_string()) {
        LOG_ERROR("Run log event dropped: it has no string 't'.");
        return;
    }
#ifndef NDEBUG
    if (!FRunEventSchemaValid(event)) {
        LOG_ERROR("Run log event does not match the v1 schema: {}", event.dump());
    }
#endif
    AppendEventLine(*log, std::move(event));
}

bool FRunEventSchemaValid(const nlohmann::json& event) {
    if (!event.is_object()) return false;
    const auto type = event.find("t");
    if (type == event.end() || !type->is_string()) return false;
    struct FRule {
        const char* type;
        std::initializer_list<const char*> keys;
    };
    static const FRule kRules[] = {
        {"log_session", {"t", "schema_version", "log_id", "log_started_at"}},
        {"run_start", {"t", "log_id", "seed", "depth", "frame", "ms", "party"}},
        {"dungeon_enter", {"t", "depth"}},
        {"room_start", {"t", "depth", "room", "name", "frame"}},
        {"room_end", {"t", "depth", "room", "result", "rounds", "hp_loss", "skills_used", "intents"}},
        {"offer", {"t", "depth", "room", "picked", "skipped", "duplicate", "refund"}},
        {"event", {"t", "depth", "room", "event", "choice"}},
        // `member` is optional (WP-10 forward compatibility): the required camp keys stay as authored.
        {"camp", {"t", "depth", "id", "amount"}},
        {"run_end", {"t", "result", "depth", "dungeons_cleared", "reason", "frames_to_first_battle",
                     "ms_to_first_battle", "report"}},
        {"dungeon_clear", {"t", "depth"}},
        {"engine_ignition", {"t", "depth", "room", "round", "engine"}},
        {"loadout", {"t", "depth", "room", "phase", "member", "slot", "rune", "action"}},
    };
    const std::string& name = type->get_ref<const std::string&>();
    for (const FRule& rule : kRules) {
        if (name != rule.type) continue;
        for (const char* key : rule.keys) {
            if (!event.contains(key)) return false;
        }
        return true;
    }
    return false;
}

FRunReport FRunReportSummarize(const FRunLog& log) {
    FRunReport report;
    std::int64_t rounds = 0;
    std::int64_t committed = 0;
    std::int64_t whiffed = 0;
    int dungeons = 0;
    int decisions = 0;
    bool haveRunStart = false;
    bool runStartSynthesized = false;
    bool logSessionBeforeFirstBattle = false;
    bool haveFirstBattle = false;
    std::int64_t startFrame = 0;
    std::int64_t startMs = 0;
    std::int64_t battleFrame = 0;
    std::int64_t battleMs = 0;
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        const auto type = event.find("t");
        if (type == event.end() || !type->is_string()) continue;
        const std::string& name = type->get_ref<const std::string&>();
        if (name == "run_start" && !haveRunStart) {
            haveRunStart = true;
            startFrame = event.value("frame", std::int64_t{0});
            startMs = event.value("ms", std::int64_t{0});
            runStartSynthesized = event.value("synthesized", false);
        } else if (name == "log_session" && haveRunStart && !haveFirstBattle) {
            // Another process started writing this run's file between its start and its first battle: the
            // frame counter and the steady clock restarted, so the two ends of the interval are not comparable.
            logSessionBeforeFirstBattle = true;
        } else if (name == "room_start" && haveRunStart && !haveFirstBattle) {
            haveFirstBattle = true;
            battleFrame = event.value("frame", std::int64_t{0});
            battleMs = event.value("ms", startMs);
        } else if (name == "dungeon_enter") {
            ++dungeons;
        } else if (name == "room_end") {
            ++report.rooms;
            rounds += event.value("rounds", 0);
            bool damageFree = true;
            if (const auto hp = event.find("hp_loss"); hp != event.end() && hp->is_array()) {
                for (const auto& entry : *hp) {
                    if (entry.value("loss", 0) != 0) {
                        damageFree = false;
                        break;
                    }
                }
            }
            if (damageFree) ++report.damageFreeRooms;
            if (event.value("ko", false)) ++report.koRooms;
            if (const auto intents = event.find("intents"); intents != event.end() && intents->is_object()) {
                whiffed += intents->value("whiffed", 0);
                committed += intents->value("committed", 0);
            }
        } else if (name == "offer" || name == "event" || name == "camp") {
            ++decisions;
        }
    }
    report.avgRounds = report.rooms > 0 ? static_cast<double>(rounds) / report.rooms : 0.0;
    report.whiffRate = committed > 0 ? static_cast<double>(whiffed) / committed : 0.0;
    report.decisionsPerDungeon = static_cast<double>(decisions) / std::max(1, dungeons);
    if (haveRunStart && haveFirstBattle && !runStartSynthesized && !logSessionBeforeFirstBattle) {
        report.framesToFirstBattle = battleFrame - startFrame;
        report.msToFirstBattle = battleMs - startMs;
    }
    return report;
}

nlohmann::json FRunReportToJson(const FRunReport& report) {
    return {{"rooms", report.rooms},
            {"avg_rounds_per_room", report.avgRounds},
            {"damage_free_rooms", report.damageFreeRooms},
            {"ko_rooms", report.koRooms},
            {"intent_whiff_rate", report.whiffRate},
            {"decisions_per_dungeon", report.decisionsPerDungeon}};
}

std::vector<FRunScene> BuildRunScenes(const FRunLog& log) {
    // One defensive scan: the eligible room_end lines (integer depth and room), the first offer per room,
    // and the LAST run_end that carries a fatal_intent object. A missing or mistyped key drops only the
    // fact (or line) that reads it.
    std::vector<FSceneRoomEnd> rooms;
    std::map<std::pair<int, int>, const nlohmann::json*> firstOffers;
    const nlohmann::json* fatalIntent = nullptr;
    for (std::size_t index = 0; index < log.events.size(); ++index) {
        const nlohmann::json& event = log.events[index];
        if (!event.is_object()) continue;
        const auto type = event.find("t");
        if (type == event.end() || !type->is_string()) continue;
        const std::string& name = type->get_ref<const std::string&>();
        if (name == "room_end") {
            const auto depth = IntMember(event, "depth");
            const auto room = IntMember(event, "room");
            if (!depth || !room) continue;
            FSceneRoomEnd record;
            record.position = index;
            record.depth = *depth;
            record.room = *room;
            record.line = &event;
            record.hpLossSum = RoomHpLossSum(event);
            record.knockout = BoolMember(event, "ko").value_or(false);
            record.defeat = HasStringMember(event, "result", kSceneResultDefeat);
            rooms.push_back(record);
        } else if (name == "run_end") {
            if (const auto fatal = event.find("fatal_intent"); fatal != event.end() && fatal->is_object()) {
                fatalIntent = &*fatal;
            }
        } else if (name == "offer") {
            const auto depth = IntMember(event, "depth");
            const auto room = IntMember(event, "room");
            if (depth && room) firstOffers.try_emplace({*depth, *room}, &event);
        }
    }

    // Rule 1: the largest HP-loss sum (strict > keeps the earliest room on a tie); a run without any loss
    // has no rule-1 scene. Rule 2: the first room_end with ko == true. Rule 3: the last run_end carrying a
    // fatal_intent object, placed at the last defeat room.
    const FSceneRoomEnd* largestLoss = nullptr;
    int largestLossTotal = 0;
    for (const FSceneRoomEnd& record : rooms) {
        if (record.hpLossSum > largestLossTotal) {
            largestLossTotal = record.hpLossSum;
            largestLoss = &record;
        }
    }
    const FSceneRoomEnd* firstKnockout = nullptr;
    for (const FSceneRoomEnd& record : rooms) {
        if (!record.knockout) continue;
        firstKnockout = &record;
        break;
    }
    const FSceneRoomEnd* lastDefeat = nullptr;
    for (const FSceneRoomEnd& record : rooms) {
        if (record.defeat) lastDefeat = &record;
    }

    std::vector<FSceneDraft> scenes;
    scenes.reserve(kRunSceneLimit);
    if (largestLoss != nullptr) {
        FSceneDraft& draft = SceneForRoom(scenes, *largestLoss);
        AddSceneRule(draft.scene, ERunSceneRule::LargestLoss);
        AppendRoomFacts(draft.scene, *largestLoss, firstOffers);
    }
    if (firstKnockout != nullptr) {
        FSceneDraft& draft = SceneForRoom(scenes, *firstKnockout);
        AddSceneRule(draft.scene, ERunSceneRule::FirstKo);
        AppendRoomFacts(draft.scene, *firstKnockout, firstOffers);
    }
    if (fatalIntent != nullptr && lastDefeat != nullptr) {
        FSceneDraft& draft = SceneForRoom(scenes, *lastDefeat);
        AddSceneRule(draft.scene, ERunSceneRule::FatalIntent);
        AppendRoomFacts(draft.scene, *lastDefeat, firstOffers);
        AppendFatalIntentFacts(draft.scene, *fatalIntent);
    }

    // Titles join the merged rules in ascending rule order; the returned order is the room_end's log order.
    for (FSceneDraft& draft : scenes) {
        std::string title;
        for (ERunSceneRule rule : draft.scene.rules) {
            if (!title.empty()) title += " / ";
            title.append(SceneTitle(rule));
        }
        draft.scene.title = std::move(title);
    }
    std::ranges::sort(scenes, [](const FSceneDraft& left, const FSceneDraft& right) {
        return left.position < right.position;
    });

    std::vector<FRunScene> result;
    result.reserve(std::min(scenes.size(), kRunSceneLimit));
    for (FSceneDraft& draft : scenes) {
        if (result.size() >= kRunSceneLimit) break;
        result.push_back(std::move(draft.scene));
    }
    return result;
}

void AttachRunLogToFile(entt::registry& registry, FRunLog log) {
    auto& config = EnsureConfig(registry);
    const std::string directory = ResolveDirectory(registry, config);
    if (!directory.empty() && !log.logId.empty()) {
        log.path = (std::filesystem::path(directory) / (log.logId + ".jsonl")).string();
    }
    log.fileEnabled = true;
    log.headerWritten = false;
    log.fileWarnings = 0;
    log.events.clear();
    log.runStartWritten = false;
    log.lastEnteredDepth = -1;
    if (!log.path.empty()) {
        std::ifstream file(log.path);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            try {
                log.events.push_back(nlohmann::json::parse(line));
            } catch (const nlohmann::json::exception&) {
                // A torn line from a crash is skipped; the rest of the run's history still loads.
            }
        }
        for (const auto& event : log.events) {
            if (!event.is_object()) continue;
            const auto type = event.find("t");
            if (type == event.end() || !type->is_string()) continue;
            const std::string& name = type->get_ref<const std::string&>();
            if (name == "log_session") {
                if (log.runStartedAt.empty()) {
                    if (const auto at = event.find("log_started_at"); at != event.end() && at->is_string()) {
                        log.runStartedAt = at->get<std::string>();
                    }
                }
            } else if (name == "run_start") {
                log.runStartWritten = true;
                if (const auto seed = event.find("seed"); seed != event.end() && seed->is_number_unsigned()) {
                    log.seed = seed->get<std::uint32_t>();
                }
                if (const auto frame = event.find("frame"); frame != event.end() && frame->is_number_unsigned()) {
                    log.runStartFrame = frame->get<std::uint64_t>();
                }
                if (const auto ms = event.find("ms"); ms != event.end() && ms->is_number_unsigned()) {
                    log.runStartMs = ms->get<std::uint64_t>();
                }
            } else if (name == "dungeon_enter") {
                if (const auto depth = event.find("depth"); depth != event.end() && depth->is_number_integer()) {
                    log.lastEnteredDepth = depth->get<int>();
                }
            }
        }
    }
    registry.ctx().erase<FRunLog>();
    registry.ctx().emplace<FRunLog>(std::move(log));
}

} // namespace game
