#include "gameplay/combat/FCombatSheet.h"

#include <algorithm>

#include "core/Logger.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBaseCombatStats.h"
#include "ecs/components/FBossGoals.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEliteRules.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunPath.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/grid/FGridQuery.h"

namespace game {
namespace {

// ③ 적 스케일의 명명 상수 — 구 ApplyRunParty(FDungeonRun.cpp)의 리터럴 100/5/1과 값이 같다.
inline constexpr int kMaxEnemyDepthScaling = 100;
inline constexpr int kEnemyHealthPerRoom = 5;
inline constexpr int kEnemyAttackPerRoom = 1;

} // namespace

void RefreshCombatSheet(
    entt::registry& registry, entt::entity entity, const gameplay::FContentRegistry& content) {
    const auto* base = registry.try_get<FBaseCombatStats>(entity);
    auto* stats = registry.try_get<FStats>(entity);
    auto* health = registry.try_get<FHealth>(entity);
    if (base == nullptr || stats == nullptr || health == nullptr) {
        return;
    }
    auto* speed = registry.try_get<FSpeed>(entity);

    // ① 베이스 — 갱신은 매번 여기서 시작한다(제자리 가산 금지 → 멱등).
    *stats = base->stats;
    health->max = base->maxHealth;
    if (speed != nullptr) speed->value = base->speed;

    // ② 런 보너스 — FDungeonRun 경로(Ember)에서만.
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run != nullptr && registry.all_of<FPartyMember>(entity)) {
        const auto* unit = registry.try_get<FUnitData>(entity);
        const auto found = unit != nullptr
            ? std::ranges::find(run->party, unit->definition.id, &FRunMember::id)
            : run->party.end();
        if (found != run->party.end()) {
            const auto* growth = registry.ctx().find<FDungeonGrowth>();
            if (growth == nullptr) LOG_ERROR("Run growth content is missing; node bonuses are not applied.");
            const FPassiveStatBonuses nodes = growth != nullptr
                ? growth->passiveTree.Bonuses(found->allocatedNodes) : FPassiveStatBonuses{};
            health->max += found->bonusHealth + nodes.maxHealth;
            stats->attack += found->bonusAttack + nodes.attack;
            stats->magicAttack += found->bonusMagic + nodes.magicAttack;
            stats->defense += found->bonusDefense + nodes.defense;
            stats->magicDefense += found->bonusMagicDefense + nodes.magicDefense;
            if (speed != nullptr) speed->value += found->bonusSpeed + nodes.speed;
        }
    }

    // ③ 적 스케일 — 구 ApplyRunParty 공식 그대로(FDungeonRun 경로에서만, 비파티). WP-08: depth 성장은
    //    캡(kMaxEnemyDepthScaling)에서 멈추고 그 위에 회차(cycle) 항이 가산된다(이중 캡 없음).
    if (run != nullptr && !registry.all_of<FPartyMember>(entity)) {
        const int extra = std::min(run->depth - 1, kMaxEnemyDepthScaling);
        health->max += extra * DungeonBalance(registry, "enemy_health_per_depth") + run->room * kEnemyHealthPerRoom;
        stats->attack += extra * DungeonBalance(registry, "enemy_attack_per_depth") + run->room * kEnemyAttackPerRoom;
        stats->magicAttack +=
            extra * DungeonBalance(registry, "enemy_attack_per_depth") + run->room * kEnemyAttackPerRoom;
        // WP-08: 회차 스케일(D-19) — 규칙은 FRunSequence 한 곳이 갖고 이 블록은 적용만 한다. 회차 0이나
        // 빈 테이블이면 0이 돌아온다(콘텐츠를 되돌리는 롤백 레버).
        const FCycleScale cycle = CycleScaleFor(registry);
        if (cycle.healthPct != 0) health->max += health->max * cycle.healthPct / 100;
        if (cycle.attackPct != 0) {
            stats->attack += stats->attack * cycle.attackPct / 100;
            stats->magicAttack += stats->magicAttack * cycle.attackPct / 100;
        }
        // WP-44 elite stage (combat-math.md: enemy sheet), after the cycle scale: max HP +health_percent%, then the
        // rule's flat attack bonus on attack and magic attack.
        if (const FEliteBattleState* elite = FindEliteBattle(registry)) {
            health->max += health->max * elite->healthPercent / 100;
            stats->attack += elite->attackBonus;
            stats->magicAttack += elite->attackBonus;
        }
    }

    // ④ 상태 speed_pct — 100 = 변화 없음, 곱으로 합성하고 하한 1: speed = max(1, round(base × Π(pct/100))).
    //    속도는 이니셔티브(TurnController)가 합성 없이 그대로 읽는 파생 수치라 시트가 소유한다 — 만료되면
    //    다음 갱신이 배수를 제거해 원래 속도로 돌아온다. 공격 계열 상태 보정은 여기서 접지 않는다: 사용 시점
    //    합성(리졸버 ActionResolverSystem / AI FAiWorld)이 유지된다 — 시트가 접으면 두 경로가 같은 보정을
    //    두 번 센다(AttackModifierNotDoubleCounted).
    if (speed != nullptr) {
        if (const auto* active = registry.try_get<FActiveStatuses>(entity);
            active != nullptr && !active->effects.empty()) {
            speed->value = StatusSpeedValue(speed->value, *active, CollectStatuses(*active, content));
        }
    }

    // max 변경 시 current 규칙: 감소 시 clamp, 증가 시 유지(비율 보존 없음).
    health->current = std::min(health->current, health->max);
}

void RefreshAllCombatSheets(entt::registry& registry, const gameplay::FContentRegistry& content) {
    for (const auto entity : registry.view<FBaseCombatStats>()) {
        RefreshCombatSheet(registry, entity, content);
    }
}

void ApplyBattleStart(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    for (const auto entity : registry.view<FUnitData, FHealth, FStats>()) {
        auto& health = registry.get<FHealth>(entity);
        if (registry.all_of<FPartyMember>(entity)) {
            if (run == nullptr) continue;
            const auto& id = registry.get<FUnitData>(entity).definition.id;
            const auto found = std::ranges::find(run->party, id, &FRunMember::id);
            if (found == run->party.end()) continue;  // 런에 없는 파티원은 완전히 그대로 둔다(구 동작)
            health.current = found->health >= 0 ? std::clamp(found->health, 1, health.max) : health.max;
            if (!found->skills.empty()) {
                if (auto* loadout = registry.try_get<FSkillLoadout>(entity)) loadout->slots = found->skills;
            }
        } else {
            health.current = health.max;
        }
    }
}

std::optional<FSkillPreview> PreviewSkill(
    entt::registry& registry,
    entt::entity actor,
    const FResolvedSkill& resolved,
    const FGridPosition& aim,
    const FGridPosition& actorCell) {
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    const auto* stats = registry.try_get<FStats>(actor);
    if (content == nullptr || stats == nullptr) {
        return std::nullopt;
    }

    FSkillPreview preview;
    preview.apCost = resolved.apCost;
    preview.cells = ComputeSkillCells(resolved, aim, actorCell);

    // 리졸버와 같은 대상 규칙(진영은 시전자 관점): "ally"/"self" = 자기 진영, 그 외 = 상대 진영.
    const bool targetsOwnSide = resolved.target == "ally" || resolved.target == "self";
    const bool casterIsParty = registry.all_of<FPartyMember>(actor);
    const bool affectsParty = targetsOwnSide ? casterIsParty : !casterIsParty;

    // 사용 시점 합성(§1.1): 시트에 접지 않는다 — 리졸버와 같은 값(StatusAttackModifier).
    int attackModifier = 0;
    if (const auto* active = registry.try_get<FActiveStatuses>(actor); active && !active->effects.empty()) {
        attackModifier = StatusAttackModifier(*active, CollectStatuses(*active, *content));
    }

    for (const entt::entity target : FindUnitsOnCells(registry, preview.cells, affectsParty)) {
        if (!HasGridLineOfSight(registry, actorCell, registry.get<FGridPosition>(target))) {
            continue;
        }
        const auto* targetStats = registry.try_get<FStats>(target);
        const auto* health = registry.try_get<FHealth>(target);
        const int current = health != nullptr ? health->current : 0;

        FPreviewTarget entry;
        entry.unit = target;
        entry.healthAfter = current;

        if (targetsOwnSide) {
            // 힐 수치는 ComputeSkillHeal 단일 소스가 낸다. 근거 항(offense/power/defense)은 힐 대상에서
            // 0으로 둔다 — 힐 로그는 근거를 쓰지 않는다(소비자가 생기면 ComputeSkillHeal 옆에 같은 방식으로).
            const int rawHeal = ComputeSkillHeal(*stats, resolved);
            entry.heal = health != nullptr ? std::min(std::max(0, health->max - current), rawHeal) : 0;
            entry.healthAfter = current + entry.heal;
        } else if (resolved.power > 0) {
            // 계산도 근거도 ComputeSkillDamageTerms 한 곳에서 — 프리뷰와 리졸버가 같은 숫자를 본다.
            const FDamageTerms terms = ComputeSkillDamageTerms(
                *stats, attackModifier, resolved, targetStats != nullptr ? *targetStats : FStats{});
            entry.offense = terms.offense;
            entry.power = terms.power;
            entry.defense = terms.defense;
            entry.damage = terms.damage;
            // WP-15: the preview is the gate's canon — the resolver applies this number, so the cap sits
            // here (after ComputeDamage's minimum-1: a gated hit at the floor is a real 0, not 1). The
            // offense/power/defense terms above stay the uncapped formula terms.
            const int capped = BossBodyDamageCap(registry, target, entry.damage);
            entry.blockedByGate = capped < entry.damage;
            entry.damage = capped;
            entry.healthAfter = std::max(0, current - entry.damage);
            entry.lethal = entry.healthAfter == 0;
            // WP-39: a party hit may crit; the number is the boss-capped doubled damage, shown only when it
            // raises the hit. The preview rolls nothing (the resolver owns the roll).
            if (casterIsParty && terms.damage > 0) {
                if (const auto* runtime = registry.ctx().find<FCombatRuntime>();
                    runtime != nullptr && runtime->critChancePercent > 0) {
                    const int crit = BossBodyDamageCap(
                        registry, target, ComputeCritDamage(terms.damage, runtime->critMultiplierPercent));
                    entry.critDamage = crit > entry.damage ? crit : 0;
                    preview.critChancePercent = runtime->critChancePercent;
                }
            }
        }

        // 부여 목록: 리졸버가 실제로 적용하는 것과 같다 — 정의가 있는 상태만, lethal이면 없음. WP-15b: 면역
        // 대상은 TryApplyStatus가 차단하므로 프리뷰 목록에서도 뺀다(프리뷰 = 실제 적용).
        if (!entry.lethal) {
            const auto* targetActive = registry.try_get<FActiveStatuses>(target);
            for (const FResolvedStatus& status : resolved.statuses) {
                if (content->statuses.find(status.statusId) != nullptr &&
                    (targetActive == nullptr || !IsStatusImmune(*targetActive, status.statusId))) {
                    entry.statuses.push_back(status);
                }
            }
        }
        preview.targets.push_back(std::move(entry));
    }
    return preview;
}

} // namespace game
