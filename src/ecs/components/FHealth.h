#pragma once

namespace game {

/// Current and maximum health. max is copied from unit content at spawn and raised by equipment;
/// current never exceeds max. Death is a separate tag (FDead), not "current == 0" (R: one representation).
struct FHealth {
    int current{0};
    int max{0};
};

} // namespace game
