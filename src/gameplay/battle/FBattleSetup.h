#pragma once

#include <entt/entt.hpp>

#include "gameplay/data/FContentRegistry.h"
#include "gameplay/grid/FBattlefieldData.h"

namespace game {

/// Attaches the combat components (stats/health/AP/speed/loadout/statuses + turn participation) to the
/// entities FEntityFactory created for a battlefield. Numbers come from unit content; the placement's
/// `skills` override carries the run-scope loadout (skills + runes).
/// False means the field cannot be fought (reason already logged).
[[nodiscard]] bool SetupCombatUnits(
    entt::registry& registry,
    const FBattlefieldData& battlefield,
    const gameplay::FContentRegistry& content);

} // namespace game
