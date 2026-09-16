#pragma once

#include <string>

namespace game::gameplay {

/// The content row this entity was spawned from. why: systems need unit values (AP, later 수치노드 적용) at
/// turn start without a second copy of the data — one row, one lookup path (R12).
struct FUnitRef {
    std::string contentId;
};

} // namespace game::gameplay
