#pragma once

#include <entt/entt.hpp>

namespace game {

enum class ETacticalCommandAction {
    Unknown,
    Move,
    Attack,
    Dash,
    Dodge,
    EndTurn
};

const char* TacticalCommandActionName(ETacticalCommandAction action);

struct FQueuedTacticalCommand {
    ETacticalCommandAction action{ETacticalCommandAction::Unknown};
    int targetTileX{0};
    int targetTileY{0};
    bool hasTargetTile{false};
    entt::entity targetEntity{entt::null};
    bool endTurnAfterResolution{false};
};

} // namespace game
