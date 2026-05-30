#include "gameplay/tactical_d20/systems/actions/TacticalD20WaitActionResolutionSystem.h"

#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FTacticalUnit.h"
#include "gameplay/tactical_d20/actions/FTacticalD20ActionResolutionUtils.h"
#include "gameplay/tactical_d20/logging/FTacticalD20LogUtils.h"

#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

void ResolveWait(entt::registry& registry, entt::entity unit) {
    ZoneScopedN("TacticalD20::ActionResolution");
    // Action economy transition table:
    //   any active action economy state + Wait resolved -> TurnComplete
    SetTacticalD20ActionEconomy<FActionEconomyTurnComplete>(registry, unit);
    AppendTacticalD20CombatLog(registry, fmt::format("{} waited.", registry.get<FTacticalUnit>(unit).displayName));
    PublishTacticalD20ActionResolved(registry, unit, "wait", true);
}

} // namespace

void TacticalD20WaitActionResolutionSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20WaitActionResolutionSystem");
    entt::entity unit{entt::null};
    FQueuedTacticalD20Command command;
    if (TryTakeQueuedTacticalD20Command(registry, "wait", unit, command)) ResolveWait(registry, unit);
}

} // namespace game
