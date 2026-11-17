#include "gameplay/combat/FCombatApply.h"

#include <algorithm>

#include "ecs/components/FBossGoals.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FTurnParticipant.h"

namespace game {

bool ApplyDamageToUnit(entt::registry& registry, entt::entity target, int amount) {
    auto* health = registry.try_get<FHealth>(target);
    if (health == nullptr) {
        return false;
    }

    // WP-15 safety net: the boss gate caps damage that does not come from a skill preview (traps, status
    // ticks, hook damage). Preview-driven skill damage already carries the capped number from PreviewSkill
    // (the canon), so this is idempotent there. The helper also writes the one-per-battle "first blocked
    // hit" line. See FBossGoals.h (BossBodyDamageCap / ApplyBossGateToHit).
    amount = ApplyBossGateToHit(registry, target, amount);

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
