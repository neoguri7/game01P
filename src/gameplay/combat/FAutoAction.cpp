#include "gameplay/combat/FAutoAction.h"

#include "ecs/components/FGridPosition.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FHealth.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTypes.h"
#include <algorithm>
#include <cstdlib>
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FSkillResolver.h"

namespace game {

std::optional<FActionRequest> ChooseAutoAction(
    entt::registry& registry,
    entt::entity actor,
    bool targetPartySide,
    const gameplay::FContentRegistry& content) {
    const auto* loadout = registry.try_get<FSkillLoadout>(actor);
    const auto* actorCell = registry.try_get<FGridPosition>(actor);
    const auto* points = registry.try_get<FActionPoints>(actor);
    if (loadout == nullptr || actorCell == nullptr || points == nullptr) {
        return std::nullopt;
    }

    for (const FSkillSlot& slot : loadout->slots) {
        const auto resolved = ResolveSkillSlot(slot, content);
        if (!resolved || points->skill < resolved->apCost) {
            continue;
        }
        const bool ownSide = resolved->target == "ally" || resolved->target == "self";
        const entt::entity target = resolved->target == "self" ? actor
            : FindNearestAlive(registry, actor, ownSide ? !targetPartySide : targetPartySide);
        if (target == entt::null) {
            continue;
        }
        const auto* targetCell = registry.try_get<FGridPosition>(target);
        if (targetCell == nullptr || !IsInRange(*actorCell, *targetCell, resolved->range)) {
            continue;
        }
        if (ownSide && resolved->power > 0) {
            const auto* health = registry.try_get<FHealth>(target);
            if (!health || health->current >= health->max) continue;
        }
        FActionRequest request{actor, target, slot.skillId};
        request.cell = *targetCell;
        return request;
    }
    const auto target = FindNearestAlive(registry, actor, targetPartySide);
    if (target == entt::null) {
        return std::nullopt;
    }
    const auto* targetCell = registry.try_get<FGridPosition>(target);
    if (targetCell && points->move > 0) {
        const auto distance = [&](FGridPosition cell) {
            return std::abs(cell.x - targetCell->x) + std::abs(cell.y - targetCell->y);
        };
        const auto reachable = ComputeReachableCells(registry, actor, points->move);
        const auto best = std::ranges::min_element(reachable, {}, distance);
        if (best != reachable.end() && distance(*best) < distance(*actorCell)) {
            FActionRequest request{actor};
            request.action = EBattleAction::Move;
            request.cell = *best;
            return request;
        }
    }
    return std::nullopt;
}

} // namespace game
