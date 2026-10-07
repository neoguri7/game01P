#pragma once

#include <algorithm>

#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridTypes.h"

namespace game {

struct FBattleLayout {
    float partyWidth{};
    float journalWidth{};
    float headerHeight{78.f};
    float dockHeight{152.f};
};

[[nodiscard]] inline FBattleLayout BattleLayout(const FViewport& viewport) noexcept {
    return {std::min(184.f, viewport.width * 0.16f),
            viewport.width >= 1100.f ? 220.f : 0.f, 78.f, 152.f};
}

[[nodiscard]] inline FCamera FitBattleCamera(const FGridMetrics& metrics, const FViewport& viewport) noexcept {
    const auto layout = BattleLayout(viewport);
    const float width = std::max(1.f, metrics.width * metrics.tileSize);
    const float height = std::max(1.f, metrics.height * metrics.tileSize);
    const float availableWidth = std::max(1.f, viewport.width - layout.partyWidth - layout.journalWidth - 24.f);
    const float availableHeight = std::max(1.f, viewport.height - layout.headerHeight - layout.dockHeight - 16.f);
    const float zoom = std::clamp(std::min(availableWidth / width, availableHeight / height), 0.25f, 2.f);
    const glm::vec2 desiredCenter{(layout.partyWidth + viewport.width - layout.journalWidth) * 0.5f,
                                 (layout.headerHeight + viewport.height - layout.dockHeight) * 0.5f};
    const glm::vec2 screenCenter{viewport.width * 0.5f, viewport.height * 0.5f};
    return {metrics.origin + glm::vec2{width, height} * 0.5f + (screenCenter - desiredCenter) / zoom, zoom};
}

} // namespace game
