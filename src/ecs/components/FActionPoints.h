#pragma once

namespace game {

/// The two AP pools (design §4: 서로 전환 불가). `move` pays for movement, `skill` pays for skills;
/// both refill to their max at the start of the owner's turn.
struct FActionPoints {
    int move{0};
    int moveMax{0};
    int skill{0};
    int skillMax{0};
};

} // namespace game
