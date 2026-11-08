#include "gameplay/meta/FMetaSave.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <limits>

namespace game {
namespace {

bool AllDigits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

/// `(0|[1-9][0-9]*)` — the WP-20 granted-key dungeon index.
bool IsDecimalIndex(std::string_view text) {
    if (!AllDigits(text)) return false;
    return text.size() == 1 || text.front() != '0';
}

/// Reads any JSON integer into int64 without throwing on an unsigned value above int64 (get<std::int64_t>
/// would). False for non-integers and out-of-range values.
bool IntegerValue(const nlohmann::json& value, std::int64_t& out) {
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
        out = static_cast<std::int64_t>(raw);
        return true;
    }
    if (value.is_number_integer()) {
        out = value.get<std::int64_t>();
        return true;
    }
    return false;
}

bool IsNonNegativeInteger(const nlohmann::json& value, int& out) {
    std::int64_t raw = 0;
    if (!IntegerValue(value, raw) || raw < 0 || raw > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(raw);
    return true;
}

std::expected<void, std::string> ReadCount(
    const nlohmann::json& object, const char* key, int& out, bool required) {
    const auto found = object.find(key);
    if (found == object.end()) {
        if (!required) return {};
        return std::unexpected(std::string(key) + " is required");
    }
    if (!IsNonNegativeInteger(*found, out)) {
        return std::unexpected(std::string(key) + " must be a non-negative integer");
    }
    return {};
}

std::expected<FMetaRosterRow, std::string> RosterRowFromJson(const nlohmann::json& value) {
    if (!value.is_object()) return std::unexpected("roster rows must be objects");
    FMetaRosterRow row;
    const auto id = value.find("id");
    if (id == value.end() || !id->is_string() || id->get_ref<const std::string&>().empty()) {
        return std::unexpected("a roster row is missing its id");
    }
    row.id = id->get<std::string>();
    if (const auto name = value.find("name"); name != value.end()) {
        if (!name->is_string()) return std::unexpected("roster row '" + row.id + "' name must be a string");
        row.name = name->get<std::string>();
    }
    if (const auto trait = value.find("trait"); trait != value.end()) {
        if (!trait->is_string()) return std::unexpected("roster row '" + row.id + "' trait must be a string");
        row.trait = trait->get<std::string>();
    }
    if (const auto medals = ReadCount(value, "medals", row.medals, false); !medals) {
        return std::unexpected("roster row '" + row.id + "' " + medals.error());
    }
    if (const auto enabled = value.find("enabled"); enabled != value.end()) {
        if (!enabled->is_boolean()) return std::unexpected("roster row '" + row.id + "' enabled must be a boolean");
        row.enabled = enabled->get<bool>();
    }
    if (const auto locked = value.find("locked"); locked != value.end()) {
        if (!locked->is_boolean()) return std::unexpected("roster row '" + row.id + "' locked must be a boolean");
        row.locked = locked->get<bool>();
    }
    // Identity rows are either enabled or locked — never both, never neither (the catalog's shape).
    if (row.enabled == row.locked) {
        return std::unexpected("roster row '" + row.id + "' must be exactly one of enabled or locked");
    }
    return row;
}

std::expected<FMetaLadderRank, std::string> LadderRankFromJson(const nlohmann::json& value) {
    if (!value.is_object()) return std::unexpected("ladder rank entries must be objects");
    FMetaLadderRank rank;
    if (const auto best = ReadCount(value, "best_dungeon", rank.bestDungeon, false); !best) {
        return std::unexpected("ladder " + best.error());
    }
    if (const auto checkpoint = ReadCount(value, "checkpoint", rank.checkpoint, false); !checkpoint) {
        return std::unexpected("ladder " + checkpoint.error());
    }
    if (const auto grand = ReadCount(value, "grand", rank.grand, false); !grand) {
        return std::unexpected("ladder " + grand.error());
    }
    if (const auto granted = value.find("granted"); granted != value.end()) {
        if (!granted->is_array()) return std::unexpected("ladder granted must be an array");
        for (const auto& entry : *granted) {
            if (!entry.is_string() || !IsValidMetaGrantedKey(entry.get_ref<const std::string&>())) {
                return std::unexpected("ladder granted contains an invalid milestone key");
            }
            const std::string key = entry.get<std::string>();
            if (std::ranges::find(rank.granted, key) != rank.granted.end()) {
                return std::unexpected("ladder granted repeats '" + key + "'");
            }
            rank.granted.push_back(key);
        }
    }
    return rank;
}

} // namespace

bool IsValidMetaGrantedKey(std::string_view key) {
    // `^.+/(0|[1-9][0-9]*)#(clear|boss)$` without pulling in <regex>: the last '/' separates the theme
    // id from the 0-based dungeon index, the first '#' after it separates the index from the kind. The
    // theme part may itself contain '/' (the `.+` of the WP regex is greedy by contract).
    const auto slash = key.rfind('/');
    if (slash == std::string_view::npos || slash == 0) return false;
    const auto rest = key.substr(slash + 1);
    const auto hash = rest.find('#');
    if (hash == std::string_view::npos) return false;
    const auto index = rest.substr(0, hash);
    const auto kind = rest.substr(hash + 1);
    if (kind != "clear" && kind != "boss") return false;
    return IsDecimalIndex(index);
}

bool IsValidMetaLadderRankKey(std::string_view key) {
    if (!AllDigits(key)) return false;
    return key.front() != '0';
}

FMetaSave DefaultMetaSave() {
    FMetaSave save;
    save.schemaVersion = kMetaSaveVersion;
    return save;
}

nlohmann::json MetaSaveToJson(const FMetaSave& save) {
    nlohmann::json roster = nlohmann::json::array();
    for (const auto& row : save.roster) {
        roster.push_back({{"id", row.id},
                          {"name", row.name},
                          {"trait", row.trait},
                          {"medals", row.medals},
                          {"enabled", row.enabled},
                          {"locked", row.locked}});
    }
    nlohmann::json ladder = nlohmann::json::object();
    for (const auto& [rank, entry] : save.ladder) {
        ladder[rank] = {{"best_dungeon", entry.bestDungeon},
                        {"checkpoint", entry.checkpoint},
                        {"granted", entry.granted},
                        {"grand", entry.grand}};
    }
    const nlohmann::json stats = {{"runs", save.stats.runs},
                                  {"clears", save.stats.clears},
                                  {"best_depth", save.stats.bestDepth},
                                  {"cycle_best", save.stats.cycleBest},
                                  {"grand", save.stats.grand}};
    return {{"schema_version", save.schemaVersion},
            {"saved_at", save.savedAt},
            {"seals", save.seals},
            {"unlocks", save.unlocks},
            {"roster", std::move(roster)},
            {"ladder", std::move(ladder)},
            {"stats", stats}};
}

std::expected<FMetaSave, std::string> MetaSaveFromJson(const nlohmann::json& document) {
    if (!document.is_object()) return std::unexpected("meta save must be a JSON object");
    const auto version = document.find("schema_version");
    if (version == document.end() || !version->is_number_integer()) {
        return std::unexpected("schema_version is missing or not an integer");
    }
    std::int64_t versionValue = 0;
    if (!IntegerValue(*version, versionValue) || versionValue != kMetaSaveVersion) {
        return std::unexpected("schema_version is not supported by this build");
    }
    FMetaSave save;
    save.schemaVersion = kMetaSaveVersion;
    if (const auto stamp = document.find("saved_at"); stamp != document.end()) {
        if (!stamp->is_string()) return std::unexpected("saved_at must be a string");
        save.savedAt = stamp->get<std::string>();
    }
    if (const auto seals = ReadCount(document, "seals", save.seals, false); !seals) {
        return std::unexpected(seals.error());
    }
    if (const auto unlocks = document.find("unlocks"); unlocks != document.end()) {
        if (!unlocks->is_array()) return std::unexpected("unlocks must be an array");
        for (const auto& entry : *unlocks) {
            if (!entry.is_string() || entry.get_ref<const std::string&>().empty()) {
                return std::unexpected("unlocks entries must be non-empty strings");
            }
            const std::string id = entry.get<std::string>();
            if (std::ranges::find(save.unlocks, id) != save.unlocks.end()) {
                return std::unexpected("unlocks repeats '" + id + "'");
            }
            save.unlocks.push_back(id);
        }
    }
    if (const auto roster = document.find("roster"); roster != document.end()) {
        if (!roster->is_array()) return std::unexpected("roster must be an array");
        for (const auto& entry : *roster) {
            auto row = RosterRowFromJson(entry);
            if (!row) return std::unexpected(row.error());
            if (std::ranges::find(save.roster, row->id, &FMetaRosterRow::id) != save.roster.end()) {
                return std::unexpected("roster repeats '" + row->id + "'");
            }
            save.roster.push_back(std::move(*row));
        }
    }
    if (const auto ladder = document.find("ladder"); ladder != document.end()) {
        if (!ladder->is_object()) return std::unexpected("ladder must be an object");
        for (auto it = ladder->begin(); it != ladder->end(); ++it) {
            if (!IsValidMetaLadderRankKey(it.key())) {
                return std::unexpected("ladder rank key '" + it.key() + "' is not a positive integer");
            }
            auto rank = LadderRankFromJson(it.value());
            if (!rank) return std::unexpected("ladder rank '" + it.key() + "': " + rank.error());
            save.ladder.emplace(it.key(), std::move(*rank));
        }
    }
    if (const auto stats = document.find("stats"); stats != document.end()) {
        if (!stats->is_object()) return std::unexpected("stats must be an object");
        for (const auto& [key, target] :
             {std::pair{"runs", &save.stats.runs}, std::pair{"clears", &save.stats.clears},
              std::pair{"best_depth", &save.stats.bestDepth}, std::pair{"cycle_best", &save.stats.cycleBest},
              std::pair{"grand", &save.stats.grand}}) {
            if (const auto read = ReadCount(*stats, key, *target, false); !read) {
                return std::unexpected("stats " + read.error());
            }
        }
    }
    return save;
}

std::string MetaUtcTimestamp() {
    return std::format(
        "{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

} // namespace game
