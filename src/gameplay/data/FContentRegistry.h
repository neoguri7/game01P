#pragma once

#include "core/AssetManager.h"
#include "core/data/FContentTable.h"
#include "gameplay/data/FEncounterContent.h"
#include "gameplay/data/FSkillContent.h"
#include "gameplay/data/FUnitContent.h"

#include <optional>

namespace game::gameplay {

/// Every gameplay content table, decoded once at boot (R23). Systems take this, not `FAssetManager`:
/// nothing outside the data layer needs to know that content happens to be JSON today.
struct FContentRegistry {
    FContentTable<FUnitContent> units;
    FContentTable<FSkillContent> skills;
    FContentTable<FEncounterContent> encounters;

    /// Loads and validates every gameplay asset. Nullopt means "content is unusable" and the reason is
    /// already logged — a broken asset must stop the boot, not surface as a missing skill mid-fight
    /// (R19/R22).
    [[nodiscard]] static std::optional<FContentRegistry> load(const FAssetManager& assets);
};

} // namespace game::gameplay
