#pragma once

#include <string>

namespace game {

enum class ETacticalD20CommandDragPhase {
    // DragIdle is represented at runtime by absence of FTacticalD20CommandDragState.
    DragIdle,
    DraggingCommand,
    DropCandidate,
    DragRejected,
    CommandAccepted
};

struct FTacticalD20CommandDragState {
    std::string commandId;
    float originX{0.f};
    float originY{0.f};
    float cursorOffsetX{0.f};
    float cursorOffsetY{0.f};
    ETacticalD20CommandDragPhase phase{ETacticalD20CommandDragPhase::DragIdle};
    std::string invalidReason;
};

} // namespace game
