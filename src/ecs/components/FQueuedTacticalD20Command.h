#pragma once

#include <entt/entt.hpp>
#include <string>

namespace game {

struct FQueuedTacticalD20Command {
    std::string actionId{"wait"};
    int movementSpentTiles{0};
    bool hasTargetTile{false};
    int targetTileX{0};
    int targetTileY{0};
    entt::entity targetEntity{entt::null};
    bool validationApproved{false};
};

} // namespace game
