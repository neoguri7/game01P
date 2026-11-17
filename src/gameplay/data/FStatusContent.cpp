#include "gameplay/data/FStatusContent.h"

namespace game::gameplay {

FStatusContent decodeStatus(const FContentRow& row) {
    FStatusContent status;
    status.id = row.text(kStatusFieldId);
    status.displayName = row.text(kStatusFieldDisplayName);
    status.kind = row.text(kStatusFieldKind);
    status.magnitude = row.integer(kStatusFieldMagnitude, 0);
    status.durationTurns = row.integer(kStatusFieldDurationTurns, 1);

    // The loader projects a missing optional Number as 0 (dense rows — FContentLoader::defaultOf), so for
    // fields whose declared default is not 0, "absent" arrives as 0. Apply the declared default here, the
    // numeric twin of the `empty -> default` mapping the Text fields use. Neither field authors 0: a speed
    // percentage of 0 and a 0% chance cap have no designed meaning (a control that can never fire is
    // expressed as magnitude 0).
    status.speedPct = row.integer(kStatusFieldSpeedPct, kStatusSpeedPctNeutral);
    if (status.speedPct == 0) {
        status.speedPct = kStatusSpeedPctNeutral;
    }
    status.mode = row.text(kStatusFieldMode);
    if (status.mode.empty()) {
        status.mode = kStatusModeTurnStart; // pre-D-12 rows and all dot/stat rows keep the turn_start default
    }
    status.chanceCap = row.integer(kStatusFieldChanceCap, kDefaultChanceCap);
    if (status.chanceCap == 0) {
        status.chanceCap = kDefaultChanceCap;
    }
    return status;
}

} // namespace game::gameplay
