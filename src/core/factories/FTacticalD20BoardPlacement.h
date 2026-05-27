#pragma once

namespace game {

constexpr float TacticalD20BoardTileSizePixels = 64.f;
constexpr float TacticalD20BoardOriginX = 64.f;
constexpr float TacticalD20BoardOriginY = 64.f;

float TacticalD20TileToWorldX(int tileX);
float TacticalD20TileToWorldY(int tileY);

} // namespace game
