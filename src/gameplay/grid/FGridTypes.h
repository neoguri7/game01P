#pragma once

#include <glm/vec2.hpp>
#include <vector>

#include "ecs/components/FGridPosition.h"

namespace game {

inline constexpr int kGridWidth = 8;
inline constexpr int kGridHeight = 8;
inline constexpr float kTileSize = 64.f;
inline constexpr float kUnitHitboxInset = 6.f;

struct FGridMetrics {
	int width{kGridWidth};
	int height{kGridHeight};
	float tileSize{kTileSize};
	glm::vec2 origin{0.f, 0.f};
};

[[nodiscard]] bool IsInsideGrid(const FGridMetrics& metrics, int x, int y) noexcept;
[[nodiscard]] glm::vec2 GridCellTopLeftToWorld(const FGridMetrics& metrics, int x, int y) noexcept;
[[nodiscard]] glm::vec2 GridCellCenterToWorld(const FGridMetrics& metrics, int x, int y) noexcept;
[[nodiscard]] bool WorldToGridCell(const FGridMetrics& metrics, glm::vec2 world, int& outX, int& outY) noexcept;
[[nodiscard]] std::vector<FGridPosition> OrthogonalNeighbors(FGridPosition cell);

} // namespace game
