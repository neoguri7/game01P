#include "gameplay/combat/FAutoAction.h"

#include "ecs/components/FGridPosition.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FSkillResolver.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FFaction.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTerrain.h"
#include <algorithm>
#include <limits>

namespace game {

std::optional<FBattleAction> ChooseAutoAction(
    entt::registry& registry,
    FTurnToken token,
    bool targetPartySide,
    const gameplay::FContentRegistry& content) {
    const auto actor = token.actor;
    const auto* loadout = registry.try_get<FSkillLoadout>(actor);
    const auto* actorCell = registry.try_get<FGridPosition>(actor);
    const auto* points = registry.try_get<FActionPoints>(actor);
    if (!IsAlive(registry, actor) || !loadout || !actorCell || !points) {
        return std::nullopt;
    }

    for (const FSkillSlot& slot : loadout->slots) {
        const auto resolved = ResolveSkillSlot(slot, content);
        if (!resolved || points->skill < resolved->apCost) {
            continue;
        }
        const bool ownSide = resolved->target == "ally" || resolved->target == "self";
        for (const auto target : registry.view<FGridPosition, FHealth>()) {
            if (!IsAlive(registry, target) || registry.all_of<FPartyMember>(target) !=
                (ownSide ? !targetPartySide : targetPartySide)) continue;
            if (resolved->target == "self" && target != actor) continue;
            if (ownSide && registry.get<FHealth>(target).current >= registry.get<FHealth>(target).max) continue;
            const auto targetCell = registry.get<FGridPosition>(target);
            if (IsInRange(*actorCell, targetCell, resolved->range) &&
                HasGridLineOfSight(registry, *actorCell, targetCell)) {
                return FActionRequest{actor, target, slot.skillId, token};
            }
        }
    }
    if (points->move <= 0) return std::nullopt;
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    if (!terrain) return std::nullopt;
    const auto reachable = ComputeReachableCells(registry, actor, terrain->moveRange);
    // Score full paths so the AI can route around obstacles, not just approach in a straight line.
    int bestScore = std::numeric_limits<int>::max();
    std::optional<FGridPosition> best;
    for (const auto target : registry.view<FGridPosition, FHealth>()) {
        if (!IsAlive(registry, target) || registry.all_of<FPartyMember>(target) != targetPartySide) continue;
        const auto targetCell = registry.get<FGridPosition>(target);
        for (const auto neighbor : OrthogonalNeighbors(targetCell)) {
            const auto path = FindGridPath(registry, actor, neighbor, kGridWidth * kGridHeight);
            if (path.empty()) continue;
            const auto destination = path[std::min(path.size(), static_cast<std::size_t>(terrain->moveRange)) - 1];
            if (std::ranges::find_if(reachable, [&](auto cell) {
                    return cell.x == destination.x && cell.y == destination.y;
                }) == reachable.end()) continue;
            int score = static_cast<int>(path.size()) * 10;
            for (const auto cell : FindGridPath(registry, actor, destination, terrain->moveRange)) {
                if (terrain->at(cell.x, cell.y) == ETile::Trap) score += 15;
            }
            if (score < bestScore) { bestScore = score; best = destination; }
        }
    }
    if (best) return FMoveRequest{actor, *best, token};
    return std::nullopt;
}

} // namespace game
