#include "gameplay/tactical_d20/FTacticalD20CommandDragLog.h"

#include "core/events/FEventBus.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20LogUtils.h"

#include <fmt/format.h>

namespace game {
namespace {

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

} // namespace

void PublishTacticalD20DragStateChanged(entt::registry& registry,
                                        entt::entity token,
                                        const std::string& commandId,
                                        ETacticalD20CommandDragPhase previous,
                                        ETacticalD20CommandDragPhase next,
                                        const std::string& reason) {
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

} // namespace game
