#include "gameplay/dungeon/FDungeonSave.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <thread>

#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FRunSequence.h"

namespace game {
namespace {

// Generous ceilings: they exist to reject garbage, not to enforce balance.
constexpr std::int64_t kMaxCount = 100000;
constexpr std::size_t kMaxListEntries = 256;
constexpr std::size_t kMaxTextLength = 512;

using FNumber = std::expected<std::int64_t, std::string>;
using FText = std::expected<std::string, std::string>;
using FTextList = std::expected<std::vector<std::string>, std::string>;

// Windows refuses to replace or delete a file while another process holds a handle on it
// (MoveFileExW(REPLACE_EXISTING) -> ERROR_ACCESS_DENIED even when that handle shares delete;
// DeleteFileW -> ERROR_SHARING_VIOLATION when it does not). On-access antivirus scans and readers open a
// freshly written file for a few milliseconds, so a single attempt fails intermittently. Retry those codes
// with a short backoff: 1+2+4+...+64 ms, then 100 ms steps, ~0.6 s in total and only on the failure path.
constexpr int kTransientFsAttempts = 12;

bool IsTransientFsError(const std::error_code& code) {
    if (code == std::errc::permission_denied || code == std::errc::device_or_resource_busy) return true;
#ifdef _WIN32
    if (code.category() == std::system_category()) {
        // ERROR_ACCESS_DENIED, ERROR_SHARING_VIOLATION, ERROR_LOCK_VIOLATION
        return code.value() == 5 || code.value() == 32 || code.value() == 33;
    }
#endif
    return false;
}

template <typename FOperation>
void RetryTransientFs(FOperation&& operation, std::error_code& code) {
    for (int attempt = 0;; ++attempt) {
        code.clear();
        operation(code);
        if (!code || attempt + 1 >= kTransientFsAttempts || !IsTransientFsError(code)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(1 << attempt, 100)));
    }
}

std::unexpected<std::string> Fail(std::string_view key, std::string_view problem) {
    return std::unexpected(std::string(key) + ": " + std::string(problem));
}

FNumber ReadNumber(const nlohmann::json& object, const char* key, std::int64_t lowest, std::int64_t highest) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) return Fail(key, "must be an integer");
    const auto value = found->get<std::int64_t>();
    if (value < lowest || value > highest) return Fail(key, "is out of range");
    return value;
}

FText ReadText(const nlohmann::json& object, const char* key, bool allowEmpty = false) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) return Fail(key, "must be text");
    auto text = found->get<std::string>();
    if (text.size() > kMaxTextLength || (!allowEmpty && text.empty())) return Fail(key, "has an unusable length");
    return text;
}

FTextList ReadTextList(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_array() || found->size() > kMaxListEntries) {
        return Fail(key, "must be a short list");
    }
    std::vector<std::string> values;
    for (const auto& entry : *found) {
        if (!entry.is_string() || entry.get_ref<const std::string&>().empty() ||
            entry.get_ref<const std::string&>().size() > kMaxTextLength) {
            return Fail(key, "must list ids");
        }
        values.push_back(entry.get<std::string>());
    }
    return values;
}

std::optional<EDungeonPhase> ParsePhase(std::string_view name) {
    for (const auto phase : {EDungeonPhase::Battle, EDungeonPhase::Reward, EDungeonPhase::Event, EDungeonPhase::Camp}) {
        if (name == DungeonPhaseName(phase)) return phase;
    }
    return std::nullopt;  // a lost run is never saved, so "defeat" is not a phase a save may name
}

std::string UtcTimestamp() {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

/// v1 -> v2: keep every core key, introduce the empty `run.ext` section container. Nothing else — the
/// section mechanism fills ext later, and the core never names a module.
std::optional<nlohmann::json> MigrateV1ToV2(nlohmann::json document) {
    if (!document.is_object()) return std::nullopt;
    if (auto run = document.find("run"); run != document.end() && run->is_object() && run->find("ext") == run->end()) {
        run->emplace("ext", nlohmann::json::object());
    }
    document["schema_version"] = 2;
    return document;
}

/// Exact keeps the historical rule (whole party); Subset accepts any non-empty part of it, or exactly
/// `partySize` members when the rules pin a count (a run party is always kRunPartySize — the four
/// battlefield cells — and Subset only widens WHICH ids are legal, never the size).
bool PartySizeAccepted(std::size_t size, const FDungeonSaveRules& rules) {
    if (rules.partyMatch == EPartyMatch::Exact) return size == rules.partyIds.size();
    if (rules.partySize > 0) return size == rules.partySize;
    return size >= 1 && size <= rules.partyIds.size();
}

nlohmann::json MemberToJson(const FRunMember& member) {
    nlohmann::json skills = nlohmann::json::array();
    for (const auto& slot : member.skills) skills.push_back({{"skill", slot.skillId}, {"runes", slot.runeIds}});
    return {{"id", member.id},
            {"display_name", member.displayName},
            {"health", member.health},
            {"bonus_health", member.bonusHealth},
            {"bonus_attack", member.bonusAttack},
            {"bonus_magic", member.bonusMagic},
            {"bonus_defense", member.bonusDefense},
            {"bonus_magic_defense", member.bonusMagicDefense},
            {"bonus_speed", member.bonusSpeed},
            {"experience", member.experience},
            {"level", member.level},
            {"passive_points", member.passivePoints},
            {"allocated_nodes", member.allocatedNodes},
            {"skills", std::move(skills)}};
}

std::expected<std::vector<FSkillSlot>, std::string> SkillsFromJson(
    const nlohmann::json& member, const FDungeonSaveRules& rules) {
    const auto found = member.find("skills");
    if (found == member.end() || !found->is_array() || found->size() > kMaxListEntries) {
        return Fail("skills", "must be a short list");
    }
    std::vector<FSkillSlot> slots;
    for (const auto& entry : *found) {
        if (!entry.is_object()) return Fail("skills", "must hold skill objects");
        const auto skill = ReadText(entry, "skill");
        const auto runes = ReadTextList(entry, "runes");
        if (!skill) return std::unexpected(skill.error());
        if (!runes) return std::unexpected(runes.error());
        FSkillSlot slot{*skill, *runes};
        if (const auto problem = ValidateSkillSlot(slot, *rules.content)) return Fail("skills", *problem);
        slots.push_back(std::move(slot));
    }
    return slots;
}

std::expected<FRunMember, std::string> MemberFromJson(const nlohmann::json& json, const FDungeonSaveRules& rules) {
    if (!json.is_object()) return Fail("party", "must hold member objects");
    FRunMember member;
    const auto id = ReadText(json, "id");
    const auto name = ReadText(json, "display_name", true);
    if (!id) return std::unexpected(id.error());
    if (!name) return std::unexpected(name.error());
    member.id = *id;
    member.displayName = *name;

    struct FIntField { const char* key; std::int64_t lowest; std::int64_t highest; int* target; };
    for (const FIntField& field : {
             FIntField{"health", -1, kMaxCount, &member.health},
             FIntField{"bonus_health", 0, kMaxCount, &member.bonusHealth},
             FIntField{"bonus_attack", 0, kMaxCount, &member.bonusAttack},
             FIntField{"bonus_magic", 0, kMaxCount, &member.bonusMagic},
             FIntField{"bonus_defense", 0, kMaxCount, &member.bonusDefense},
             FIntField{"bonus_magic_defense", 0, kMaxCount, &member.bonusMagicDefense},
             FIntField{"bonus_speed", 0, kMaxCount, &member.bonusSpeed},
             FIntField{"experience", 0, kMaxCount, &member.experience},
             FIntField{"level", 1, 999, &member.level},
             FIntField{"passive_points", 0, 999, &member.passivePoints}}) {
        const auto value = ReadNumber(json, field.key, field.lowest, field.highest);
        if (!value) return std::unexpected(value.error());
        *field.target = static_cast<int>(*value);
    }

    // Replaying the allocations in order proves every node exists and was reachable when it was taken.
    const auto nodes = ReadTextList(json, "allocated_nodes");
    if (!nodes) return std::unexpected(nodes.error());
    for (const auto& node : *nodes) {
        const bool taken = std::ranges::find(member.allocatedNodes, node) != member.allocatedNodes.end();
        if (taken || rules.passiveTree->Find(node) == nullptr || !rules.passiveTree->CanAllocate(member.allocatedNodes, node)) {
            return Fail("allocated_nodes", "names a node that cannot be allocated");
        }
        member.allocatedNodes.push_back(node);
    }

    auto skills = SkillsFromJson(json, rules);
    if (!skills) return std::unexpected(skills.error());
    member.skills = std::move(*skills);
    return member;
}

} // namespace

const char* DungeonPhaseName(EDungeonPhase phase) noexcept {
    switch (phase) {
    case EDungeonPhase::Battle: return "battle";
    case EDungeonPhase::Reward: return "reward";
    case EDungeonPhase::Event: return "event";
    case EDungeonPhase::Camp: return "camp";
    case EDungeonPhase::Defeat: return "defeat";
    }
    return "battle";
}

nlohmann::json DungeonRunToJson(const FDungeonRun& run) {
    nlohmann::json party = nlohmann::json::array();
    for (const auto& member : run.party) party.push_back(MemberToJson(member));
    return {{"schema_version", kDungeonSaveVersion},
            {"saved_at", UtcTimestamp()},
            {"run", {{"seed", run.seed},
                     {"depth", run.depth},
                     {"room", run.room},
                     {"cleared", run.cleared},
                     {"materials", run.materials},
                     {"phase", DungeonPhaseName(run.phase)},
                     {"room_name", run.roomName},
                     {"message", run.message},
                     {"runes", run.runes},
                     {"party", std::move(party)},
                     // The section container starts empty; PackSections fills it at the save call site.
                     {"ext", nlohmann::json::object()}}}};
}

std::expected<FDungeonRun, std::string> DungeonRunFromJson(
    const nlohmann::json& document, const FDungeonSaveRules& rules) {
    if (rules.content == nullptr || rules.passiveTree == nullptr) return Fail("rules", "are unavailable");
    if (!document.is_object()) return Fail("save", "must be an object");
    const auto version = ReadNumber(document, "schema_version", 1, 1'000'000);
    if (!version) return std::unexpected(version.error());
    if (*version > kDungeonSaveVersion) return Fail("schema_version", "is not supported by this build");

    // Older saves are upgraded up front (never on disk — the file is rewritten by the next save); the rest
    // of this function only ever validates a current document.
    nlohmann::json migrated;
    const nlohmann::json* source = &document;
    if (*version < kDungeonSaveVersion) {
        auto upgraded = MigrateSaveDocument(document, static_cast<int>(*version), kDungeonSaveVersion);
        if (!upgraded) return Fail("schema_version", "cannot be migrated by this build");
        migrated = std::move(*upgraded);
        source = &migrated;
    }
    const auto found = source->find("run");
    if (found == source->end() || !found->is_object()) return Fail("run", "must be an object");
    const nlohmann::json& data = *found;
    if (const auto ext = data.find("ext"); ext != data.end() && !ext->is_object()) {
        return Fail("run.ext", "must be an object");
    }

    FDungeonRun run;
    const auto seed = ReadNumber(data, "seed", 1, std::numeric_limits<std::uint32_t>::max());
    const auto depth = ReadNumber(data, "depth", 1, 1000);
    // WP-08: `room` is the battle ordinal of the dungeon at `depth - 1`; the slot cap is the static upper
    // bound (the sequence-specific bound is dynamic and lives in ParseSavedRun, which owns the registry).
    const auto room = ReadNumber(data, "room", 0, kMaxDungeonSlots - 1);
    const auto cleared = ReadNumber(data, "cleared", 0, 999);
    const auto materials = ReadNumber(data, "materials", 0, kMaxCount);
    const auto roomName = ReadText(data, "room_name", true);
    const auto message = ReadText(data, "message", true);
    for (const auto* problem : {&seed, &depth, &room, &cleared, &materials}) {
        if (!*problem) return std::unexpected(problem->error());
    }
    if (!roomName) return std::unexpected(roomName.error());
    if (!message) return std::unexpected(message.error());
    run.seed = static_cast<std::uint32_t>(*seed);
    run.depth = static_cast<int>(*depth);
    run.room = static_cast<int>(*room);
    run.cleared = static_cast<int>(*cleared);
    run.materials = static_cast<int>(*materials);
    run.roomName = *roomName;
    run.message = *message;

    const auto phaseText = ReadText(data, "phase");
    if (!phaseText) return std::unexpected(phaseText.error());
    const auto phase = ParsePhase(*phaseText);
    if (!phase) return Fail("phase", "is not a resumable phase");
    run.phase = *phase;
    // The checkpoint count trails the depth by one, or equals it while camp is open.
    if (run.cleared != run.depth - 1 && !(run.phase == EDungeonPhase::Camp && run.cleared == run.depth)) {
        return Fail("cleared", "does not match the depth");
    }

    const auto runes = ReadTextList(data, "runes");
    if (!runes) return std::unexpected(runes.error());
    for (const auto& rune : *runes) {
        if (rules.content->runes.find(rune) == nullptr) return Fail("runes", "names an unknown rune");
    }
    run.runes = *runes;

    const auto party = data.find("party");
    if (party == data.end() || !party->is_array() || !PartySizeAccepted(party->size(), rules)) {
        return Fail("party", "does not match the expedition party");
    }
    for (const auto& entry : *party) {
        auto member = MemberFromJson(entry, rules);
        if (!member) return std::unexpected(member.error());
        const bool known = std::ranges::find(rules.partyIds, member->id) != rules.partyIds.end();
        const bool repeated = std::ranges::find(run.party, member->id, &FRunMember::id) != run.party.end();
        if (!known || repeated) return Fail("party", "does not match the expedition party");
        run.party.push_back(std::move(*member));
    }
    return run;
}

std::optional<nlohmann::json> MigrateSaveDocumentWith(const nlohmann::json& document, int fromVersion,
                                                      int toVersion, std::span<const FSaveMigrationStep> steps) {
    if (fromVersion < 1 || toVersion < fromVersion) return std::nullopt;
    nlohmann::json migrated = document;
    for (int version = fromVersion; version < toVersion; ++version) {
        const auto index = static_cast<std::size_t>(version - 1);  // steps[i] upgrades i+1 -> i+2
        if (index >= steps.size() || !steps[index]) return std::nullopt;
        auto next = steps[index](std::move(migrated));
        if (!next) return std::nullopt;
        migrated = std::move(*next);
    }
    return migrated;
}

std::optional<nlohmann::json> MigrateSaveDocument(const nlohmann::json& document, int fromVersion, int toVersion) {
    static const std::array<FSaveMigrationStep, 1> steps{MigrateV1ToV2};
    return MigrateSaveDocumentWith(document, fromVersion, toVersion, steps);
}

bool WriteSaveFile(const std::filesystem::path& path, const nlohmann::json& document, std::string& error) {
    std::error_code code;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), code);
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << document.dump(2);
        file.flush();
        if (!file) {
            error = "could not write " + temp.string();
            file.close();
            std::filesystem::remove(temp, code);
            return false;
        }
    }
    RetryTransientFs([&](std::error_code& attempt) { std::filesystem::rename(temp, path, attempt); }, code);
    if (code) {
        error = "could not replace " + path.string() + ": " + code.message();
        std::filesystem::remove(temp, code);
        return false;
    }
    return true;
}

void RemoveSaveFile(const std::filesystem::path& path, std::error_code& code) {
    RetryTransientFs([&](std::error_code& attempt) { std::filesystem::remove(path, attempt); }, code);
}

std::expected<nlohmann::json, std::string> ReadSaveFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected("save file is missing or unreadable: " + path.string());
    auto document = nlohmann::json::parse(file, nullptr, false);
    if (document.is_discarded()) return std::unexpected("save file is not valid JSON: " + path.string());
    return document;
}

} // namespace game
