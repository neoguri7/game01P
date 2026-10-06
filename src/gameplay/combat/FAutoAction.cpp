#include "gameplay/combat/FAutoAction.h"

#include "ecs/components/FGridPosition.h"
#include "ecs/components/FSkillLoadout.h"
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
    if (loadout == nullptr || actorCell == nullptr) {
        return std::nullopt;
    }

    for (const FSkillSlot& slot : loadout->slots) {
        const auto resolved = ResolveSkillSlot(slot, content);
        if (!resolved) {
            continue;
        }
        const entt::entity target = FindNearestAlive(registry, actor, targetPartySide);
        if (target == entt::null) {
            return std::nullopt;
        }
        const auto* targetCell = registry.try_get<FGridPosition>(target);
        if (targetCell == nullptr || !IsInRange(*actorCell, *targetCell, resolved->range)) {
            continue;
        }
        return FActionRequest{actor, target, slot.skillId};
    }
    return std::nullopt;
}

} // namespace game
