#pragma once

#include <cstdint>
#include <string>

#include <entt/entt.hpp>

#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FSkillContent.h"
#include "gameplay/dungeon/FRunSequence.h"

namespace game {

/// The rule of the battle being fought. Registry context, never saved: the room build installs it.
struct FEliteBattleState {
    FEliteAffix affix;                      // a copy of the rule row
    int healthPercent{0};                   // a copy of FEliteRules::healthPercent
    int attackBonus{0};                     // applied to every enemy's attack and magic attack by the sheet
    int pendingBonus{0};                    // SpellFed: queued, applied at the next round start
    std::uint64_t lastRound{0};             // the last round number EliteOnRoundStarted processed
};

/// Erases the state; when affix is non-null, emplaces a fresh one (bonuses 0, lastRound 0).
void InstallEliteBattle(entt::registry& registry, const FEliteAffix* affix, int healthPercent);
[[nodiscard]] const FEliteBattleState* FindEliteBattle(const entt::registry& registry);
/// No state: nothing. Else: bonuses and lastRound back to 0; for Unyielding, adds each status id of the
/// rule to `FActiveStatuses::immune` of every entity with `FEnemy` (get_or_emplace; no duplicate entries).
void ApplyEliteBattleStart(entt::registry& registry);
/// SpellFed only: when actor has FPartyMember and skill.tags contains "Spell",
/// pendingBonus = min(pendingBonus + amount, maxBonus - attackBonus). Nothing else changes.
void EliteOnSkillUsed(entt::registry& registry, entt::entity actor, const gameplay::FSkillContent& skill);
/// Once per round number (roundNumber == 0 or <= lastRound: nothing). SpellFed: attackBonus += pendingBonus,
/// pendingBonus = 0. Escalating: when roundNumber >= fromRound, attackBonus = min(attackBonus + amount, maxBonus).
/// When attackBonus changed: RefreshCombatSheet for every FEnemy entity (only if the registry has an
/// FDungeonRun) and one feed line.
void EliteOnRoundStarted(entt::registry& registry, const gameplay::FContentRegistry& content, std::uint64_t roundNumber);
/// Thorned only: a party unit that damaged an enemy with a Melee-tagged skill loses `amount` HP (never below 1).
/// Reads the skill row from the FContentRegistry in the registry context.
void EliteOnHit(entt::registry& registry, const FHookContext& context);
/// Registers EliteOnHit as an OnHit listener (boot, once).
void RegisterEliteRuleListener(entt::registry& registry);
/// "" for Escalating and Unyielding. For SpellFed ("Spell") and Thorned ("Melee"): the display names of the
/// party's skills whose base tags contain the tag, in PartySkillRows order, as
/// "Your <Tag> skills: A, B. " or "Your party has no <Tag> skills. " (note the trailing space).
[[nodiscard]] std::string EliteAffectedSkills(entt::registry& registry, const FEliteAffix& affix);
/// "" without a state, or when the registry has an FDungeonRun whose phase is not Battle; else the HUD line.
[[nodiscard]] std::string EliteRuleHudLine(entt::registry& registry);

} // namespace game
