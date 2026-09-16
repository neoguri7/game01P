#pragma once

#include "core/AssetManager.h"
#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <optional>
#include <vector>

namespace game {

/// The JSON boundary (R22/R23/R25): reads one asset through FAssetManager, checks `schema_version`,
/// validates every row against the declared schema — required keys, declared types, and unknown keys —
/// and returns rows projected into field order. Every failure is logged with the asset id (R19/R31),
/// so a bad data file fails at load instead of surfacing as a silent default deep in gameplay code.
struct FContentLoader {
    /// Nullopt means "the asset is unusable"; the reason is already in the log.
    [[nodiscard]] static std::optional<std::vector<FContentRow>> load(const FAssetManager& assets, const FContentSchema& schema);
};

} // namespace game
