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

/// True when `to` is within `range` cells of `from` (Chebyshev via FGridDistance).
/// why this exists: range is asked by the AI's `opponent_in_skill_range` fact AND validated by
/// FSkillResolveSystem — a second comparison would eventually let a rule pick a target the resolver rejects
/// (R12). The resolver calls this same predicate.
[[nodiscard]] inline bool withinSkillRange(const FGridPosition& from, const FGridPosition& to, int range) {
    return gridDistance(from, to) <= range;
}

namespace detail {

/// Nearest living opponent whose cell `acceptPosition(self, candidate)` accepts, or entt::null.
/// why one helper for both public queries: the tie rule (lower entity id wins — the sorted order above) and the
/// missing-cell handling are the parts that must not drift between "any opponent" and "an opponent in range"
/// (R12); the two queries differ only in which candidates they accept.
/// `invariant:` the accepted candidate set is decided by the caller's predicate, never by iteration order —
/// `livingOpponents` is sorted, and only a strictly smaller distance replaces the current best.
template <typename AcceptPosition>
[[nodiscard]] inline entt::entity nearestLivingOpponentWhere(entt::registry& registry,
                                                             entt::entity self,
                                                             AcceptPosition acceptPosition) {
    const FGridPosition* selfPosition = registry.try_get<FGridPosition>(self);
    if (selfPosition == nullptr) {
        return entt::null;
    }

    entt::entity best = entt::null;
    int bestDistance = 0;
    for (const entt::entity candidate : livingOpponents(registry, self)) {
        const FGridPosition* position = registry.try_get<FGridPosition>(candidate);
        if (position == nullptr || !acceptPosition(*selfPosition, *position)) {
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

} // namespace detail

/// Nearest living opponent, ties going to the lower entity id (the sorted order above already decides it).
/// why a free function: the default player target and the monster's behaviour rules share this rule, so a
/// balance change to "nearest" moves both (R12).
[[nodiscard]] inline entt::entity nearestLivingOpponent(entt::registry& registry, entt::entity self) {
    return detail::nearestLivingOpponentWhere(registry, self, [](const FGridPosition&, const FGridPosition&) {
        return true;
    });
}

/// Nearest living opponent *inside* `range` cells, or entt::null when none is in range. Uses `withinSkillRange`,
/// so a rule and the resolver's own check can never disagree about who is reachable (R12), and the tie rule is
/// the shared one (lower entity id wins) so a rule and the player's default target never disagree (R16).
[[nodiscard]] inline entt::entity nearestLivingOpponentWithinRange(entt::registry& registry,
                                                                   entt::entity self,
                                                                   int range) {
    return detail::nearestLivingOpponentWhere(
        registry,
        self,
        [range](const FGridPosition& from, const FGridPosition& to) { return withinSkillRange(from, to, range); });
}

} // namespace game::gameplay
