#pragma once

#include <string>

#include <entt/entt.hpp>

namespace game {

/// Alive = valid and not tagged FDead. One representation of "out of the fight" (FDead), never inferred.
[[nodiscard]] bool IsAlive(entt::registry& registry, entt::entity unit);

/// Display name used by logs and the HUD: FUnitData first, FTag as fallback.
[[nodiscard]] std::string UnitDisplayName(entt::registry& registry, entt::entity unit);

/// Number of living units on one side (FPartyMember when `partySide`, FEnemy otherwise).
[[nodiscard]] int AliveCount(entt::registry& registry, bool partySide);

/// Nearest living unit on `partySide` measured in grid distance (Manhattan). entt::null when none.
/// Ties resolve to the first unit in view order; determinism of the AI is not promised yet.
[[nodiscard]] entt::entity FindNearestAlive(entt::registry& registry, entt::entity from, bool partySide);

} // namespace game
