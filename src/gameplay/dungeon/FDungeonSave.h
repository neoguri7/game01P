#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "gameplay/dungeon/FDungeonRun.h"

namespace game {

namespace gameplay { struct FContentRegistry; }

inline constexpr int kDungeonSaveVersion = 1;

/// What a saved run has to agree with before the game trusts it: the shipped party, the content ids and the
/// passive tree. A save that names anything else is rejected whole rather than patched up.
struct FDungeonSaveRules {
    std::vector<std::string> partyIds;
    const gameplay::FContentRegistry* content{nullptr};
    const FPassiveTree* passiveTree{nullptr};
};

[[nodiscard]] const char* DungeonPhaseName(EDungeonPhase phase) noexcept;

[[nodiscard]] nlohmann::json DungeonRunToJson(const FDungeonRun& run);

/// Rebuilds a run from a save document. Transient request flags always come back cleared.
[[nodiscard]] std::expected<FDungeonRun, std::string> DungeonRunFromJson(
    const nlohmann::json& document, const FDungeonSaveRules& rules);

/// Writes to a sibling temp file and renames it over `path`, so a crash mid-write cannot leave half a save.
[[nodiscard]] bool WriteSaveFile(const std::filesystem::path& path, const nlohmann::json& document, std::string& error);

/// Reads and parses a save file; a missing or unparsable file comes back as an error message.
[[nodiscard]] std::expected<nlohmann::json, std::string> ReadSaveFile(const std::filesystem::path& path);

} // namespace game
