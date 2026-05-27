#pragma once

#include "ecs/components/FAbilityScores.h"
#include <string>
#include <vector>

namespace game {

struct FTacticalD20TileConfig {
    int x{0};
    int y{0};
};

struct FTacticalD20UnitConfig {
    std::string id;
    std::string team;
    std::string displayName;
    int startTileX{0};
    int startTileY{0};
    int maxHp{1};
    int armorClass{10};
    int speedFeet{30};
    FAbilityScores abilities{};
    std::vector<std::string> weaponProficiencies;
    std::vector<std::string> savingThrowProficiencies;
    std::vector<std::string> skillProficiencies;
    std::vector<std::string> actions;
};

struct FTacticalD20WeaponConfig {
    std::string id;
    std::string displayName;
    std::string attackType;
    std::string ability;
    int rangeTiles{1};
    std::string damageDice;
    std::string damageAbility;
};

struct FTacticalD20ActionConfig {
    int dashExtraMovementMultiplier{1};
    std::string dodgeIncomingAttackMode{"disadvantage"};
    bool dodgeExpiresAtStartOfNextTurn{true};
    bool waitEndsTurn{true};
};

struct FTacticalD20ConditionConfig {
    std::string burningDamageDice{"1d4"};
    int burningDurationRounds{2};
    std::string poisonedAttackRollMode{"disadvantage"};
    std::string poisonedAbilityCheckMode{"disadvantage"};
    bool stunnedSkipMove{true};
    bool stunnedSkipAction{true};
    int stunnedDurationTurns{1};
};

struct FTacticalD20EnemyAiConfig {
    std::string type{"nearestTargetMeleeFirst"};
    bool attackIfInRange{true};
    bool moveTowardTargetIfOutOfRange{true};
    bool noAttackAfterMoveInFirstPrototype{true};
    float enemyThinkDelaySeconds{0.4f};
};

struct FTacticalD20LoggingConfig {
    int combatLogMaxLines{12};
    int eventLogMaxLines{32};
    int stateLogMaxLines{32};
    bool consoleLogEnabled{true};
    bool imguiLogEnabled{true};
    bool tracyZonesEnabled{true};
};

struct FTacticalD20Config {
    int gridWidth{8};
    int gridHeight{6};
    int tileFeet{5};
    int proficiencyBonus{2};
    bool showEngineTelemetry{true};
    bool showCombatLog{true};
    bool showEventLog{true};
    bool showStateLog{true};
    std::string initiativeTieBreaker{"dexterityThenPlayer"};
    std::vector<FTacticalD20UnitConfig> units;
    std::vector<FTacticalD20WeaponConfig> weapons;
    FTacticalD20ActionConfig actions{};
    FTacticalD20ConditionConfig conditions{};
    std::vector<FTacticalD20TileConfig> walls;
    std::vector<FTacticalD20TileConfig> coverTiles;
    int coverAcBonus{2};
    FTacticalD20EnemyAiConfig enemyAi{};
    FTacticalD20LoggingConfig logging{};
    std::vector<std::string> warnings;
};

} // namespace game
