#pragma once

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

} // namespace game