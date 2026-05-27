#include "gameplay/tactical_d20/TacticalD20ActionResolutionSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FAbilityScores.h"
#include "ecs/components/FActionEconomyHasActionOnly.h"
#include "ecs/components/FActionEconomyHasMoveAndAction.h"
#include "ecs/components/FActionEconomyHasMoveOnly.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FCombatStateResolvingAction.h"
#include "ecs/components/FConditionDodge.h"
#include "ecs/components/FConditionPoisoned.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FQueuedTacticalD20Command.h"
#include "ecs/components/FTacticalBoardTile.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20LogUtils.h"
#include "gameplay/tactical_d20/FTacticalD20Random.h"
#include "gameplay/tactical_d20/FTacticalD20Rules.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

constexpr float TileSize = 64.f;
constexpr float OriginX = 64.f;
constexpr float OriginY = 64.f;

bool IsResolving(entt::registry& registry) {
    return !registry.view<FCombatStateResolvingAction>().empty();
}

void ClearEconomy(entt::registry& registry, entt::entity unit) {
    registry.remove<FActionEconomyHasMoveAndAction, FActionEconomyHasActionOnly, FActionEconomyHasMoveOnly, FActionEconomyTurnComplete>(unit);
}

template<typename Tag>
void SetEconomy(entt::registry& registry, entt::entity unit) {
    ClearEconomy(registry, unit);
    registry.emplace<Tag>(unit);
}

void PublishResolved(entt::registry& registry, entt::entity unit, const std::string& action, bool complete) {
    const int movement = registry.all_of<FTurnBudget>(unit) ? registry.get<FTurnBudget>(unit).movementBudgetTiles : 0;
    const FTacticalD20ActionResolvedEvent event{unit, action, movement, complete};
    PUBLISH(FTacticalD20ActionResolvedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20ActionResolvedEvent, registry, event);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] ActionResolved action={} complete={}", action, complete));
    AppendTacticalD20StateLog(registry, fmt::format("[Action] {} resolved", action));
}

const FTacticalD20WeaponConfig* FindWeapon(const FTacticalD20Config* config, const FTacticalUnit& attacker, int distance) {
    if (!config) return nullptr;
    const FTacticalD20WeaponConfig* fallback = nullptr;
    for (const auto& weaponId : attacker.weaponProficiencies) {
        const auto found = std::ranges::find_if(config->weapons, [&weaponId](const auto& weapon) { return weapon.id == weaponId; });
        if (found == config->weapons.end()) continue;
        if (!fallback) fallback = &(*found);
        const bool melee = found->attackType != "ranged";
        if ((distance <= 1 && melee) || (distance > 1 && !melee)) return &(*found);
    }
    return fallback;
}

bool TileMatches(entt::registry& registry, int x, int y, bool coverOnly) {
    auto view = registry.view<FTacticalBoardTile>();
    for (auto entity : view) {
        const auto& tile = view.get<FTacticalBoardTile>(entity);
        if (tile.tileX == x && tile.tileY == y) return coverOnly ? tile.isCover : tile.isWall;
    }
    return false;
}

bool LineOfSightClear(entt::registry& registry, int startX, int startY, int targetX, int targetY) {
    const int dx = std::abs(targetX - startX);
    const int dy = -std::abs(targetY - startY);
    const int sx = startX < targetX ? 1 : -1;
    const int sy = startY < targetY ? 1 : -1;
    int error = dx + dy;
    int x = startX;
    int y = startY;
    while (x != targetX || y != targetY) {
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
        if ((x != targetX || y != targetY) && TileMatches(registry, x, y, false)) return false;
    }
    return true;
}

void ResolveMove(entt::registry& registry, entt::entity unit, const FQueuedTacticalD20Command& command) {
    auto& tactical = registry.get<FTacticalUnit>(unit);
    tactical.tileX = command.targetTileX;
    tactical.tileY = command.targetTileY;
    if (auto* position = registry.try_get<FPosition>(unit)) {
        position->x = OriginX + static_cast<float>(tactical.tileX) * TileSize;
        position->y = OriginY + static_cast<float>(tactical.tileY) * TileSize;
    }
    auto& budget = registry.get<FTurnBudget>(unit);
    budget.movementBudgetTiles = std::max(budget.movementBudgetTiles - std::max(command.movementSpentTiles, 0), 0);
    const bool enemyMoveEndsTurn = tactical.team == "enemy";
    if (enemyMoveEndsTurn || registry.all_of<FActionEconomyHasMoveOnly>(unit)) SetEconomy<FActionEconomyTurnComplete>(registry, unit);
    else SetEconomy<FActionEconomyHasActionOnly>(registry, unit);
    AppendTacticalD20CombatLog(registry, fmt::format("{} moved to ({}, {}).", tactical.displayName, tactical.tileX, tactical.tileY));
    PublishResolved(registry, unit, "move", registry.all_of<FActionEconomyTurnComplete>(unit));
}

void ResolveDash(entt::registry& registry, entt::entity unit) {
    auto& budget = registry.get<FTurnBudget>(unit);
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const int multiplier = config ? std::max(config->actions.dashExtraMovementMultiplier, 0) : 1;
    budget.movementBudgetTiles += budget.baseMovementTiles * multiplier;
    SetEconomy<FActionEconomyHasMoveOnly>(registry, unit);
    AppendTacticalD20CombatLog(registry, fmt::format("{} dashed.", registry.get<FTacticalUnit>(unit).displayName));
    PublishResolved(registry, unit, "dash", false);
}

void ResolveDodge(entt::registry& registry, entt::entity unit) {
    registry.emplace_or_replace<FConditionDodge>(unit);
    SetEconomy<FActionEconomyTurnComplete>(registry, unit);
    AppendTacticalD20CombatLog(registry, fmt::format("{} dodged.", registry.get<FTacticalUnit>(unit).displayName));
    const FTacticalD20ConditionChangedEvent condition{unit, "dodge", "applied", 1};
    PUBLISH(FTacticalD20ConditionChangedEvent, registry, condition);
    QUEUE_FRAME_EVENT(FTacticalD20ConditionChangedEvent, registry, condition);
    PublishResolved(registry, unit, "dodge", true);
}

void ResolveWait(entt::registry& registry, entt::entity unit) {
    SetEconomy<FActionEconomyTurnComplete>(registry, unit);
    AppendTacticalD20CombatLog(registry, fmt::format("{} waited.", registry.get<FTacticalUnit>(unit).displayName));
    PublishResolved(registry, unit, "wait", true);
}

bool ValidateAttack(entt::registry& registry, const FTacticalUnit& attacker, const FTacticalUnit& target, const FTacticalD20WeaponConfig& weapon) {
    const int distance = std::abs(target.tileX - attacker.tileX) + std::abs(target.tileY - attacker.tileY);
    const bool ranged = weapon.attackType == "ranged";
    if (!ranged && distance != 1) return false;
    if (distance > std::max(weapon.rangeTiles, 1)) return false;
    return !ranged || LineOfSightClear(registry, attacker.tileX, attacker.tileY, target.tileX, target.tileY);
}

void PublishAttackEvent(entt::registry& registry, const FTacticalD20AttackResolvedEvent& event) {
    PUBLISH(FTacticalD20AttackResolvedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20AttackResolvedEvent, registry, event);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] AttackResolved weapon={} total={} ac={} result={}",
        event.weaponId,
        event.total,
        event.effectiveArmorClass,
        event.hit ? "hit" : "miss"));
}

void ResolveAttack(entt::registry& registry, entt::entity attackerEntity, entt::entity targetEntity) {
    auto& attacker = registry.get<FTacticalUnit>(attackerEntity);
    auto& target = registry.get<FTacticalUnit>(targetEntity);
    const int distance = std::abs(target.tileX - attacker.tileX) + std::abs(target.tileY - attacker.tileY);
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const auto* weapon = FindWeapon(config, attacker, distance);
    if (!weapon || !ValidateAttack(registry, attacker, target, *weapon)) {
        AppendTacticalD20CombatLog(registry, fmt::format("{} attack had no valid target.", attacker.displayName));
        SetEconomy<FActionEconomyTurnComplete>(registry, attackerEntity);
        return PublishResolved(registry, attackerEntity, "attack", true);
    }

    if (!registry.ctx().contains<FTacticalD20Random>()) registry.ctx().emplace<FTacticalD20Random>();
    auto& random = registry.ctx().get<FTacticalD20Random>();
    const bool ranged = weapon->attackType == "ranged";
    const bool cover = ranged && TileMatches(registry, target.tileX, target.tileY, true);
    const int coverBonus = cover && config ? std::max(config->coverAcBonus, 0) : 0;
    const bool poisoned = registry.all_of<FConditionPoisoned>(attackerEntity);
    const bool dodging = registry.all_of<FConditionDodge>(targetEntity);
    const auto attackAbility = AbilityFromString(weapon->ability);
    const int abilityMod = AbilityModifier(registry.get<FAbilityScores>(attackerEntity), attackAbility);
    const int proficiency = HasProficiency(attacker.weaponProficiencies, weapon->id)
        ? TacticalD20PrototypeProficiencyBonus(config ? config->proficiencyBonus : 2)
        : 0;
    auto roll = ResolveD20Roll(FTacticalD20RollRequest{
        .hasDisadvantage = poisoned || dodging,
        .abilityModifier = abilityMod,
        .proficiencyBonus = proficiency,
        .targetNumber = target.armorClass + coverBonus,
    }, random.rng);
    const auto outcome = ResolveAttackOutcome(roll, target.armorClass + coverBonus);
    PublishAttackEvent(registry, {attackerEntity, targetEntity, weapon->id, roll.selectedRoll, roll.total, target.armorClass + coverBonus, outcome.hit, outcome.criticalHit, cover, poisoned || dodging});
    AppendTacticalD20CombatLog(registry, fmt::format("{} attack: {} vs AC {} -- {}.", weapon->displayName, roll.breakdown, target.armorClass + coverBonus, outcome.hit ? "HIT" : "MISS"));
    if (cover) AppendTacticalD20CombatLog(registry, fmt::format("{} has cover: +{} AC.", target.displayName, coverBonus));
    if (outcome.hit) {
        const int damageMod = AbilityModifier(registry.get<FAbilityScores>(attackerEntity), AbilityFromString(weapon->damageAbility, attackAbility));
        const auto damage = ResolveDamage(weapon->damageDice, damageMod, outcome.criticalHit, random.rng);
        const auto applied = ApplyDamageAndDefeat(registry, targetEntity, damage.finalDamage);
        const FTacticalD20DamageAppliedEvent damageEvent{attackerEntity, targetEntity, "weapon", applied.damageApplied, applied.hpBefore, applied.hpAfter, applied.defeated};
        PUBLISH(FTacticalD20DamageAppliedEvent, registry, damageEvent);
        QUEUE_FRAME_EVENT(FTacticalD20DamageAppliedEvent, registry, damageEvent);
        AppendTacticalD20EventLog(registry, fmt::format("[Event] DamageApplied amount={} hp={}->{} defeated={}",
            applied.damageApplied,
            applied.hpBefore,
            applied.hpAfter,
            applied.defeated));
        AppendTacticalD20CombatLog(registry, fmt::format("Damage: {}. {} HP {} -> {}{}.", damage.breakdown, target.displayName, applied.hpBefore, applied.hpAfter, applied.defeated ? " defeated" : ""));
    }
    SetEconomy<FActionEconomyTurnComplete>(registry, attackerEntity);
    PublishResolved(registry, attackerEntity, "attack", true);
}

void ResolveCommand(entt::registry& registry, entt::entity unit, const FQueuedTacticalD20Command& command) {
    if (command.actionId == "move" && command.hasTargetTile) return ResolveMove(registry, unit, command);
    if (command.actionId == "dash") return ResolveDash(registry, unit);
    if (command.actionId == "dodge") return ResolveDodge(registry, unit);
    if (command.actionId == "attack" && command.targetEntity != entt::null && registry.valid(command.targetEntity)) return ResolveAttack(registry, unit, command.targetEntity);
    ResolveWait(registry, unit);
}

} // namespace

void TacticalD20ActionResolutionSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20ActionResolutionSystem");
    if (!IsResolving(registry)) return;

    auto view = registry.view<FTacticalUnit, FQueuedTacticalD20Command>(entt::exclude<FUnitStateDefeated>);
    for (auto unit : view) {
        const auto command = view.get<FQueuedTacticalD20Command>(unit);
        registry.remove<FQueuedTacticalD20Command>(unit);
        if (!command.validationApproved) continue;
        ResolveCommand(registry, unit, command);
        return;
    }
}

} // namespace game
