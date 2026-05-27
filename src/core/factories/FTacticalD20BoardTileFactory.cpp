#include "core/factories/FTacticalD20BoardTileFactory.h"

#include "core/factories/FTacticalD20BoardPlacement.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FTacticalBoardTile.h"

namespace game {
namespace {

const char* TileTexture(bool isWall, bool isCover) {
    if (isWall) return "assets/tactical_d20/wall_tile.png";
    if (isCover) return "assets/tactical_d20/cover_tile.png";
    return "assets/tactical_d20/floor_tile.png";
}

} // namespace

entt::entity FTacticalD20BoardTileFactory::create(entt::registry& registry, int tileX, int tileY, int tileFeet, bool isWall, bool isCover) {
    auto entity = registry.create();
    registry.emplace<FTacticalBoardTile>(entity, tileX, tileY, tileFeet, isWall, isCover);
    registry.emplace<FPosition>(entity, TacticalD20TileToWorldX(tileX), TacticalD20TileToWorldY(tileY));
    registry.emplace<FCollider>(entity, EColliderType::AABB, glm::vec2{0.f, 0.f}, TacticalD20BoardTileSizePixels * 0.5f, TacticalD20BoardTileSizePixels * 0.5f, 2, "tactical_board_tile");
    registry.emplace<FSprite>(entity, TileTexture(isWall, isCover), static_cast<int>(TacticalD20BoardTileSizePixels), static_cast<int>(TacticalD20BoardTileSizePixels));
    registry.emplace<FTag>(entity, isWall ? "tactical_wall_tile" : "tactical_board_tile");
    registry.emplace<FLayer>(entity, isWall ? 2 : 1);
    return entity;
}

} // namespace game
