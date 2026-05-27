#include "gameplay/tactical_d20/TacticalD20CommandDragInputSystem.h"

#include "core/InputState.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FCombatStateDefeat.h"
#include "ecs/components/FCombatStateResolvingAction.h"
#include "ecs/components/FCombatStateVictory.h"
#include "ecs/components/FCommandToken.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FQueuedTacticalD20Command.h"
#include "ecs/components/FTacticalBoardTile.h"
#include "ecs/components/FTacticalD20CommandDragState.h"
#include "ecs/components/FTacticalUnit.h"
#include "gameplay/tactical_d20/FTacticalD20BoardInteraction.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"

#include <glm/vec2.hpp>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

bool ContainsPoint(const FPosition& position, const FCollider& collider, const glm::vec2& point) {
    const float centerX = position.x + collider.offset.x;
    const float centerY = position.y + collider.offset.y;
    return point.x >= centerX - collider.halfWidth && point.x <= centerX + collider.halfWidth
        && point.y >= centerY - collider.halfHeight && point.y <= centerY + collider.halfHeight;
}

entt::entity ActiveUnit(entt::registry& registry) {
    auto view = registry.view<FActiveTacticalUnit, FTacticalUnit>();
    for (auto entity : view) return entity;
    return entt::null;
}

bool IsAwaitingPlayerCommand(entt::registry& registry) {
    return !registry.view<FCombatStateAwaitingCommand>().empty();
}

bool IsCombatEnded(entt::registry& registry) {
    return !registry.view<FCombatStateVictory>().empty() || !registry.view<FCombatStateDefeat>().empty();
}

bool IsInputLocked(entt::registry& registry, entt::entity active) {
    return !registry.view<FCombatStateResolvingAction>().empty()
        || (active != entt::null && registry.all_of<FQueuedTacticalD20Command>(active));
}

entt::entity TopCommandTokenAt(entt::registry& registry, const glm::vec2& point) {
    entt::entity best = entt::null;
    int bestLayer = -1;
    auto view = registry.view<FCommandToken, FPosition, FCollider>();
    for (auto entity : view) {
        const auto& position = view.get<FPosition>(entity);
        const auto& collider = view.get<FCollider>(entity);
        if (!ContainsPoint(position, collider, point)) continue;

        const int layer = registry.all_of<FLayer>(entity) ? registry.get<FLayer>(entity).depth : collider.collisionLayer;
        if (layer >= bestLayer) {
            best = entity;
            bestLayer = layer;
        }
    }
    return best;
}

entt::entity BoardTileAt(entt::registry& registry, const glm::vec2& point) {
    auto view = registry.view<FTacticalBoardTile, FPosition, FCollider>();
    for (auto entity : view) {
        if (ContainsPoint(view.get<FPosition>(entity), view.get<FCollider>(entity), point)) return entity;
    }
    return entt::null;
}

entt::entity UnitAt(entt::registry& registry, const glm::vec2& point) {
    auto view = registry.view<FTacticalUnit, FPosition, FCollider>();
    for (auto entity : view) {
        if (ContainsPoint(view.get<FPosition>(entity), view.get<FCollider>(entity), point)) return entity;
    }
    return entt::null;
}

void UpdateBoardHover(entt::registry& registry, const glm::vec2& point) {
    auto* interaction = registry.ctx().find<FTacticalD20BoardInteraction>();
    if (!interaction) return;

    const auto tileEntity = BoardTileAt(registry, point);
    interaction->hasHoveredTile = tileEntity != entt::null;
    if (tileEntity == entt::null) return;

    const auto& tile = registry.get<FTacticalBoardTile>(tileEntity);
    interaction->hoveredTileX = tile.tileX;
    interaction->hoveredTileY = tile.tileY;
}

void SnapBack(entt::registry& registry, entt::entity token, const FTacticalD20CommandDragState& drag) {
    if (registry.valid(token) && registry.all_of<FPosition>(token)) {
        auto& position = registry.get<FPosition>(token);
        position.x = drag.originX;
        position.y = drag.originY;
    }
    if (registry.valid(token) && registry.all_of<FTacticalD20CommandDragState>(token)) {
        registry.remove<FTacticalD20CommandDragState>(token);
    }
}

void PublishDropRequest(entt::registry& registry,
                        entt::entity token,
                        entt::entity active,
                        const std::string& commandId,
                        entt::entity tileEntity,
                        entt::entity targetEntity) {
    FTacticalD20CommandDropRequestedEvent event;
    event.token = token;
    event.unit = active;
    event.commandId = commandId;
    event.targetEntity = targetEntity;
    event.hasTargetTile = tileEntity != entt::null;
    if (event.hasTargetTile) {
        const auto& tile = registry.get<FTacticalBoardTile>(tileEntity);
        event.targetTileX = tile.tileX;
        event.targetTileY = tile.tileY;
    }

    PUBLISH(FTacticalD20CommandDropRequestedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20CommandDropRequestedEvent, registry, event);
}

void BeginDrag(entt::registry& registry, const FInputState& input, entt::entity active) {
    const auto token = TopCommandTokenAt(registry, input.mousePos);
    if (token == entt::null) return;

    const auto& position = registry.get<FPosition>(token);
    const auto& command = registry.get<FCommandToken>(token);
    registry.emplace_or_replace<FTacticalD20CommandDragState>(
        token,
        command.id,
        position.x,
        position.y,
        input.mousePos.x - position.x,
        input.mousePos.y - position.y,
        ETacticalD20CommandDragPhase::DraggingCommand,
        "");
    (void)active;
}

void UpdateActiveDrag(entt::registry& registry, const FInputState& input, entt::entity active) {
    auto view = registry.view<FTacticalD20CommandDragState, FPosition, FCommandToken>();
    for (auto token : view) {
        auto& drag = view.get<FTacticalD20CommandDragState>(token);
        auto& position = view.get<FPosition>(token);

        if (drag.phase == ETacticalD20CommandDragPhase::DropCandidate) return;

        if (input.mouseLeftHeld) {
            drag.phase = ETacticalD20CommandDragPhase::DraggingCommand;
            position.x = input.mousePos.x - drag.cursorOffsetX;
            position.y = input.mousePos.y - drag.cursorOffsetY;
            return;
        }

        const auto tileEntity = BoardTileAt(registry, input.mousePos);
        const auto targetEntity = UnitAt(registry, input.mousePos);
        PublishDropRequest(registry, token, active, drag.commandId, tileEntity, targetEntity);

        if (tileEntity == entt::null && targetEntity == entt::null) {
            drag.phase = ETacticalD20CommandDragPhase::DragRejected;
            drag.invalidReason = "drop outside board or valid target";
            SnapBack(registry, token, drag);
            return;
        }

        drag.phase = ETacticalD20CommandDragPhase::DropCandidate;
        return;
    }
}

} // namespace

void TacticalD20CommandDragInputSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20CommandDragInputSystem");

    const auto* input = registry.ctx().find<FInputState>();
    if (!input) return;

    UpdateBoardHover(registry, input->mousePos);

    const auto active = ActiveUnit(registry);
    UpdateActiveDrag(registry, *input, active);

    if (input->uiCapturesMouse || !input->mouseLeftPressed || active == entt::null) return;
    if (!IsAwaitingPlayerCommand(registry) || IsCombatEnded(registry) || IsInputLocked(registry, active)) return;

    BeginDrag(registry, *input, active);
}

} // namespace game
