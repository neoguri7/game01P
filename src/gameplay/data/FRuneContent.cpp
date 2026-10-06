#include "gameplay/data/FRuneContent.h"

namespace game::gameplay {

FRuneContent decodeRune(const FContentRow& row) {
    FRuneContent rune;
    rune.id = row.text(kRuneFieldId);
    rune.displayName = row.text(kRuneFieldDisplayName);
    rune.axis = row.text(kRuneFieldAxis);
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
    return rune;
}

} // namespace game::gameplay
