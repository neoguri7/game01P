#pragma once

#include <entt/entt.hpp>

namespace game {

struct FCombatLog;

/// Applies `amount` damage: HP is clamped at 0; when that kills the unit it gains FDead, drops
/// FTurnParticipant (TurnController.synchronize then skips it), and the kill is logged.
/// Returns true when this application killed the unit.
[[nodiscard]] bool ApplyDamageToUnit(
    entt::registry& registry, entt::entity target, int amount, FCombatLog* log);

} // namespace game
