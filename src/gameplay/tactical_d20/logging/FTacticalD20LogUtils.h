#pragma once

#include "gameplay/tactical_d20/events/FTacticalD20CommandEvents.h"

#include <entt/entt.hpp>
#include <string>

namespace game {

void AppendTacticalD20StateLog(entt::registry& registry, const std::string& line);
void AppendTacticalD20EventLog(entt::registry& registry, const std::string& line);
void AppendTacticalD20CombatLog(entt::registry& registry, const std::string& line);
void AppendTacticalD20CommandValidationLogs(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event);
std::string TacticalD20EntityLogKey(entt::registry& registry, entt::entity entity);

} // namespace game
