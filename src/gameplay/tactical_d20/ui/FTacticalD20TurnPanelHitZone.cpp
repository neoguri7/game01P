#include "gameplay/tactical_d20/ui/FTacticalD20TurnPanelHitZone.h"

#include "ecs/components/FCollider.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FTacticalBoardTile.h"

#include <algorithm>

namespace game {

bool IsFallbackTacticalD20TurnPanelHit(entt::registry& registry, const glm::vec2& point) {
    bool hasBoard = false;
    float boardRight = 0.f;
    float boardTop = 0.f;
    float boardBottom = 0.f;

    auto view = registry.view<FTacticalBoardTile, FPosition, FCollider>();
    for (auto entity : view) {
        const auto& position = view.get<FPosition>(entity);
        const auto& collider = view.get<FCollider>(entity);
        const float right = position.x + collider.offset.x + collider.halfWidth;
        const float top = position.y + collider.offset.y - collider.halfHeight;
        const float bottom = position.y + collider.offset.y + collider.halfHeight;
        if (!hasBoard) {
            boardRight = right;
            boardTop = top;
            boardBottom = bottom;
            hasBoard = true;
            continue;
        }
        boardRight = std::max(boardRight, right);
        boardTop = std::min(boardTop, top);
        boardBottom = std::max(boardBottom, bottom);
    }

    // Phase 5 has no turn-panel entity. This deterministic right-of-board zone
    // lets Wait target the panel until phase 7 UI replaces it with real hit UI.
    const float left = hasBoard ? boardRight + 24.f : 608.f;
    const float right = left + 320.f;
    const float top = hasBoard ? boardTop + 128.f : 160.f;
    const float bottom = hasBoard ? boardBottom : 416.f;
    return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

} // namespace game
