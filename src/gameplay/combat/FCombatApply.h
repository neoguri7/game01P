#pragma once

#include <entt/entt.hpp>

namespace game {

/// Applies `amount` damage: HP is clamped at 0; when that kills the unit it gains FDead and drops
/// FTurnParticipant (TurnController.synchronize then skips it). Pure state change: callers own the
/// battle feed, so a lethal hit can be reported after the hit itself (order matters in the feed).
/// Returns true when this application killed the unit.
[[nodiscard]] bool ApplyDamageToUnit(entt::registry& registry, entt::entity target, int amount);

} // namespace game
