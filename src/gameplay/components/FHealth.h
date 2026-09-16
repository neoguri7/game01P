#pragma once

namespace game::gameplay {

/// Current/max health of one unit. why two fields instead of a fraction: damage is applied as a subtraction
/// and the display needs both numbers, so storing the pair avoids a divide per frame.
/// 미결(design §4): 피해 공식 — 경감·저항이 확정되면 그 값은 수치노드/장비가 공급한다.
struct FHealth {
    double current{0.0};
    double max{0.0};
};

} // namespace game::gameplay
