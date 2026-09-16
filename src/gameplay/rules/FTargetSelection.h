#pragma once

#include "gameplay/components/FDowned.h"
#include "gameplay/components/FGridPosition.h"
#include "gameplay/components/FTeamEnemy.h"
#include "gameplay/components/FTeamPlayer.h"
#include "gameplay/rules/FGridDistance.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <vector>

namespace game::gameplay {

/// Living units on the opposing side, in a *stable* order (ascending entity id == spawn order).
/// why sorted: the player cycles targets with Tab and the AI picks the nearest — both need an order that does
/// not change between frames or depend on registry storage iteration (R16).
[[nodiscard]] inline std::vector<entt::entity> livingOpponents(entt::registry& registry, entt::entity self) {
    std::vector<entt::entity> opponents;

    // why two branches instead of one view with an "is opponent" predicate: team identity is a tag (R11), and
    // EnTT views are typed per component set — the exclusion belongs to the view, not to an if inside the loop.
    if (registry.all_of<FTeamPlayer>(self)) {
        for (auto [candidate, position] : registry.view<FTeamEnemy, FGridPosition>(entt::exclude<FDowned>).each()) {
            (void)position;
            opponents.push_back(candidate);
        }
    } else {
        for (auto [candidate, position] : registry.view<FTeamPlayer, FGridPosition>(entt::exclude<FDowned>).each()) {
            (void)position;
            opponents.push_back(candidate);
        }
    }

    std::sort(opponents.begin(), opponents.end());
    return opponents;
}

/// Nearest living opponent, ties going to the lower entity id (the sorted order above already decides it).
/// why a free function: the default player target and 미결(design §4) 몬스터 AI share this rule; when the AI is
/// designed, only the caller changes.
[[nodiscard]] inline entt::entity nearestLivingOpponent(entt::registry& registry, entt::entity self) {
    const FGridPosition* selfPosition = registry.try_get<FGridPosition>(self);
    if (selfPosition == nullptr) {
        return entt::null;
    }

    entt::entity best = entt::null;
    int bestDistance = 0;
    for (const entt::entity candidate : livingOpponents(registry, self)) {
        const FGridPosition* position = registry.try_get<FGridPosition>(candidate);
        if (position == nullptr) {
            continue;
        }
        const int distance = gridDistance(*selfPosition, *position);
        if (best == entt::null || distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

/// True when `to` is within `range` cells of `from` (Chebyshev via FGridDistance).
/// why this exists: range is asked by the AI's `opponent_in_skill_range` fact AND validated by
/// FSkillResolveSystem — a second comparison would eventually let a rule pick a target the resolver rejects
/// (R12). The resolver calls this same predicate.
[[nodiscard]] inline bool withinSkillRange(const FGridPosition& from, const FGridPosition& to, int range) {
    return gridDistance(from, to) <= range;
}

/// Nearest living opponent *inside* `range` cells, or entt::null when none is in range. Same tie rule as
/// `nearestLivingOpponent` (lower entity id wins), so a rule and the player's default target never disagree (R16).
[[nodiscard]] inline entt::entity nearestLivingOpponentWithinRange(entt::registry& registry,
                                                                   entt::entity self,
                                                                   int range) {
    const FGridPosition* selfPosition = registry.try_get<FGridPosition>(self);
    if (selfPosition == nullptr) {
        return entt::null;
    }

    entt::entity best = entt::null;
    int bestDistance = 0;
    for (const entt::entity candidate : livingOpponents(registry, self)) {
        const FGridPosition* position = registry.try_get<FGridPosition>(candidate);
        if (position == nullptr || !withinSkillRange(*selfPosition, *position, range)) {
            continue;
        }
        const int distance = gridDistance(*selfPosition, *position);
        if (best == entt::null || distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace game::gameplay
