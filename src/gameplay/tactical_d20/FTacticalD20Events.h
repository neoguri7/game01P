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

struct FTacticalD20ActionResolvedEvent {
    entt::entity unit{entt::null};
    std::string actionId{"wait"};
    int remainingMovementTiles{0};
    bool turnComplete{false};
};

struct FTacticalD20AttackResolvedEvent {
    entt::entity attacker{entt::null};
    entt::entity target{entt::null};
    std::string weaponId;
    int d20{0};
    int total{0};
    int effectiveArmorClass{0};
    bool hit{false};
    bool criticalHit{false};
    bool coverApplied{false};
    bool disadvantageApplied{false};
};

struct FTacticalD20AttackRollResolvedEvent {
    entt::entity attacker{entt::null};
    entt::entity target{entt::null};
    std::string weaponId;
    int naturalRoll{0};
    int total{0};
    int targetNumber{0};
    bool hit{false};
    bool criticalHit{false};
    std::string breakdown;
};

struct FTacticalD20DamageAppliedEvent {
    entt::entity source{entt::null};
    entt::entity target{entt::null};
    std::string damageType{"weapon"};
    int damage{0};
    int hpBefore{0};
    int hpAfter{0};
    bool defeated{false};
};

struct FTacticalD20ConditionChangedEvent {
    entt::entity unit{entt::null};
    std::string conditionId;
    std::string change;
    int remainingRounds{0};
};

struct FTacticalD20ConditionAppliedEvent {
    entt::entity unit{entt::null};
    std::string conditionId;
    int remainingRounds{0};
};

struct FTacticalD20ConditionTickedEvent {
    entt::entity unit{entt::null};
    std::string conditionId;
    int remainingRounds{0};
};

struct FTacticalD20ConditionExpiredEvent {
    entt::entity unit{entt::null};
    std::string conditionId;
};

} // namespace game
