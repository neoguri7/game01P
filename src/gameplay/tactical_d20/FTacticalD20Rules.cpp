#include "gameplay/tactical_d20/FTacticalD20Rules.h"

#include "ecs/components/FUnitStateDefeated.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fmt/format.h>
#include <utility>

namespace game {
namespace {

constexpr int MinPrototypeAbilityScore = 8;
constexpr int MaxPrototypeAbilityScore = 21;
constexpr int FixedPrototypeProficiencyBonus = 2;

std::string ToLower(std::string_view value) {
    std::string lowered(value);
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lowered;
}

int RollDie(int sides, std::mt19937& rng) {
    std::uniform_int_distribution<int> distribution(1, sides);
    return distribution(rng);
}

bool ParsePositiveInt(std::string_view text, int& value) {
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    auto [ptr, error] = std::from_chars(begin, end, value);
    return error == std::errc{} && ptr == end && value > 0;
}

std::string BuildRollBreakdown(const FTacticalD20RollResult& result, int targetNumber) {
    const auto rollText = result.secondaryNaturalRoll > 0
        ? fmt::format("d20 [{}, {}] selected {}", result.naturalRoll, result.secondaryNaturalRoll, result.selectedRoll)
        : fmt::format("d20 {}", result.selectedRoll);
    return fmt::format("{} + modifier {} + proficiency {} + situational {} = {} vs DC {} ({}){}",
        rollText,
        result.modifier,
        result.proficiency,
        result.situationalBonus,
        result.total,
        targetNumber,
        result.success ? "success" : "failure",
        result.advantageDisadvantageCanceled ? "; advantage/disadvantage canceled to normal" : "");
}

std::string BuildDamageBreakdown(const FTacticalD20DamageResult& result) {
    return fmt::format("{}{} total {} + modifier {} = raw {}, final {}",
        result.criticalHit ? "critical " : "",
        result.diceRoll.dice,
        result.diceRoll.total,
        result.modifier,
        result.rawDamage,
        result.finalDamage);
}

} // namespace

std::string_view AbilityName(ETacticalD20Ability ability) {
    switch (ability) {
    case ETacticalD20Ability::Strength: return "strength";
    case ETacticalD20Ability::Dexterity: return "dexterity";
    case ETacticalD20Ability::Constitution: return "constitution";
    case ETacticalD20Ability::Intelligence: return "intelligence";
    case ETacticalD20Ability::Wisdom: return "wisdom";
    case ETacticalD20Ability::Charisma: return "charisma";
    }
    return "strength";
}

ETacticalD20Ability AbilityFromString(std::string_view value, ETacticalD20Ability fallback) {
    const auto lowered = ToLower(value);
    if (lowered == "strength" || lowered == "str") return ETacticalD20Ability::Strength;
    if (lowered == "dexterity" || lowered == "dex") return ETacticalD20Ability::Dexterity;
    if (lowered == "constitution" || lowered == "con") return ETacticalD20Ability::Constitution;
    if (lowered == "intelligence" || lowered == "int") return ETacticalD20Ability::Intelligence;
    if (lowered == "wisdom" || lowered == "wis") return ETacticalD20Ability::Wisdom;
    if (lowered == "charisma" || lowered == "cha") return ETacticalD20Ability::Charisma;
    return fallback;
}

int GetAbilityScore(const FAbilityScores& abilities, ETacticalD20Ability ability) {
    switch (ability) {
    case ETacticalD20Ability::Strength: return abilities.strength;
    case ETacticalD20Ability::Dexterity: return abilities.dexterity;
    case ETacticalD20Ability::Constitution: return abilities.constitution;
    case ETacticalD20Ability::Intelligence: return abilities.intelligence;
    case ETacticalD20Ability::Wisdom: return abilities.wisdom;
    case ETacticalD20Ability::Charisma: return abilities.charisma;
    }
    return abilities.strength;
}

int ClampAbilityScoreForPrototype(int score) {
    return std::clamp(score, MinPrototypeAbilityScore, MaxPrototypeAbilityScore);
}

int AbilityModifierForScore(int score) {
    const int clampedScore = ClampAbilityScoreForPrototype(score);
    return (clampedScore - 10) >= 0 ? (clampedScore - 10) / 2 : (clampedScore - 11) / 2;
}

int AbilityModifier(const FAbilityScores& abilities, ETacticalD20Ability ability) {
    return AbilityModifierForScore(GetAbilityScore(abilities, ability));
}

ETacticalD20RollMode RollModeFromString(std::string_view value, ETacticalD20RollMode fallback) {
    const auto lowered = ToLower(value);
    if (lowered == "normal") return ETacticalD20RollMode::Normal;
    if (lowered == "advantage") return ETacticalD20RollMode::Advantage;
    if (lowered == "disadvantage") return ETacticalD20RollMode::Disadvantage;
    return fallback;
}

ETacticalD20RollMode ResolveRollMode(ETacticalD20RollMode requestedMode, bool hasAdvantage, bool hasDisadvantage) {
    const bool advantageApplies = hasAdvantage || requestedMode == ETacticalD20RollMode::Advantage;
    const bool disadvantageApplies = hasDisadvantage || requestedMode == ETacticalD20RollMode::Disadvantage;
    if (advantageApplies && disadvantageApplies) return ETacticalD20RollMode::Normal;
    if (hasAdvantage) return ETacticalD20RollMode::Advantage;
    if (hasDisadvantage) return ETacticalD20RollMode::Disadvantage;
    return requestedMode;
}

bool HasProficiency(const std::vector<std::string>& proficiencies, std::string_view id) {
    return std::ranges::any_of(proficiencies, [id](const std::string& proficiency) {
        return proficiency == id;
    });
}

int TacticalD20PrototypeProficiencyBonus(int /*configuredProficiencyBonus*/) {
    return FixedPrototypeProficiencyBonus;
}

FTacticalD20RollResult ResolveD20Roll(const FTacticalD20RollRequest& request, std::mt19937& rng) {
    FTacticalD20RollResult result;
    result.advantageDisadvantageCanceled = (request.hasAdvantage || request.mode == ETacticalD20RollMode::Advantage)
        && (request.hasDisadvantage || request.mode == ETacticalD20RollMode::Disadvantage);
    result.mode = ResolveRollMode(request.mode, request.hasAdvantage, request.hasDisadvantage);
    result.naturalRoll = RollDie(20, rng);
    result.secondaryNaturalRoll = result.mode == ETacticalD20RollMode::Normal ? 0 : RollDie(20, rng);

    if (result.mode == ETacticalD20RollMode::Advantage) {
        result.selectedRoll = std::max(result.naturalRoll, result.secondaryNaturalRoll);
    } else if (result.mode == ETacticalD20RollMode::Disadvantage) {
        result.selectedRoll = std::min(result.naturalRoll, result.secondaryNaturalRoll);
    } else {
        result.selectedRoll = result.naturalRoll;
    }

    result.modifier = request.abilityModifier;
    result.proficiency = request.proficiencyBonus;
    result.situationalBonus = request.situationalBonus;
    result.total = result.selectedRoll + result.modifier + result.proficiency + result.situationalBonus;
    result.success = result.total >= request.targetNumber;
    result.breakdown = BuildRollBreakdown(result, request.targetNumber);
    return result;
}

FTacticalD20RollResult ResolveSavingThrow(const FTacticalUnit& unit,
    const FAbilityScores& abilities,
    const FTacticalD20SavingThrowRequest& request,
    int configuredProficiencyBonus,
    std::mt19937& rng) {
    const auto abilityName = AbilityName(request.ability);
    const int proficiency = HasProficiency(unit.savingThrowProficiencies, abilityName)
        ? TacticalD20PrototypeProficiencyBonus(configuredProficiencyBonus)
        : 0;

    return ResolveD20Roll(FTacticalD20RollRequest{
        .mode = request.mode,
        .abilityModifier = AbilityModifier(abilities, request.ability),
        .proficiencyBonus = proficiency,
        .situationalBonus = request.situationalBonus,
        .targetNumber = request.difficultyClass,
    },
        rng);
}

FTacticalD20AttackOutcome ResolveAttackOutcome(const FTacticalD20RollResult& attackRoll, int armorClass) {
    if (attackRoll.selectedRoll == 1) {
        return FTacticalD20AttackOutcome{.automaticMiss = true};
    }
    if (attackRoll.selectedRoll == 20) {
        return FTacticalD20AttackOutcome{.hit = true, .criticalHit = true, .automaticHit = true};
    }
    return FTacticalD20AttackOutcome{.hit = attackRoll.total >= armorClass};
}

FTacticalD20DiceRollResult RollDice(std::string_view dice, std::mt19937& rng) {
    FTacticalD20DiceRollResult result{.dice = std::string(dice)};
    const auto separator = dice.find('d');
    if (separator == std::string_view::npos) return result;

    if (!ParsePositiveInt(dice.substr(0, separator), result.diceCount)) return result;
    if (!ParsePositiveInt(dice.substr(separator + 1), result.dieSides)) return result;

    result.valid = true;
    result.rolls.reserve(static_cast<std::size_t>(result.diceCount));
    for (int i = 0; i < result.diceCount; ++i) {
        const int roll = RollDie(result.dieSides, rng);
        result.rolls.push_back(roll);
        result.total += roll;
    }
    return result;
}

FTacticalD20DamageResult ResolveDamage(std::string_view damageDice, int damageModifier, bool criticalHit, std::mt19937& rng) {
    auto diceRoll = RollDice(damageDice, rng);
    if (criticalHit && diceRoll.valid) {
        const int originalCount = diceRoll.diceCount;
        diceRoll.rolls.reserve(static_cast<std::size_t>(diceRoll.diceCount * 2));
        for (int i = 0; i < originalCount; ++i) {
            const int roll = RollDie(diceRoll.dieSides, rng);
            diceRoll.rolls.push_back(roll);
            diceRoll.total += roll;
        }
        diceRoll.diceCount *= 2;
    }

    FTacticalD20DamageResult result{
        .diceRoll = std::move(diceRoll),
        .modifier = damageModifier,
        .criticalHit = criticalHit,
    };
    result.rawDamage = result.diceRoll.total + damageModifier;
    result.finalDamage = std::max(result.rawDamage, 0);
    result.breakdown = BuildDamageBreakdown(result);
    return result;
}

FTacticalD20DamageApplicationResult ApplyDamageToUnit(FTacticalUnit& target, int damage) {
    const int finalDamage = std::max(damage, 0);
    const int hpBefore = target.currentHp;
    target.currentHp = std::max(target.currentHp - finalDamage, 0);
    return FTacticalD20DamageApplicationResult{
        .hpBefore = hpBefore,
        .hpAfter = target.currentHp,
        .damageApplied = finalDamage,
        .defeated = target.currentHp <= 0,
    };
}

FTacticalD20DamageApplicationResult ApplyDamageAndDefeat(entt::registry& registry, entt::entity targetEntity, int damage) {
    // Transition table:
    //   FTacticalUnit hp after damage > 0  -> no state tag change
    //   FTacticalUnit hp after damage <= 0 -> add FUnitStateDefeated
    auto& target = registry.get<FTacticalUnit>(targetEntity);
    auto result = ApplyDamageToUnit(target, damage);
    if (result.defeated && !registry.all_of<FUnitStateDefeated>(targetEntity)) {
        registry.emplace<FUnitStateDefeated>(targetEntity);
    }
    return result;
}

} // namespace game