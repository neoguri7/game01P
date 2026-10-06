#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FStats.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"

namespace game::ecs {

/// Applies queued battle actions: validates the actor, AP, target side and range, then resolves the
/// skill through the rune pipeline (docs/design/skill-rune-progression.md §3/§8) onto every affected
/// target. Movement joins in a later slice.
struct ActionResolverSystem {
    void update(entt::registry& registry, float /*deltaTime*/) {
        auto* queue = registry.ctx().find<FActionQueue>();
        auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        auto* outcome = registry.ctx().find<FBattleOutcome>();
        if (!queue || queue->pending.empty() || !content || (outcome && outcome->resolved)) {
            return;
        }

        const std::vector<FActionRequest> requests = std::move(queue->pending);
        queue->pending.clear();

        auto* log = registry.ctx().find<FCombatLog>();
        for (const FActionRequest& request : requests) {
            Resolve(registry, request, *content, log);
        }
    }

    [[nodiscard]] std::string name() const { return "ActionResolverSystem"; }

private:
    static void Reject(FCombatLog* log, const std::string& reason) {
        if (log != nullptr) {
            log->push(reason);
        }
    }

    static void Resolve(entt::registry& registry, const FActionRequest& request,
                        const gameplay::FContentRegistry& content, FCombatLog* log) {
        const entt::entity actor = request.actor;
        if (!IsAlive(registry, actor)) {
            return Reject(log, "행동 불가(쓰러짐)");
        }

        const auto* loadout = registry.try_get<FSkillLoadout>(actor);
        auto* points = registry.try_get<FActionPoints>(actor);
        const auto* stats = registry.try_get<FStats>(actor);
        const auto* actorCell = registry.try_get<FGridPosition>(actor);
        if (loadout == nullptr || points == nullptr || stats == nullptr || actorCell == nullptr) {
            return Reject(log, "행동 불가(구성 요소 없음)");
        }

        const FSkillSlot* slot = nullptr;
        for (const FSkillSlot& candidate : loadout->slots) {
            if (candidate.skillId == request.skillId) {
                slot = &candidate;
                break;
            }
        }
        if (slot == nullptr) {
            return Reject(log, UnitDisplayName(registry, actor) + " 알 수 없는 스킬");
        }

        const auto resolved = ResolveSkillSlot(*slot, content);
        if (!resolved) {
            return Reject(log, UnitDisplayName(registry, actor) + " 스킬 해석 실패");
        }

        if (!IsAlive(registry, request.target)) {
            return Reject(log, UnitDisplayName(registry, actor) + " 대상이 없다");
        }
        const bool targetIsParty = registry.all_of<FPartyMember>(request.target);
        // Target sides are read from the CASTER's perspective: "enemy"/"cell" hit the other side,
        // "ally"/"self" hit the caster's own side. This is what lets an enemy use the same strike skill.
        const bool casterIsParty = registry.all_of<FPartyMember>(actor);
        const bool targetsOwnSide = resolved->target == "ally" || resolved->target == "self";
        const bool affectsParty = targetsOwnSide ? casterIsParty : !casterIsParty;
        if (affectsParty != targetIsParty) {
            return Reject(log, affectsParty ? "아군이 아닌 대상" : "적이 아닌 대상");
        }

        const auto* targetCell = registry.try_get<FGridPosition>(request.target);
        if (targetCell == nullptr || !IsInRange(*actorCell, *targetCell, resolved->range)) {
            return Reject(log, UnitDisplayName(registry, actor) + " 사거리 밖");
        }

        if (points->skill < resolved->apCost) {
            return Reject(log, UnitDisplayName(registry, actor) + " 스킬 AP 부족 ("
                + std::to_string(points->skill) + "/" + std::to_string(resolved->apCost) + ")");
        }
        points->skill -= resolved->apCost;

        const std::string actorName = UnitDisplayName(registry, actor);
        const gameplay::FSkillContent* skillRow = content.skills.find(slot->skillId);
        const std::string skillName = skillRow != nullptr ? skillRow->displayName : slot->skillId;

        if (resolved->shape == "pierce") {
            Reject(log, "관통 형태는 아직 구현되지 않아 단일 대상으로 처리한다");
        }

        // Shape semantics live in gameplay/combat so the resolver, the HUD and later slices share one
        // definition of what a skill affects (docs/design/skill-rune-progression.md §3.2).
        const std::vector<FGridPosition> cells = ComputeSkillCells(*resolved, *targetCell);
        const std::vector<entt::entity> affected = FindUnitsOnCells(registry, cells, affectsParty);

        // §1.1: 공격 항은 damage_type이 고른다. 버프/디버프(stat 상태)는 어느 쪽 공격 항에도 더해진다.
        const bool magical = resolved->damageType == gameplay::kDamageTypeMagical;
        int offense = magical ? stats->magicAttack : stats->attack;
        if (const auto* active = registry.try_get<FActiveStatuses>(actor); active && !active->effects.empty()) {
            offense += StatusAttackModifier(*active, CollectStatuses(*active, content));
        }

        for (const entt::entity target : affected) {
            const std::string targetName = UnitDisplayName(registry, target);
            bool died = false;

            if (targetsOwnSide) {
                // Support skills: 회복량 = 마법공격력 + power (docs/design/skill-rune-progression.md §8, 잠정).
                const int heal = std::max(0, stats->magicAttack + resolved->power);
                if (auto* health = registry.try_get<FHealth>(target); health && heal > 0) {
                    const int before = health->current;
                    health->current = std::min(health->max, health->current + heal);
                    if (log && health->current > before) {
                        log->push(actorName + "의 " + skillName + " → " + targetName + " "
                                  + std::to_string(health->current - before) + " 회복");
                    }
                }
            } else if (resolved->power > 0) {
                // §1.1: 감소 항도 damage_type이 고른다(물리: 방어력, 마법: 마법방어력).
                int defense = 0;
                if (const auto* targetStats = registry.try_get<FStats>(target)) {
                    defense = magical ? targetStats->magicDefense : targetStats->defense;
                }
                const int damage = ComputeDamage(offense, resolved->power, defense);
                died = ApplyDamageToUnit(registry, target, damage);
                if (log) {
                    std::string line = actorName + "의 " + skillName + " → " + targetName + " "
                        + std::to_string(damage) + " 피해";
                    if (const auto* health = registry.try_get<FHealth>(target)) {
                        line += " (" + std::to_string(health->current) + "/" + std::to_string(health->max) + ")";
                    }
                    if (died) line += " — 처치";
                    log->push(std::move(line));
                    if (died) {
                        log->push(targetName + " 쓰러짐!");
                    }
                }
                // §3.3 흡혈: 피해를 준 공격자의 HP를 회복한다(대상이 쓰러져도 적용).
                if (resolved->lifeOnHit > 0) {
                    if (auto* actorHealth = registry.try_get<FHealth>(actor);
                        actorHealth != nullptr && actorHealth->current > 0) {
                        const int before = actorHealth->current;
                        actorHealth->current = std::min(actorHealth->max, actorHealth->current + resolved->lifeOnHit);
                        if (log && actorHealth->current > before) {
                            log->push(actorName + " 흡혈 +" + std::to_string(actorHealth->current - before));
                        }
                    }
                }
            } else if (log && !resolved->statuses.empty()) {
                log->push(actorName + "의 " + skillName + " → " + targetName + " 상태 부여");
            }

            if (died) {
                continue;
            }
            for (const FResolvedStatus& status : resolved->statuses) {
                const gameplay::FStatusContent* definition = content.statuses.find(status.statusId);
                if (definition == nullptr) continue;
                if (auto* active = registry.try_get<FActiveStatuses>(target)) {
                    ApplyStatus(*active, *definition, status.durationDelta, status.magnitudeDelta);
                }
            }
        }
    }
};

} // namespace game::ecs
