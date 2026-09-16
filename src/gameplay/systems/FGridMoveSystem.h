#pragma once

#include "ecs/systems/ISystem.h"

#include "core/InputState.h"
#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FApPool.h"
#include "gameplay/components/FGridPosition.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/rules/FGridOccupancy.h"
#include "gameplay/rules/FTurnActor.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

#include <string>
#include <utility>

namespace game::gameplay {

/// Moves the active player unit one cell per press, spending 이동 AP (design §4 확정: 이동 AP는 이동 전용,
/// 서로 전환 불가; 사거리·이동은 칸 단위). 모든 거부는 이유를 남긴다 — 조작이 안 먹은 이유를 플레이어가
/// 알아야 한다 (R19).
struct FGridMoveSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FGridMove");

        const FInputState* input = registry.ctx().find<FInputState>();
        FBattleState* state = registry.ctx().find<FBattleState>();
        if (input == nullptr || state == nullptr) {
            return;
        }

        const entt::entity unit = activePlayerUnit(registry);
        if (unit == entt::null) {
            return;
        }

        FGridPosition step{};
        if (input->isActionPressed(EInputAction::MoveUp)) {
            step.y -= 1;
        } else if (input->isActionPressed(EInputAction::MoveDown)) {
            step.y += 1;
        } else if (input->isActionPressed(EInputAction::MoveLeft)) {
            step.x -= 1;
        } else if (input->isActionPressed(EInputAction::MoveRight)) {
            step.x += 1;
        } else {
            return;
        }

        FGridPosition* cell = registry.try_get<FGridPosition>(unit);
        FApPool* pool = registry.try_get<FApPool>(unit);
        if (cell == nullptr || pool == nullptr) {
            LOG_ERROR("move: active unit entity {} lacks FGridPosition or FApPool.", static_cast<int>(unit));
            return;
        }

        FEventBus* bus = registry.ctx().find<FEventBus>();
        if (pool->move <= 0) {
            reject(bus, unit, "이동 AP가 없다 (턴당 이동은 design §4 잠정 1회)");
            return;
        }

        const FGridPosition destination{cell->x + step.x, cell->y + step.y};
        if (!state->inBounds(destination)) {
            reject(bus, unit, fmt::format("그리드 밖이다 ({}, {})", destination.x, destination.y));
            return;
        }
        // 미결(design §4): 쓰러진 유닛이 칸을 막는지 — FGridOccupancy가 그 결정을 한 곳에서 들고 있다.
        if (gridCellOccupied(registry, destination.x, destination.y)) {
            reject(bus, unit, fmt::format("({}, {}) 칸에 다른 유닛이 있다", destination.x, destination.y));
            return;
        }

        *cell = destination;
        pool->move -= 1;
        LOG_INFO("move: entity {} → ({}, {}) (move AP left {}).", static_cast<int>(unit), cell->x, cell->y, pool->move);
        if (bus != nullptr) {
            bus->queueFrame<FUnitMovedEvent>(FUnitMovedEvent{unit, cell->x, cell->y});
        }
    }

    [[nodiscard]] std::string name() const override { return "FGridMove"; }

private:
    static void reject(FEventBus* bus, entt::entity unit, std::string reason) {
        LOG_WARN("move rejected: {}", reason);
        if (bus != nullptr) {
            bus->queueFrame<FMoveRejectedEvent>(FMoveRejectedEvent{unit, std::move(reason)});
        }
    }
};

} // namespace game::gameplay
