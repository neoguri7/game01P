#include "gameplay/dungeon/FDungeonSave.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <limits>
#include <string_view>

#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {
namespace {

// Generous ceilings: they exist to reject garbage, not to enforce balance.
constexpr std::int64_t kMaxCount = 100000;
constexpr std::size_t kMaxListEntries = 256;
constexpr std::size_t kMaxTextLength = 512;

using FNumber = std::expected<std::int64_t, std::string>;
using FText = std::expected<std::string, std::string>;
using FTextList = std::expected<std::vector<std::string>, std::string>;

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
                     {"party", std::move(party)}}}};
}

std::expected<FDungeonRun, std::string> DungeonRunFromJson(
    const nlohmann::json& document, const FDungeonSaveRules& rules) {
    if (rules.content == nullptr || rules.passiveTree == nullptr) return Fail("rules", "are unavailable");
    if (!document.is_object()) return Fail("save", "must be an object");
    const auto version = ReadNumber(document, "schema_version", 1, 1'000'000);
    if (!version) return std::unexpected(version.error());
    if (*version != kDungeonSaveVersion) return Fail("schema_version", "is not supported by this build");
    const auto found = document.find("run");
    if (found == document.end() || !found->is_object()) return Fail("run", "must be an object");
    const nlohmann::json& data = *found;

    FDungeonRun run;
    const auto seed = ReadNumber(data, "seed", 1, std::numeric_limits<std::uint32_t>::max());
    const auto depth = ReadNumber(data, "depth", 1, 1000);
    const auto room = ReadNumber(data, "room", 0, 2);
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
    if (party == data.end() || !party->is_array() || party->size() != rules.partyIds.size()) {
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
    std::filesystem::rename(temp, path, code);
    if (code) {
        error = "could not replace " + path.string() + ": " + code.message();
        std::filesystem::remove(temp, code);
        return false;
    }
    return true;
}

std::expected<nlohmann::json, std::string> ReadSaveFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected("save file is missing or unreadable: " + path.string());
    auto document = nlohmann::json::parse(file, nullptr, false);
    if (document.is_discarded()) return std::unexpected("save file is not valid JSON: " + path.string());
    return document;
}

} // namespace game
