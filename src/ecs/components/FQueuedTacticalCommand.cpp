#include "ecs/components/FQueuedTacticalCommand.h"

namespace game {

const char* TacticalCommandActionName(ETacticalCommandAction action) {
    switch (action) {
        case ETacticalCommandAction::Move: return "move";
        case ETacticalCommandAction::Attack: return "attack";
        case ETacticalCommandAction::Dash: return "dash";
        case ETacticalCommandAction::Dodge: return "dodge";
        case ETacticalCommandAction::EndTurn: return "end_turn";
        case ETacticalCommandAction::Unknown: return "unknown";
    }
    return "unknown";
}

} // namespace game
