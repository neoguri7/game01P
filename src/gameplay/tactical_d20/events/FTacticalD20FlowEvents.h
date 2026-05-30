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
    const char* machine{"CombatState"};
    const char* previousState{"CombatSetup"};
    const char* nextState{"InitiativeRolling"};
};

struct FTacticalD20InitiativeRollResolvedEvent {
    entt::entity unit{entt::null};
    std::string unitId;
    int naturalRoll{0};
    int dexterityModifier{0};
    int total{0};
    std::string breakdown;
};

struct FTacticalD20RoundStartedEvent {
    int round{0};
};

struct FTacticalD20TurnStartedEvent {
    entt::entity unit{entt::null};
    std::string unitId;
    int round{0};
};

struct FTacticalD20ActiveUnitChangedEvent {
    entt::entity previousUnit{entt::null};
    entt::entity nextUnit{entt::null};
    std::string nextUnitId;
};

struct FTacticalD20TurnEndedEvent {
    entt::entity unit{entt::null};
    std::string unitId;
    int round{0};
};

} // namespace game
