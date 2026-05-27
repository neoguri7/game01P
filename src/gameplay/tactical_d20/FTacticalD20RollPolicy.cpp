#include "gameplay/tactical_d20/FTacticalD20RollPolicy.h"

#include "ecs/components/FConditionDodge.h"
#include "ecs/components/FConditionPoisoned.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"

namespace game {
namespace {

void AccumulateMode(ETacticalD20RollMode mode, bool& hasAdvantage, bool& hasDisadvantage) {
    hasAdvantage = hasAdvantage || mode == ETacticalD20RollMode::Advantage;
    hasDisadvantage = hasDisadvantage || mode == ETacticalD20RollMode::Disadvantage;
}

FTacticalD20RollPolicy BuildPolicy(bool hasAdvantage, bool hasDisadvantage) {
    const auto mode = ResolveRollMode(ETacticalD20RollMode::Normal, hasAdvantage, hasDisadvantage);
    return {.mode = mode, .disadvantageApplied = mode == ETacticalD20RollMode::Disadvantage};
}

} // namespace

FTacticalD20RollPolicy TacticalD20AttackRollPolicy(entt::registry& registry, entt::entity attacker, entt::entity target) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    bool hasAdvantage = false;
    bool hasDisadvantage = false;

    if (registry.all_of<FConditionPoisoned>(attacker)) {
        const auto configured = config ? config->conditions.poisonedAttackRollMode : "disadvantage";
        AccumulateMode(RollModeFromString(configured, ETacticalD20RollMode::Disadvantage), hasAdvantage, hasDisadvantage);
    }
    if (registry.all_of<FConditionDodge>(target)) {
        const auto configured = config ? config->actions.dodgeIncomingAttackMode : "disadvantage";
        AccumulateMode(RollModeFromString(configured, ETacticalD20RollMode::Disadvantage), hasAdvantage, hasDisadvantage);
    }
    return BuildPolicy(hasAdvantage, hasDisadvantage);
}

FTacticalD20RollPolicy TacticalD20AbilityCheckRollPolicy(entt::registry& registry, entt::entity unit) {
    if (!registry.all_of<FConditionPoisoned>(unit)) return {};

    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const auto configured = config ? config->conditions.poisonedAbilityCheckMode : "disadvantage";
    bool hasAdvantage = false;
    bool hasDisadvantage = false;
    AccumulateMode(RollModeFromString(configured, ETacticalD20RollMode::Disadvantage), hasAdvantage, hasDisadvantage);
    return BuildPolicy(hasAdvantage, hasDisadvantage);
}

} // namespace game
