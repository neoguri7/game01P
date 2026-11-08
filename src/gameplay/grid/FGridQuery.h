#pragma once

#include <entt/entt.hpp>
#include <vector>

#include "ecs/components/FGridPosition.h"

namespace game {

/// How a displacement effect moves its target relative to `from` (WP-16): a push moves away from it, a
/// pull moves toward it. The grid rules only speak this vocabulary; the data authors it as `from:"caster"`.
enum class EDisplacementDirection { Away, Toward };

/// One displacement attempt's outcome (WP-16). Pure data: ComputePushDestination mutates nothing.
struct FPushResult {
    FGridPosition finalCell{};       ///< the cell the unit ends on (its own cell when it cannot move)
    std::vector<FGridPosition> path; ///< the walked cells, start first (mirrors FBattleEvent Move.path);
                                     ///< a path of size <= 1 means the unit did not move
    bool hitObstacle{false};         ///< stopped before the full distance on a wall, a unit or the grid edge
};

/// Computes where a push (Away from `from`) or a pull (Toward `from`) of `distance` cells would take
/// `target` (WP-16 — the resolver applies it, the AI may plan with it later). The direction is the unit
/// step on the axis with the larger |delta| between `from` and the target's cell; a tie picks the x axis,
/// so an orthogonal (range 1) attacker always pushes straight back. The unit stops on the last valid cell
/// before a wall, an occupied cell or the grid edge; a pull that stops because the next cell is `from`
/// itself is the intended stop (the caster cannot be entered) and is NOT an obstacle. A pull also never
/// carries the target past the caster on the step axis: when the target is off the caster's axis, the
/// step walks until the next cell's step-axis coordinate would pass `from`'s — reaching the caster's
/// column/row laterally is fine, and that stop is the intended stop (no hitObstacle). `distance <= 0` or
/// `from ==` the target's cell moves nothing. Dead units do not block (FGridQuery occupancy rule).
[[nodiscard]] FPushResult ComputePushDestination(
    entt::registry& registry,
    entt::entity target,
    FGridPosition from,
    int distance,
    EDisplacementDirection direction = EDisplacementDirection::Away);

[[nodiscard]] entt::entity FindGridOccupant(entt::registry& registry, int x, int y);
[[nodiscard]] bool IsGridCellOccupied(entt::registry& registry, int x, int y);
[[nodiscard]] std::vector<FGridPosition> ComputeReachableCells(
    entt::registry& registry,
    entt::entity unit,
    int moveAp
);
[[nodiscard]] std::vector<FGridPosition> FindGridPath(
    entt::registry& registry, entt::entity unit, FGridPosition destination, int maxDistance);
[[nodiscard]] bool HasGridLineOfSight(entt::registry& registry, FGridPosition from, FGridPosition to);

} // namespace game
