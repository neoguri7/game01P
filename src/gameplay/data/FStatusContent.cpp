#include "gameplay/data/FStatusContent.h"

namespace game::gameplay {

FStatusContent decodeStatus(const FContentRow& row) {
    FStatusContent status;
    status.id = row.text(kStatusFieldId);
    status.displayName = row.text(kStatusFieldDisplayName);
    status.kind = row.text(kStatusFieldKind);
    status.magnitude = row.integer(kStatusFieldMagnitude, 0);
    status.durationTurns = row.integer(kStatusFieldDurationTurns, 1);
    status.speedPct = row.integer(kStatusFieldSpeedPct, 100);
    return status;
}

} // namespace game::gameplay
