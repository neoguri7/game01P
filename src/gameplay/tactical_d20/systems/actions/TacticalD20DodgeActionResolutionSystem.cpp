#include "gameplay/tactical_d20/systems/actions/TacticalD20DodgeActionResolutionSystem.h"

#include "core/events/FEventPublishing.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FConditionDodge.h"
#include "ecs/components/FTacticalUnit.h"
#include "gameplay/tactical_d20/actions/FTacticalD20ActionResolutionUtils.h"
#include "gameplay/tactical_d20/events/FTacticalD20ConditionEvents.h"
#include "gameplay/tactical_d20/logging/FTacticalD20LogUtils.h"

#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

void ResolveDodge(entt::registry& registry, entt::entity unit) {
    ZoneScopedN("TacticalD20::ActionResolution");
    // Condition transition table:
    //   no FConditionDodge + Dodge resolved -> add FConditionDodge
    //   FConditionDodge + Dodge resolved -> refresh FConditionDodge
    // Action economy transition table:
    //   HasMoveAndAction/HasActionOnly + Dodge resolved -> TurnComplete
    registry.emplace_or_replace<FConditionDodge>(unit);
    SetTacticalD20ActionEconomy<FActionEconomyTurnComplete>(registry, unit);
    AppendTacticalD20CombatLog(registry, fmt::format("{} dodged.", registry.get<FTacticalUnit>(unit).displayName));
    const FTacticalD20ConditionChangedEvent condition{unit, "dodge", "applied", 1};
    PublishAndQueueFrameEvent(registry, condition);
    const FTacticalD20ConditionAppliedEvent applied{unit, "dodge", 1};
    PublishAndQueueFrameEvent(registry, applied);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] ConditionApplied unit={} condition=dodge remaining=1", TacticalD20EntityLogKey(registry, unit)));
    PublishTacticalD20ActionResolved(registry, unit, "dodge", true);
}

} // namespace

void TacticalD20DodgeActionResolutionSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20DodgeActionResolutionSystem");
    entt::entity unit{entt::null};
    FQueuedTacticalD20Command command;
    if (TryTakeQueuedTacticalD20Command(registry, "dodge", unit, command)) ResolveDodge(registry, unit);
}

} // namespace game
