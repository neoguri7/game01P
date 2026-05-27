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
};

struct FTacticalD20ActionResolvedEvent {
    entt::entity unit{entt::null};
    std::string actionId{"wait"};
    int remainingMovementTiles{0};
    bool turnComplete{false};
};

} // namespace game