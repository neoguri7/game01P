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
#include "gameplay/tactical_d20/FTacticalD20LogUtils.h"
#include "gameplay/tactical_d20/FTacticalD20TurnPanelHitZone.h"

#include <fmt/format.h>
#include <glm/vec2.hpp>
#include <string>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

bool ContainsPoint(const FPosition& position, const FCollider& collider, const glm::vec2& point) {
    const float centerX = position.x + collider.offset.x;
    const float centerY = position.y + collider.offset.y;
    return point.x >= centerX - collider.halfWidth && point.x <= centerX + collider.halfWidth
        && point.y >= centerY - collider.halfHeight && point.y <= centerY + collider.halfHeight;
}

const char* DragPhaseName(ETacticalD20CommandDragPhase phase) {
    switch (phase) {
        case ETacticalD20CommandDragPhase::DragIdle: return "DragIdle";
        case ETacticalD20CommandDragPhase::DraggingCommand: return "DraggingCommand";
        case ETacticalD20CommandDragPhase::DropCandidate: return "DropCandidate";
        case ETacticalD20CommandDragPhase::DragRejected: return "DragRejected";
        case ETacticalD20CommandDragPhase::CommandAccepted: return "CommandAccepted";
    }
    return "DragIdle";
}

void PublishDragStateChanged(entt::registry& registry,
                             entt::entity token,
                             const std::string& commandId,
                             ETacticalD20CommandDragPhase previous,
                             ETacticalD20CommandDragPhase next,
                             const std::string& reason = "") {
    const FTacticalD20CommandDragStateChangedEvent event{
        .token = token,
        .commandId = commandId,
        .previousState = DragPhaseName(previous),
        .nextState = DragPhaseName(next),
        .reason = reason,
    };
    PUBLISH(FTacticalD20CommandDragStateChangedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20CommandDragStateChangedEvent, registry, event);
    AppendTacticalD20StateLog(registry, fmt::format("[DragState] {} -> {}", event.previousState, event.nextState));
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
        || (active != entt::null
            && registry.all_of<FQueuedTacticalD20Command>(active)
            && registry.get<FQueuedTacticalD20Command>(active).validationApproved);
}

bool HasActiveDrag(entt::registry& registry) {
    return !registry.view<FTacticalD20CommandDragState>().empty();
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
}

void PublishDropRequest(entt::registry& registry,
                        entt::entity token,
                        entt::entity active,
                        const std::string& commandId,
                        entt::entity tileEntity,
                        entt::entity targetEntity,
                        bool targetsTurnPanel) {
    FTacticalD20CommandDropRequestedEvent event;
    event.token = token;
    event.unit = active;
    event.commandId = commandId;
    event.targetEntity = targetEntity;
    event.targetsTurnPanel = targetsTurnPanel;
    event.hasTargetTile = tileEntity != entt::null;
    if (event.hasTargetTile) {
        const auto& tile = registry.get<FTacticalBoardTile>(tileEntity);
        event.targetTileX = tile.tileX;
        event.targetTileY = tile.tileY;
    }

    PUBLISH(FTacticalD20CommandDropRequestedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20CommandDropRequestedEvent, registry, event);
}

void PublishAcceptedCommand(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    const FTacticalD20CommandAcceptedEvent accepted{
        .token = event.token,
        .unit = event.unit,
        .commandId = event.commandId,
        .movementSpentTiles = event.movementCostTiles,
        .hasTargetTile = event.hasTargetTile,
        .targetTileX = event.targetTileX,
        .targetTileY = event.targetTileY,
        .targetEntity = event.targetEntity,
        .targetsTurnPanel = event.targetsTurnPanel,
    };
    PUBLISH(FTacticalD20CommandAcceptedEvent, registry, accepted);
    QUEUE_FRAME_EVENT(FTacticalD20CommandAcceptedEvent, registry, accepted);
}

void ConsumeValidationEvents(entt::registry& registry) {
    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return;

    for (const auto& event : bus->frameEvents<FTacticalD20CommandDropValidatedEvent>()) {
        if (event.token == entt::null || !registry.valid(event.token) || !registry.all_of<FTacticalD20CommandDragState>(event.token)) continue;

        // Drag state transition table:
        //   DropCandidate + valid CommandDropValidated   -> CommandAccepted -> DragIdle
        //   DropCandidate + invalid CommandDropValidated -> DragRejected    -> DragIdle
        auto drag = registry.get<FTacticalD20CommandDragState>(event.token);
        const auto terminalPhase = event.valid ? ETacticalD20CommandDragPhase::CommandAccepted : ETacticalD20CommandDragPhase::DragRejected;
        PublishDragStateChanged(registry, event.token, drag.commandId, drag.phase, terminalPhase, event.invalidReason);
        SnapBack(registry, event.token, drag);
        AppendTacticalD20CommandValidationLogs(registry, event);
        if (event.valid) PublishAcceptedCommand(registry, event);
        PublishDragStateChanged(registry, event.token, drag.commandId, terminalPhase, ETacticalD20CommandDragPhase::DragIdle, event.invalidReason);
        registry.remove<FTacticalD20CommandDragState>(event.token);
    }
}

void BeginDrag(entt::registry& registry, const FInputState& input, entt::entity active) {
    const auto token = TopCommandTokenAt(registry, input.mousePos);
    if (token == entt::null) return;

    const auto& position = registry.get<FPosition>(token);
    const auto& command = registry.get<FCommandToken>(token);
    // Drag state transition table:
    //   DragIdle + mouse pressed on command token during player turn -> DraggingCommand
    registry.emplace_or_replace<FTacticalD20CommandDragState>(
        token,
        command.id,
        position.x,
        position.y,
        input.mousePos.x - position.x,
        input.mousePos.y - position.y,
        ETacticalD20CommandDragPhase::DraggingCommand,
        "");
    PublishDragStateChanged(registry, token, command.id, ETacticalD20CommandDragPhase::DragIdle, ETacticalD20CommandDragPhase::DraggingCommand);
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
        const bool targetsTurnPanel = IsFallbackTacticalD20TurnPanelHit(registry, input.mousePos);
        if (tileEntity == entt::null && targetEntity == entt::null && !targetsTurnPanel) {
            constexpr const char* reason = "drop outside board or valid target";
            // Drag state transition table:
            //   DraggingCommand + mouse released outside board/target -> DragRejected -> DragIdle
            PublishDragStateChanged(registry, token, drag.commandId, drag.phase, ETacticalD20CommandDragPhase::DragRejected, reason);
            SnapBack(registry, token, drag);
            AppendTacticalD20EventLog(registry, fmt::format("[Event] CommandDropValidated command={} target=none result=invalid: {}", drag.commandId, reason));
            AppendTacticalD20CombatLog(registry, fmt::format("{} command invalid: {}", drag.commandId, reason));
            PublishDragStateChanged(registry, token, drag.commandId, ETacticalD20CommandDragPhase::DragRejected, ETacticalD20CommandDragPhase::DragIdle, reason);
            registry.remove<FTacticalD20CommandDragState>(token);
            return;
        }

        PublishDropRequest(registry, token, active, drag.commandId, tileEntity, targetEntity, targetsTurnPanel);

        // Drag state transition table:
        //   DraggingCommand + mouse released -> DropCandidate
        PublishDragStateChanged(registry, token, drag.commandId, drag.phase, ETacticalD20CommandDragPhase::DropCandidate);
        drag.phase = ETacticalD20CommandDragPhase::DropCandidate;
        return;
    }
}

} // namespace

void TacticalD20CommandDragInputSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20CommandDragInputSystem");

    const auto* input = registry.ctx().find<FInputState>();
    if (!input) return;

    ConsumeValidationEvents(registry);
    UpdateBoardHover(registry, input->mousePos);

    const auto active = ActiveUnit(registry);
    UpdateActiveDrag(registry, *input, active);

    if (input->uiCapturesMouse || !input->mouseLeftPressed || active == entt::null) return;
    if (!IsAwaitingPlayerCommand(registry) || IsCombatEnded(registry) || IsInputLocked(registry, active) || HasActiveDrag(registry)) return;

    BeginDrag(registry, *input, active);
}

} // namespace game
