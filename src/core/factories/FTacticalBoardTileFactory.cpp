#include "core/factories/FTacticalBoardTileFactory.h"

#include "ecs/components/FGridPosition.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FTacticalBoardTile.h"

namespace game {

entt::entity FTacticalBoardTileFactory::create(entt::registry& registry, int tileX, int tileY, int tileFeet, bool isWall, bool isCover) {
    auto entity = registry.create();
    registry.emplace<FTacticalBoardTile>(entity, tileX, tileY, tileFeet, isWall, isCover);
    registry.emplace<FGridPosition>(entity, tileX, tileY);
    registry.emplace<FTag>(entity, isWall ? "tactical_wall" : "tactical_floor");
    return entity;
}

} // namespace game
