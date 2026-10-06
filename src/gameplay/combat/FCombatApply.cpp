#include "gameplay/combat/FCombatApply.h"

#include <algorithm>

#include "ecs/components/FDead.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FTurnParticipant.h"

namespace game {

bool ApplyDamageToUnit(entt::registry& registry, entt::entity target, int amount) {
    auto* health = registry.try_get<FHealth>(target);
    if (health == nullptr) {
        return false;
    }

    health->current = std::max(0, health->current - std::max(0, amount));
    if (health->current > 0) {
        return false;
    }

    if (!registry.all_of<FDead>(target)) {
        registry.emplace<FDead>(target);
        registry.remove<FTurnParticipant>(target);
    }
    return true;
}

} // namespace game
