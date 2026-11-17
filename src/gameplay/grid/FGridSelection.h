#pragma once

#include <entt/entt.hpp>
#include <optional>
#include <vector>

#include "ecs/components/FGridPosition.h"

namespace game {

struct FGridSelection {
    entt::entity selected{entt::null};
    std::vector<FGridPosition> reachable;
    std::vector<FGridPosition> targetable;
    std::optional<FGridPosition> hovered;
    std::vector<FGridPosition> preview;
    std::vector<FGridPosition> affected;
};

} // namespace game
