#pragma once

#include <entt/entt.hpp>
#include <vector>

namespace game {

struct FTacticalTurnOrder {
    std::vector<entt::entity> units;
    int currentIndex{-1};
    int round{0};
};

} // namespace game