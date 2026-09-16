#pragma once

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FApPool.h"
#include "gameplay/components/FGridPosition.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/rules/FGridOccupancy.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <fmt/format.h>

#include <optional>
#include <string>
#include <utility>

namespace game::gameplay {

/// The movement contract shared by the two drivers of a battle: the player's input (`FGridMoveSystem`) and a
/// monster's behaviour rule (`FEnemyTurnSystem`). why a rule and not a system: legality (move AP, bounds, one
/// unit per cell) and the rejection narration must exist **once** — a second copy for the AI would eventually
/// let a monster step onto an occupied cell while the player could not (R12).
///
/// `invariant:` every position change in a battle goes through `applyStep`. A caller that writes
/// `FGridPosition` itself bypasses both the AP accounting and the rejection narration.
///
/// why cardinal steps only, while distance is Chebyshev (design §4: 사거리·이동은 칸 단위): Chebyshev says how
/// far away something is, it does not grant diagonal steps. The player moves cardinally, so the AI must too —
/// otherwise the same cell would be reachable in one turn for one side and two for the other.

/// One cell toward `target`, cardinal only. Horizontal wins a tie so the choice is total (R19: never depends on
/// iteration order). Nullopt when the two cells are the same cell.
[[nodiscard]] inline std::optional<FGridPosition> stepToward(const FGridPosition& from, const FGridPosition& target) {
    const int deltaX = target.x - from.x;
    const int deltaY = target.y - from.y;
    if (deltaX == 0 && deltaY == 0) {
        return std::nullopt;
    }

    if (std::abs(deltaY) > std::abs(deltaX)) {
        return FGridPosition{from.x, from.y + (deltaY > 0 ? 1 : -1)};
    }
    return FGridPosition{from.x + (deltaX > 0 ? 1 : -1), from.y};
}

/// One cell away from `target`: the opposite of `stepToward` (미결: 도주는 장애물 회피를 하지 않는다).
[[nodiscard]] inline std::optional<FGridPosition> stepAway(const FGridPosition& from, const FGridPosition& target) {
    const std::optional<FGridPosition> toward = stepToward(from, target);
    if (!toward) {
        return std::nullopt;
    }
    return FGridPosition{from.x - (toward->x - from.x), from.y - (toward->y - from.y)};
}

/// Moves `unit` to (x, y) if the movement contract allows it, and narrates every refusal (R19).
/// False means nothing changed and the reason is already logged + queued as `FMoveRejectedEvent`.
[[nodiscard]] inline bool applyStep(entt::registry& registry,
                                   entt::entity unit,
                                   int x,
                                   int y,
                                   FBattleState& state,
                                   FEventBus* bus) {
    auto reject = [bus, unit](std::string reason) {
        LOG_WARN("move rejected: {}", reason);
        if (bus != nullptr) {
            bus->queueFrame<FMoveRejectedEvent>(FMoveRejectedEvent{unit, std::move(reason)});
        }
        return false;
    };

    FGridPosition* cell = registry.try_get<FGridPosition>(unit);
    FApPool* pool = registry.try_get<FApPool>(unit);
    if (cell == nullptr || pool == nullptr) {
        LOG_ERROR("move: unit entity {} lacks FGridPosition or FApPool.", static_cast<int>(unit));
        return false;
    }

    if (pool->move <= 0) {
        return reject("이동 AP가 없다 (턴당 이동은 design §4 잠정 1회)");
    }

    const FGridPosition destination{x, y};
    if (!state.inBounds(destination)) {
        return reject(fmt::format("그리드 밖이다 ({}, {})", destination.x, destination.y));
    }
    // 미결(design §4): 쓰러진 유닛이 칸을 막는지 — FGridOccupancy가 그 결정을 한 곳에서 들고 있다.
    if (gridCellOccupied(registry, destination.x, destination.y)) {
        return reject(fmt::format("({}, {}) 칸에 다른 유닛이 있다", destination.x, destination.y));
    }

    *cell = destination;
    pool->move -= 1;
    LOG_INFO("move: entity {} → ({}, {}) (move AP left {}).", static_cast<int>(unit), cell->x, cell->y, pool->move);
    if (bus != nullptr) {
        bus->queueFrame<FUnitMovedEvent>(FUnitMovedEvent{unit, cell->x, cell->y});
    }
    return true;
}

} // namespace game::gameplay
