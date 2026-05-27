#pragma once

#include <entt/entt.hpp>
#include <string>

namespace game {

struct FTacticalD20CombatSetupRequestedEvent {};

struct FTacticalD20CombatSetupCompletedEvent {
    int gridWidth{0};
    int gridHeight{0};
    int unitCount{0};
};

struct FTacticalD20CombatStateChangedEvent {
    const char* previousState{"CombatSetup"};
    const char* nextState{"InitiativeRolling"};
};

struct FTacticalD20CommandQueuedEvent {
    entt::entity unit{entt::null};
    std::string actionId{"wait"};
    int movementSpentTiles{0};
    bool hasTargetTile{false};
    int targetTileX{0};
    int targetTileY{0};
    entt::entity targetEntity{entt::null};
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
    bool valid{false};
    std::string invalidReason;
    int movementCostTiles{0};
};

struct FTacticalD20ActionResolvedEvent {
    entt::entity unit{entt::null};
    std::string actionId{"wait"};
    int remainingMovementTiles{0};
    bool turnComplete{false};
};

} // namespace game
