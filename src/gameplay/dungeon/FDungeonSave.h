#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "gameplay/dungeon/FDungeonRun.h"

namespace game {

namespace gameplay { struct FContentRegistry; }

/// Current run save schema. v2 adds the `run.ext` section container (module state lives there — the core
/// never names a module) and the advisory `content_schema` summary.
inline constexpr int kDungeonSaveVersion = 2;

/// How strictly a saved party must match the expedition party.
/// Exact: the save names the whole party — same size, same ids (the historical rule).
/// Subset: any non-empty part of it, any order; every member is still validated individually (skills,
/// runes, nodes). A roster work package flips this at the rules construction site.
enum class EPartyMatch { Exact, Subset };

/// What a saved run has to agree with before the game trusts it: the shipped party, the content ids and the
/// passive tree. A save that names anything else is rejected whole rather than patched up.
struct FDungeonSaveRules {
    std::vector<std::string> partyIds;
    EPartyMatch partyMatch{EPartyMatch::Exact};
    const gameplay::FContentRegistry* content{nullptr};
    const FPassiveTree* passiveTree{nullptr};
};

[[nodiscard]] const char* DungeonPhaseName(EDungeonPhase phase) noexcept;

[[nodiscard]] nlohmann::json DungeonRunToJson(const FDungeonRun& run);

/// Rebuilds a run from a save document. Transient request flags always come back cleared.
/// Older schema versions are migrated up first (see MigrateSaveDocument); a newer one is refused.
[[nodiscard]] std::expected<FDungeonRun, std::string> DungeonRunFromJson(
    const nlohmann::json& document, const FDungeonSaveRules& rules);

/// One migration step: it receives a document of version N and returns the same document upgraded to
/// version N+1 (the step sets "schema_version" itself), or nullopt when it cannot upgrade the document.
using FSaveMigrationStep = std::function<std::optional<nlohmann::json>(nlohmann::json)>;

/// Composes migration steps: steps[i] upgrades version i+1 to i+2. from == to copies the document;
/// a future `from` (above `to`), a version below 1, or a missing step come back as nullopt.
[[nodiscard]] std::optional<nlohmann::json> MigrateSaveDocumentWith(
    const nlohmann::json& document, int fromVersion, int toVersion, std::span<const FSaveMigrationStep> steps);

/// Upgrades a save document to `toVersion` with this build's built-in steps (v1 -> v2 today).
[[nodiscard]] std::optional<nlohmann::json> MigrateSaveDocument(
    const nlohmann::json& document, int fromVersion, int toVersion);

/// Writes to a sibling temp file and renames it over `path`: in normal operation the target is never a
/// half-written file. It is not crash-durable (no fsync), so a crash around the rename can still lose it.
[[nodiscard]] bool WriteSaveFile(const std::filesystem::path& path, const nlohmann::json& document, std::string& error);

/// Reads and parses a save file; a missing or unparsable file comes back as an error message.
[[nodiscard]] std::expected<nlohmann::json, std::string> ReadSaveFile(const std::filesystem::path& path);

} // namespace game
