#include "gameplay/grid/FGridTypes.h"

#include <cmath>

namespace game {

bool IsInsideGrid(const FGridMetrics& metrics, int x, int y) noexcept {
    return x >= 0 && y >= 0 && x < metrics.width && y < metrics.height;
}

glm::vec2 GridCellTopLeftToWorld(const FGridMetrics& metrics, int x, int y) noexcept {
    return metrics.origin + glm::vec2{
        static_cast<float>(x) * metrics.tileSize,
        static_cast<float>(y) * metrics.tileSize
    };
}

glm::vec2 GridCellCenterToWorld(const FGridMetrics& metrics, int x, int y) noexcept {
    return GridCellTopLeftToWorld(metrics, x, y) + glm::vec2{metrics.tileSize * 0.5f};
}

bool WorldToGridCell(const FGridMetrics& metrics, glm::vec2 world, int& outX, int& outY) noexcept {
    if (metrics.width <= 0 || metrics.height <= 0 || metrics.tileSize <= 0.f) return false;

    const auto cell = (world - metrics.origin) / metrics.tileSize;
    if (!std::isfinite(cell.x) || !std::isfinite(cell.y) ||
        cell.x < 0.f || cell.y < 0.f ||
        cell.x >= static_cast<float>(metrics.width) ||
        cell.y >= static_cast<float>(metrics.height)) {
        return false;
    }
    outX = static_cast<int>(std::floor(cell.x));
    outY = static_cast<int>(std::floor(cell.y));
    return true;
}

std::vector<FGridPosition> OrthogonalNeighbors(FGridPosition cell) {
    return {
        {cell.x, cell.y - 1},
        {cell.x - 1, cell.y},
        {cell.x + 1, cell.y},
        {cell.x, cell.y + 1}
    };
}

} // namespace game
