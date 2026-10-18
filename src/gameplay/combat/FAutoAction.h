#pragma once

#include <optional>

#include <entt/entt.hpp>

#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {

/// The next skill or move for the unit named by the caller's turn token, decided by that unit's behavior tree
/// (src/gameplay/ai, docs/design/enemy-ai.md). Call it again after each action has been resolved; nullopt means
/// the unit has nothing more to do this turn. The caller owns turn scheduling; queued actions are validated
/// again by the action resolver.
[[nodiscard]] std::optional<FBattleAction> ChooseAutoAction(
    entt::registry& registry,
    FTurnToken token,
    bool targetPartySide,
    const gameplay::FContentRegistry& content);

} // namespace game
