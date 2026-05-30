#pragma once

#include "ecs/components/FAbilityScores.h"
#include "ecs/components/FTacticalUnit.h"

#include <entt/entt.hpp>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace game {

enum class ETacticalD20Ability {
    Strength,
    Dexterity,
    Constitution,
    Intelligence,
    Wisdom,
    Charisma,
};

enum class ETacticalD20RollMode {
    Normal,
    Advantage,
    Disadvantage,
};

struct FTacticalD20RollRequest {
    ETacticalD20RollMode mode{ETacticalD20RollMode::Normal};
    bool hasAdvantage{false};
    bool hasDisadvantage{false};
    int abilityModifier{0};
    int proficiencyBonus{0};
    int situationalBonus{0};
    int targetNumber{0};
};

struct FTacticalD20RollResult {
    int naturalRoll{0};
    int secondaryNaturalRoll{0};
    int selectedRoll{0};
    int modifier{0};
    int proficiency{0};
    int situationalBonus{0};
    int total{0};
    bool success{false};
    bool advantageDisadvantageCanceled{false};
    ETacticalD20RollMode mode{ETacticalD20RollMode::Normal};
    std::string breakdown;
};

struct FTacticalD20SavingThrowRequest {
    ETacticalD20Ability ability{ETacticalD20Ability::Dexterity};
    int difficultyClass{10};
    ETacticalD20RollMode mode{ETacticalD20RollMode::Normal};
    int situationalBonus{0};
};

struct FTacticalD20AttackOutcome {
    bool hit{false};
    bool criticalHit{false};
    bool automaticHit{false};
    bool automaticMiss{false};
};

struct FTacticalD20DiceRollResult {
    std::string dice;
    int diceCount{0};
    int dieSides{0};
    std::vector<int> rolls;
    int total{0};
    bool valid{false};
};

struct FTacticalD20DamageResult {
    FTacticalD20DiceRollResult diceRoll;
    int modifier{0};
    int rawDamage{0};
    int finalDamage{0};
    bool criticalHit{false};
    std::string breakdown;
};

struct FTacticalD20DamageApplicationResult {
    int hpBefore{0};
    int hpAfter{0};
    int damageApplied{0};
    bool defeated{false};
};

std::string_view AbilityName(ETacticalD20Ability ability);
ETacticalD20Ability AbilityFromString(std::string_view value, ETacticalD20Ability fallback = ETacticalD20Ability::Strength);
int GetAbilityScore(const FAbilityScores& abilities, ETacticalD20Ability ability);
int ClampAbilityScoreForPrototype(int score);
int AbilityModifierForScore(int score);
int AbilityModifier(const FAbilityScores& abilities, ETacticalD20Ability ability);

ETacticalD20RollMode RollModeFromString(std::string_view value, ETacticalD20RollMode fallback = ETacticalD20RollMode::Normal);
ETacticalD20RollMode ResolveRollMode(ETacticalD20RollMode requestedMode, bool hasAdvantage, bool hasDisadvantage);
bool HasProficiency(const std::vector<std::string>& proficiencies, std::string_view id);
int TacticalD20PrototypeProficiencyBonus(int configuredProficiencyBonus);

FTacticalD20RollResult ResolveD20Roll(const FTacticalD20RollRequest& request, std::mt19937& rng);
FTacticalD20RollResult ResolveSavingThrow(const FTacticalUnit& unit,
    const FAbilityScores& abilities,
    const FTacticalD20SavingThrowRequest& request,
    int configuredProficiencyBonus,
    std::mt19937& rng);

FTacticalD20AttackOutcome ResolveAttackOutcome(const FTacticalD20RollResult& attackRoll, int armorClass);
FTacticalD20DiceRollResult RollDice(std::string_view dice, std::mt19937& rng);
FTacticalD20DamageResult ResolveDamage(std::string_view damageDice, int damageModifier, bool criticalHit, std::mt19937& rng);
FTacticalD20DamageApplicationResult ApplyDamageToUnit(FTacticalUnit& target, int damage);
FTacticalD20DamageApplicationResult ApplyDamageAndDefeat(entt::registry& registry, entt::entity targetEntity, int damage);

} // namespace game