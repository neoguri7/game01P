#pragma once

#include <entt/entt.hpp>
#include <vector>
#include <optional>

#include "ecs/components/FGridPosition.h"

namespace game {

struct FGridSelection {
    entt::entity selected{entt::null};
    std::vector<FGridPosition> reachable;
    std::vector<FGridPosition> affected;
    std::optional<FGridPosition> hovered;
};

} // namespace game
