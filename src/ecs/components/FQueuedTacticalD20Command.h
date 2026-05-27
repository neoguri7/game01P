#pragma once

#include <string>

namespace game {

struct FQueuedTacticalD20Command {
    std::string actionId{"wait"};
    int movementSpentTiles{0};
};

} // namespace game
