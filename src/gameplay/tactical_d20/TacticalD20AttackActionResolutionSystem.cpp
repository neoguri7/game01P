#include "gameplay/tactical_d20/TacticalD20AttackActionResolutionSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FAbilityScores.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FTacticalBoardTile.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "gameplay/tactical_d20/FTacticalD20ActionResolutionUtils.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20LogUtils.h"
#include "gameplay/tactical_d20/FTacticalD20Random.h"
#include "gameplay/tactical_d20/FTacticalD20RollPolicy.h"
#include "gameplay/tactical_d20/FTacticalD20Rules.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

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

std::string AttackInvalidReason(entt::registry& registry,
    const FTacticalUnit& attacker,
    const FTacticalUnit& target,
    const FTacticalD20WeaponConfig& weapon) {
    const int distance = std::abs(target.tileX - attacker.tileX) + std::abs(target.tileY - attacker.tileY);
    const bool ranged = weapon.attackType == "ranged";
    if (!ranged && distance != 1) return fmt::format("melee target distance {} is not adjacent", distance);
    if (distance > std::max(weapon.rangeTiles, 1)) return fmt::format("target distance {} exceeds range {}", distance, std::max(weapon.rangeTiles, 1));
    if (ranged && !LineOfSightClear(registry, attacker.tileX, attacker.tileY, target.tileX, target.tileY)) return "ranged line of sight is blocked";
    return {};
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

void PublishDamageEvent(entt::registry& registry, entt::entity attacker, entt::entity target, const FTacticalD20DamageApplicationResult& applied) {
    const FTacticalD20DamageAppliedEvent damageEvent{attacker, target, "weapon", applied.damageApplied, applied.hpBefore, applied.hpAfter, applied.defeated};
    PUBLISH(FTacticalD20DamageAppliedEvent, registry, damageEvent);
    QUEUE_FRAME_EVENT(FTacticalD20DamageAppliedEvent, registry, damageEvent);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] DamageApplied amount={} hp={}->{} defeated={}",
        applied.damageApplied,
        applied.hpBefore,
        applied.hpAfter,
        applied.defeated));
}

void ResolveInvalidAttack(entt::registry& registry, entt::entity attacker, const std::string& reason) {
    AppendTacticalD20CombatLog(registry, fmt::format("{} attack invalid: {}.", registry.get<FTacticalUnit>(attacker).displayName, reason));
    SetTacticalD20ActionEconomy<FActionEconomyTurnComplete>(registry, attacker);
    PublishTacticalD20ActionResolved(registry, attacker, "attack", true);
}

void ResolveAttack(entt::registry& registry, entt::entity attackerEntity, entt::entity targetEntity) {
    // Action economy transition table:
    //   HasMoveAndAction/HasActionOnly + Attack resolved or invalid -> TurnComplete
    if (targetEntity == entt::null || !registry.valid(targetEntity) || !registry.all_of<FTacticalUnit>(targetEntity)) {
        return ResolveInvalidAttack(registry, attackerEntity, "target entity is invalid");
    }
    if (registry.all_of<FUnitStateDefeated>(targetEntity)) return ResolveInvalidAttack(registry, attackerEntity, "target is defeated");

    auto& attacker = registry.get<FTacticalUnit>(attackerEntity);
    auto& target = registry.get<FTacticalUnit>(targetEntity);
    const int distance = std::abs(target.tileX - attacker.tileX) + std::abs(target.tileY - attacker.tileY);
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const auto* weapon = FindWeapon(config, attacker, distance);
    if (!weapon) return ResolveInvalidAttack(registry, attackerEntity, "no usable weapon is configured");
    if (const auto reason = AttackInvalidReason(registry, attacker, target, *weapon); !reason.empty()) {
        return ResolveInvalidAttack(registry, attackerEntity, reason);
    }

    if (!registry.ctx().contains<FTacticalD20Random>()) registry.ctx().emplace<FTacticalD20Random>();
    auto& random = registry.ctx().get<FTacticalD20Random>();
    const bool ranged = weapon->attackType == "ranged";
    const bool cover = ranged && TileMatches(registry, target.tileX, target.tileY, true);
    const int coverBonus = cover && config ? std::max(config->coverAcBonus, 0) : 0;
    const auto attackAbility = AbilityFromString(weapon->ability);
    const int abilityMod = AbilityModifier(registry.get<FAbilityScores>(attackerEntity), attackAbility);
    const int proficiency = HasProficiency(attacker.weaponProficiencies, weapon->id)
        ? TacticalD20PrototypeProficiencyBonus(config ? config->proficiencyBonus : 2)
        : 0;
    const auto policy = TacticalD20AttackRollPolicy(registry, attackerEntity, targetEntity);
    auto roll = ResolveD20Roll(FTacticalD20RollRequest{
        .mode = policy.mode,
        .abilityModifier = abilityMod,
        .proficiencyBonus = proficiency,
        .targetNumber = target.armorClass + coverBonus,
    }, random.rng);
    const auto outcome = ResolveAttackOutcome(roll, target.armorClass + coverBonus);
    PublishAttackEvent(registry, {attackerEntity, targetEntity, weapon->id, roll.selectedRoll, roll.total, target.armorClass + coverBonus, outcome.hit, outcome.criticalHit, cover, policy.disadvantageApplied});
    AppendTacticalD20CombatLog(registry, fmt::format("{} attack: {} vs AC {} -- {}.", weapon->displayName, roll.breakdown, target.armorClass + coverBonus, outcome.hit ? "HIT" : "MISS"));
    if (cover) AppendTacticalD20CombatLog(registry, fmt::format("{} has cover: +{} AC.", target.displayName, coverBonus));
    if (outcome.hit) {
        const int damageMod = AbilityModifier(registry.get<FAbilityScores>(attackerEntity), AbilityFromString(weapon->damageAbility, attackAbility));
        const auto damage = ResolveDamage(weapon->damageDice, damageMod, outcome.criticalHit, random.rng);
        const auto applied = ApplyDamageAndDefeat(registry, targetEntity, damage.finalDamage);
        PublishDamageEvent(registry, attackerEntity, targetEntity, applied);
        AppendTacticalD20CombatLog(registry, fmt::format("Damage: {}. {} HP {} -> {}{}.", damage.breakdown, target.displayName, applied.hpBefore, applied.hpAfter, applied.defeated ? " defeated" : ""));
    }
    SetTacticalD20ActionEconomy<FActionEconomyTurnComplete>(registry, attackerEntity);
    PublishTacticalD20ActionResolved(registry, attackerEntity, "attack", true);
}

} // namespace

void TacticalD20AttackActionResolutionSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20AttackActionResolutionSystem");
    entt::entity unit{entt::null};
    FQueuedTacticalD20Command command;
    if (TryTakeQueuedTacticalD20Command(registry, "attack", unit, command)) ResolveAttack(registry, unit, command.targetEntity);
}

} // namespace game
