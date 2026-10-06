#pragma once

#include <optional>

#include <entt/entt.hpp>

#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {

/// The fallback action when a unit is not player-driven (enemies; both sides under GAME01P_AUTOPLAY):
/// pick the first loadout slot whose resolved skill has a living target on `targetPartySide` within
/// range, and strike the nearest one. Movement joins a later slice, so a unit with no in-range target
/// has no action and its turn passes.
///
/// Combat policy lives here, not in the turn system (TurnFlowSystem), so the likely next changes
/// (movement, smarter targeting) stay isolated from turn bookkeeping.
[[nodiscard]] std::optional<FActionRequest> ChooseAutoAction(
    entt::registry& registry,
    entt::entity actor,
    bool targetPartySide,
    const gameplay::FContentRegistry& content);

} // namespace game
