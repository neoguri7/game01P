#include "gameplay/tactical_d20/TacticalD20ConditionSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateTurnStart.h"
#include "ecs/components/FConditionBurning.h"
#include "ecs/components/FConditionDodge.h"
#include "ecs/components/FConditionPoisoned.h"
#include "ecs/components/FConditionStunned.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20LogUtils.h"
#include "gameplay/tactical_d20/FTacticalD20Random.h"
#include "gameplay/tactical_d20/FTacticalD20Rules.h"

#include <fmt/format.h>
#include <string>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

entt::entity ActiveUnitRaw(entt::registry& registry) {
    auto view = registry.view<FActiveTacticalUnit, FTacticalUnit>();
    for (auto entity : view) return entity;
    return entt::null;
}

bool IsTurnStart(entt::registry& registry) {
    return !registry.view<FCombatStateTurnStart>().empty();
}

void QueueActionResolved(entt::registry& registry, entt::entity unit, const std::string& action) {
    const int movement = registry.all_of<FTurnBudget>(unit) ? registry.get<FTurnBudget>(unit).movementBudgetTiles : 0;
    const FTacticalD20ActionResolvedEvent event{unit, action, movement, true};
    PUBLISH(FTacticalD20ActionResolvedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20ActionResolvedEvent, registry, event);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] ActionResolved action={} complete=true", action));
    AppendTacticalD20StateLog(registry, fmt::format("[Action] {} resolved", action));
}

void PublishCondition(entt::registry& registry, entt::entity unit, const std::string& condition, const std::string& change, int remaining) {
    const FTacticalD20ConditionChangedEvent event{unit, condition, change, remaining};
    PUBLISH(FTacticalD20ConditionChangedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20ConditionChangedEvent, registry, event);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] ConditionChanged condition={} change={} remaining={}", condition, change, remaining));
}

void PublishDamage(entt::registry& registry, entt::entity unit, const FTacticalD20DamageApplicationResult& applied) {
    const FTacticalD20DamageAppliedEvent event{unit, unit, "fire", applied.damageApplied, applied.hpBefore, applied.hpAfter, applied.defeated};
    PUBLISH(FTacticalD20DamageAppliedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20DamageAppliedEvent, registry, event);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] DamageApplied type=fire amount={} hp={}->{} defeated={}",
        applied.damageApplied,
        applied.hpBefore,
        applied.hpAfter,
        applied.defeated));
}

void ExpireDodge(entt::registry& registry, entt::entity unit) {
    if (!registry.all_of<FConditionDodge>(unit)) return;
    registry.remove<FConditionDodge>(unit);
    PublishCondition(registry, unit, "dodge", "expired", 0);
    AppendTacticalD20CombatLog(registry, fmt::format("{} is no longer dodging.", registry.get<FTacticalUnit>(unit).displayName));
}

bool ApplyBurning(entt::registry& registry, entt::entity unit) {
    auto* burning = registry.try_get<FConditionBurning>(unit);
    if (!burning) return false;
    if (!registry.ctx().contains<FTacticalD20Random>()) registry.ctx().emplace<FTacticalD20Random>();
    auto& random = registry.ctx().get<FTacticalD20Random>();
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const std::string dice = config ? config->conditions.burningDamageDice : "1d4";
    const auto damage = ResolveDamage(dice, 0, false, random.rng);
    const auto applied = ApplyDamageAndDefeat(registry, unit, damage.finalDamage);
    burning->remainingRounds -= 1;
    const int remaining = burning->remainingRounds;
    if (remaining <= 0) registry.remove<FConditionBurning>(unit);
    PublishDamage(registry, unit, applied);
    PublishCondition(registry, unit, "burning", remaining <= 0 ? "expired" : "ticked", remaining);
    AppendTacticalD20CombatLog(registry, fmt::format("{} burns for {} damage. HP {} -> {}{}.",
        registry.get<FTacticalUnit>(unit).displayName,
        applied.damageApplied,
        applied.hpBefore,
        applied.hpAfter,
        applied.defeated ? " defeated" : ""));
    return applied.defeated;
}

bool ApplyStunned(entt::registry& registry, entt::entity unit) {
    auto* stunned = registry.try_get<FConditionStunned>(unit);
    if (!stunned) return false;
    stunned->remainingTurns -= 1;
    const int remaining = stunned->remainingTurns;
    if (remaining <= 0) registry.remove<FConditionStunned>(unit);
    PublishCondition(registry, unit, "stunned", remaining <= 0 ? "expired" : "ticked", remaining);
    AppendTacticalD20CombatLog(registry, fmt::format("{} is stunned and skips the turn.", registry.get<FTacticalUnit>(unit).displayName));
    return true;
}

void HandleTurnStart(entt::registry& registry) {
    // Condition transition table:
    //   FConditionDodge + affected unit turn start -> remove FConditionDodge
    //   FConditionBurning remainingRounds > 1 + turn start -> decrement
    //   FConditionBurning remainingRounds <= 1 + turn start -> remove FConditionBurning
    //   FConditionPoisoned remainingRounds > 1 + turn start -> decrement
    //   FConditionPoisoned remainingRounds == 1 + turn start -> remove FConditionPoisoned
    //   FConditionPoisoned remainingRounds < 0 + turn start -> no tag change
    //   FConditionStunned remainingTurns > 1 + turn start -> decrement and skip
    //   FConditionStunned remainingTurns <= 1 + turn start -> remove FConditionStunned and skip
    const auto unit = ActiveUnitRaw(registry);
    if (unit == entt::null || registry.all_of<FUnitStateDefeated>(unit)) return;
    ExpireDodge(registry, unit);
    if (auto* poisoned = registry.try_get<FConditionPoisoned>(unit); poisoned && poisoned->remainingRounds > 0) {
        poisoned->remainingRounds -= 1;
        const int remaining = poisoned->remainingRounds;
        if (remaining <= 0) registry.remove<FConditionPoisoned>(unit);
        PublishCondition(registry, unit, "poisoned", remaining <= 0 ? "expired" : "ticked", remaining);
    }
    const bool defeatedByBurning = ApplyBurning(registry, unit);
    if (defeatedByBurning) {
        registry.emplace_or_replace<FActionEconomyTurnComplete>(unit);
        QueueActionResolved(registry, unit, "burning");
        return;
    }
    if (ApplyStunned(registry, unit)) {
        registry.emplace_or_replace<FActionEconomyTurnComplete>(unit);
        QueueActionResolved(registry, unit, "stunned");
    }
}

} // namespace

void TacticalD20ConditionSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20ConditionSystem");
    if (IsTurnStart(registry)) HandleTurnStart(registry);
}

} // namespace game
