#pragma once

#include <optional>

#include <entt/entt.hpp>

#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {

/// Choose an affordable skill or an obstacle-aware move for the caller's turn token.
/// The caller owns turn scheduling; queued actions are validated again by the action resolver.
[[nodiscard]] std::optional<FBattleAction> ChooseAutoAction(
    entt::registry& registry,
    FTurnToken token,
    bool targetPartySide,
    const gameplay::FContentRegistry& content);

} // namespace game
