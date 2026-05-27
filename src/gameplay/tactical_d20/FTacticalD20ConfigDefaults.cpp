#include "gameplay/tactical_d20/FTacticalD20ConfigDefaults.h"

namespace game {
namespace {

FTacticalD20UnitConfig MakePlayerFallback() {
    return FTacticalD20UnitConfig{
        .id = "player_warrior",
        .team = "player",
        .displayName = "Warrior",
        .startTileX = 1,
        .startTileY = 2,
        .maxHp = 24,
        .armorClass = 15,
        .speedFeet = 30,
        .abilities = FAbilityScores{16, 12, 14, 10, 12, 10},
        .weaponProficiencies = {"longsword"},
        .savingThrowProficiencies = {"strength", "constitution"},
        .skillProficiencies = {},
        .actions = {"move", "attack", "dash", "dodge", "wait"},
    };
}

FTacticalD20UnitConfig MakeEnemyFallback() {
    return FTacticalD20UnitConfig{
        .id = "enemy_goblin",
        .team = "enemy",
        .displayName = "Goblin",
        .startTileX = 6,
        .startTileY = 2,
        .maxHp = 14,
        .armorClass = 13,
        .speedFeet = 30,
        .abilities = FAbilityScores{12, 14, 10, 10, 10, 8},
        .weaponProficiencies = {"scimitar", "shortbow"},
        .savingThrowProficiencies = {"dexterity"},
        .skillProficiencies = {},
        .actions = {"move", "attack", "dash", "dodge", "wait"},
    };
}

} // namespace

FTacticalD20Config MakeTacticalD20FallbackConfig() {
    FTacticalD20Config config;
    config.units = {MakePlayerFallback(), MakeEnemyFallback()};
    config.weapons = {
        {"longsword", "Longsword", "melee", "strength", 1, "1d8", "strength"},
        {"scimitar", "Scimitar", "melee", "dexterity", 1, "1d6", "dexterity"},
        {"shortbow", "Shortbow", "ranged", "dexterity", 5, "1d6", "dexterity"},
    };
    config.walls = {{4, 1}, {4, 2}, {4, 3}};
    config.coverTiles = {{3, 2}, {5, 2}};
    return config;
}

} // namespace game