#include "gameplay/data/FRuneContent.h"

#include <algorithm>

namespace game::gameplay {

std::optional<ERuneRarity> ParseRuneRarity(std::string_view name) {
    if (name == "common") return ERuneRarity::Common;
    if (name == "uncommon") return ERuneRarity::Uncommon;
    if (name == "rare") return ERuneRarity::Rare;
    return std::nullopt;
}

std::string_view RuneRarityName(ERuneRarity rarity) {
    switch (rarity) {
    case ERuneRarity::Common: return "common";
    case ERuneRarity::Uncommon: return "uncommon";
    case ERuneRarity::Rare: return "rare";
    }
    return "common";
}

FRuneContent decodeRune(const FContentRow& row) {
    FRuneContent rune;
    rune.id = row.text(kRuneFieldId);
    rune.displayName = row.text(kRuneFieldDisplayName);
    rune.statusOnHit = row.text(kRuneFieldStatusOnHit);
    rune.shapeOverride = row.text(kRuneFieldShapeOverride);
    rune.powerDelta = row.integer(kRuneFieldPowerDelta, 0);
    rune.apDelta = row.integer(kRuneFieldApDelta, 0);
    rune.rangeDelta = row.integer(kRuneFieldRangeDelta, 0);
    rune.areaRadiusDelta = row.integer(kRuneFieldAreaRadiusDelta, 0);
    rune.requiresAllTags = row.textArray(kRuneFieldRequiresAllTags);
    rune.requiresAnyTags = row.textArray(kRuneFieldRequiresAnyTags);
    rune.excludesTags = row.textArray(kRuneFieldExcludesTags);
    rune.addsTags = row.textArray(kRuneFieldAddsTags);
    rune.statusId = row.text(kRuneFieldStatusId);
    rune.statusDurationDelta = row.integer(kRuneFieldStatusDurationDelta, 0);
    rune.statusMagnitudeDelta = row.integer(kRuneFieldStatusMagnitudeDelta, 0);
    rune.lifeOnHit = row.integer(kRuneFieldLifeOnHit, 0);

    // WP-04: typed hooks (optional). The decoder cannot fail; an unknown event name is kept for boot
    // validation, and the source/id provenance is stamped here.
    rune.hooks = DecodeHooks(row.objectArray(kRuneFieldHooks), "rune:" + rune.id);
    // WP-17: a weight below 1 would make a rune undrawable; the floor keeps authored data honest.
    rune.dropWeight = std::max(1, row.integer(kRuneFieldDropWeight, 1));
    // WP-40: an absent rarity is common; an unknown one is kept for the boot gate in FContentRegistry.
    rune.rarityName = row.text(kRuneFieldRarity);
    if (rune.rarityName.empty()) rune.rarityName = "common";
    rune.rarity = ParseRuneRarity(rune.rarityName).value_or(ERuneRarity::Common);
    rune.kind = row.text(kRuneFieldKind);
    return rune;
}

} // namespace game::gameplay
