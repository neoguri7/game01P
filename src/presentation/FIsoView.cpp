#include "presentation/FIsoView.h"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

// Block thickness under the diamond face of the isometric ground tiles.
constexpr float kTileThickness = 12.f;

} // namespace

FIsoView MakeIsoView(const FGridMetrics& metrics, float viewportWidth, float viewportHeight) noexcept {
    const float span = static_cast<float>(metrics.width + metrics.height);
    const float mapWidth = span * kIsoHalfWidth;
    const float mapHeight = span * kIsoHalfHeight + kTileThickness;
    const float availableWidth = std::max(1.f, viewportWidth - 32.f);
    const float availableHeight = std::max(1.f, viewportHeight - kIsoTopReserve - kIsoBottomReserve);
    const float fit = std::min(availableWidth / mapWidth, availableHeight / mapHeight);
    FIsoView view;
    view.scale = std::max(1.f, std::floor(fit * 2.f) / 2.f);
    view.origin.x = std::floor(viewportWidth * .5f + kIsoCenterShift +
                               static_cast<float>(metrics.height - metrics.width) * .5f * kIsoHalfWidth * view.scale);
    view.origin.y = std::floor(kIsoTopReserve + (availableHeight - mapHeight * view.scale) * .5f);
    return view;
}

bool IsoScreenToCell(const FIsoView& view, const FGridMetrics& metrics, glm::vec2 screen, int& outX, int& outY) noexcept {
    const float dx = (screen.x - view.origin.x) / (kIsoHalfWidth * view.scale);
    const float dy = (screen.y - view.origin.y) / (kIsoHalfHeight * view.scale);
    const int x = static_cast<int>(std::floor((dx + dy) * .5f));
    const int y = static_cast<int>(std::floor((dy - dx) * .5f));
    if (x < 0 || y < 0 || x >= metrics.width || y >= metrics.height) return false;
    outX = x;
    outY = y;
    return true;
}

} // namespace game
