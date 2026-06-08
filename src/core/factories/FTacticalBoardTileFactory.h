#pragma once

#include <entt/entt.hpp>

namespace game {

struct FTacticalBoardTileFactory {
    static entt::entity create(entt::registry& registry, int tileX, int tileY, int tileFeet, bool isWall, bool isCover);
};

} // namespace game
