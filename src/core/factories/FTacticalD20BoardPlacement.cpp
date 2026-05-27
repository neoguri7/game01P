#include "core/factories/FTacticalD20BoardPlacement.h"

namespace game {

float TacticalD20TileToWorldX(int tileX) {
    return TacticalD20BoardOriginX + static_cast<float>(tileX) * TacticalD20BoardTileSizePixels;
}

float TacticalD20TileToWorldY(int tileY) {
    return TacticalD20BoardOriginY + static_cast<float>(tileY) * TacticalD20BoardTileSizePixels;
}

} // namespace game
