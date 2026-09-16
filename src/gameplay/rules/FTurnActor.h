#pragma once

#include "gameplay/components/FTeamEnemy.h"
#include "gameplay/components/FTeamPlayer.h"
#include "gameplay/components/FTurnActive.h"

#include <entt/entt.hpp>

namespace game::gameplay {

/// The unit whose turn is open, or entt::null when none is.
/// why shared here: the move system, the command system and the AI all ask "whose turn is it" — a second
/// implementation would eventually disagree at a phase boundary (R12).
[[nodiscard]] inline entt::entity activeUnit(entt::registry& registry) {
    for (auto entity : registry.view<FTurnActive>()) {
        return entity;
    }
    return entt::null;
}

[[nodiscard]] inline entt::entity activePlayerUnit(entt::registry& registry) {
    const entt::entity unit = activeUnit(registry);
    if (unit == entt::null || !registry.all_of<FTeamPlayer>(unit)) {
        return entt::null;
    }
    return unit;
}

[[nodiscard]] inline entt::entity activeEnemyUnit(entt::registry& registry) {
    const entt::entity unit = activeUnit(registry);
    if (unit == entt::null || !registry.all_of<FTeamEnemy>(unit)) {
        return entt::null;
    }
    return unit;
}

} // namespace game::gameplay
