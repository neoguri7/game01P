#include "presentation/FIsoView.h"

#include <algorithm>
#include <cmath>

namespace game {

FIsoView MakeIsoView(const FGridMetrics& metrics, const FIsoViewport& viewport) noexcept {
    const float span = static_cast<float>(metrics.width + metrics.height);
    const float mapWidth = span * kIsoHalfWidth;
    const float mapHeight = span * kIsoHalfHeight;
    const float availableWidth = std::max(1.f, viewport.width - 32.f);
    const float availableHeight = std::max(1.f, viewport.height);
    const float fit = std::min(availableWidth / mapWidth, availableHeight / mapHeight);
    FIsoView view;
    // The epsilon keeps a viewport that is exactly one step tall from rounding down to the smaller scale.
    view.scale = std::max(1.f, std::floor(fit * 2.f + 1e-3f) / 2.f);
    view.origin.x = std::floor(viewport.x + viewport.width * .5f +
                               static_cast<float>(metrics.height - metrics.width) * .5f * kIsoHalfWidth * view.scale);
    view.origin.y = std::floor(viewport.y + (availableHeight - mapHeight * view.scale) * .5f);
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
