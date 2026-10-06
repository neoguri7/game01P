#include "gameplay/combat/FCombatQuery.h"

#include <limits>

#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FTag.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/factory/FEntityDefinitions.h"

namespace game {

bool IsAlive(entt::registry& registry, entt::entity unit) {
    return registry.valid(unit) && !registry.all_of<FDead>(unit);
}

std::string UnitDisplayName(entt::registry& registry, entt::entity unit) {
    if (!registry.valid(unit)) {
        return "(없음)";
    }
    if (const auto* data = registry.try_get<FUnitData>(unit)) {
        return data->definition.displayName;
    }
    if (const auto* tag = registry.try_get<FTag>(unit)) {
        return tag->value;
    }
    return std::to_string(entt::to_integral(unit));
}

int AliveCount(entt::registry& registry, bool partySide) {
    int count = 0;
    for (const auto entity : registry.view<FHealth>()) {
        if (!IsAlive(registry, entity)) {
            continue;
        }
        if (registry.all_of<FPartyMember>(entity) == partySide) {
            ++count;
        }
    }
    return count;
}

entt::entity FindNearestAlive(entt::registry& registry, entt::entity from, bool partySide) {
    const auto* fromCell = registry.try_get<FGridPosition>(from);
    if (fromCell == nullptr) {
        return entt::null;
    }

    entt::entity best = entt::null;
    int bestDistance = std::numeric_limits<int>::max();
    for (const auto entity : registry.view<FGridPosition, FHealth>()) {
        if (!IsAlive(registry, entity)) {
            continue;
        }
        if (registry.all_of<FPartyMember>(entity) != partySide) {
            continue;
        }
        const int distance = GridDistance(*fromCell, registry.get<FGridPosition>(entity));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = entity;
        }
    }
    return best;
}

} // namespace game
