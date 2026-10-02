#pragma once

#include <entt/entt.hpp>
#include <vector>

#include "ecs/components/FGridPosition.h"

namespace game {

struct FGridSelection {
    entt::entity selected{entt::null};
    std::vector<FGridPosition> reachable;
};

} // namespace game
