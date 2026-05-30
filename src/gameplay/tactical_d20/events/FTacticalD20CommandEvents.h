#pragma once

#include <entt/entt.hpp>
#include <string>

namespace game {

struct FTacticalD20CommandDragStartedEvent {
    entt::entity token{entt::null};
    entt::entity unit{entt::null};
    std::string commandId;
};

struct FTacticalD20CommandSelectedEvent {
    entt::entity token{entt::null};
    entt::entity unit{entt::null};
    std::string commandId;
};

struct FTacticalD20CommandQueuedEvent {
    entt::entity unit{entt::null};
    std::string actionId{"wait"};
    int movementSpentTiles{0};
    bool hasTargetTile{false};
    int targetTileX{0};
    int targetTileY{0};
    entt::entity targetEntity{entt::null};
    bool validationApproved{false};
    bool endTurnAfterResolution{false};
};

struct FTacticalD20CommandDropRequestedEvent {
    entt::entity token{entt::null};
    entt::entity unit{entt::null};
    std::string commandId{"wait"};
    bool hasTargetTile{false};
    int targetTileX{0};
    int targetTileY{0};
    entt::entity targetEntity{entt::null};
    bool targetsTurnPanel{false};
    bool endTurnAfterResolution{false};
};

struct FTacticalD20MovementPathValidatedEvent {
    entt::entity unit{entt::null};
    std::string commandId{"move"};
    bool hasTargetTile{false};
    int targetTileX{0};
    int targetTileY{0};
    bool valid{false};
    std::string invalidReason;
    int movementCostTiles{0};
    int movementBudgetTiles{0};
};

struct FTacticalD20CommandDropValidatedEvent {
    entt::entity token{entt::null};
    entt::entity unit{entt::null};
    std::string commandId{"wait"};
    bool hasTargetTile{false};
    int targetTileX{0};
    int targetTileY{0};
    entt::entity targetEntity{entt::null};
    bool targetsTurnPanel{false};
    bool valid{false};
    std::string invalidReason;
    int movementCostTiles{0};
};

struct FTacticalD20CommandAcceptedEvent {
    entt::entity token{entt::null};
    entt::entity unit{entt::null};
    std::string commandId{"wait"};
    int movementSpentTiles{0};
    bool hasTargetTile{false};
    int targetTileX{0};
    int targetTileY{0};
    entt::entity targetEntity{entt::null};
    bool targetsTurnPanel{false};
    bool endTurnAfterResolution{false};
};

struct FTacticalD20CommandDragStateChangedEvent {
    entt::entity token{entt::null};
    std::string commandId;
    const char* previousState{"DragIdle"};
    const char* nextState{"DraggingCommand"};
    std::string reason;
};

} // namespace game
