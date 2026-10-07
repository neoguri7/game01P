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
#include "ecs/components/FPosition.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTypes.h"
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
/// target. Grid movement uses the separate move AP pool; visuals never determine hits.
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
        const auto* runtime = registry.ctx().find<FTurnRuntime>();
        const auto snapshot = runtime ? runtime->controller.snapshot() : FTurnSnapshot{};
        if (!snapshot.currentTurn || request.turn != *snapshot.currentTurn || request.turn.actor != actor) {
            return Reject(log, "Action rejected: not the current turn");
        }
        if (!IsAlive(registry, actor)) {
            return Reject(log, "Action rejected: unit is down");
        }

        const auto* loadout = registry.try_get<FSkillLoadout>(actor);
        auto* points = registry.try_get<FActionPoints>(actor);
        const auto* stats = registry.try_get<FStats>(actor);
        const auto* actorCell = registry.try_get<FGridPosition>(actor);
        if (loadout == nullptr || points == nullptr || stats == nullptr || actorCell == nullptr) {
            return Reject(log, "Action rejected: unit is missing combat components");
        }

        if (request.action == EBattleAction::Move) {
            const auto* metrics = registry.ctx().find<FGridMetrics>();
            auto* position = registry.try_get<FPosition>(actor);
            const auto reachable = ComputeReachableCells(registry, actor, points->move);
            const bool legal = std::ranges::any_of(reachable, [&](const FGridPosition& cell) {
                return cell.x == request.cell.x && cell.y == request.cell.y;
            });
            if (!metrics || !position || !legal) {
                return Reject(log, "Move rejected: no move AP, occupied, or out of reach");
            }
            registry.get<FGridPosition>(actor) = request.cell;
            const auto world = GridCellTopLeftToWorld(*metrics, request.cell.x, request.cell.y);
            position->x = world.x;
            position->y = world.y;
            --points->move;
            return Reject(log, UnitDisplayName(registry, actor) + " moves to ("
                + std::to_string(request.cell.x) + "," + std::to_string(request.cell.y) + ")");
        }

        const FSkillSlot* slot = nullptr;
        for (const FSkillSlot& candidate : loadout->slots) {
            if (candidate.skillId == request.skillId) {
                slot = &candidate;
                break;
            }
        }
        if (slot == nullptr) {
            return Reject(log, UnitDisplayName(registry, actor) + " does not know that skill");
        }

        const auto resolved = ResolveSkillSlot(*slot, content);
        if (!resolved) {
            return Reject(log, UnitDisplayName(registry, actor) + " skill could not be resolved");
        }

        const bool cellTarget = resolved->target == "cell";
        if (!cellTarget && !IsAlive(registry, request.target)) {
            return Reject(log, UnitDisplayName(registry, actor) + " has no target");
        }
        const bool targetIsParty = registry.all_of<FPartyMember>(request.target);
        // Target sides are read from the CASTER's perspective: "enemy"/"cell" hit the other side,
        // "ally"/"self" hit the caster's own side. This is what lets an enemy use the same strike skill.
        const bool casterIsParty = registry.all_of<FPartyMember>(actor);
        const bool targetsOwnSide = resolved->target == "ally" || resolved->target == "self";
        const bool affectsParty = targetsOwnSide ? casterIsParty : !casterIsParty;
        if (!cellTarget && affectsParty != targetIsParty) {
            return Reject(log, affectsParty ? "Target is not an ally" : "Target is not an enemy");
        }
        if (resolved->target == "self" && request.target != actor) {
            return Reject(log, "This skill can only target the caster");
        }

        const auto* targetCell = cellTarget ? &request.cell : registry.try_get<FGridPosition>(request.target);
        const auto* metrics = registry.ctx().find<FGridMetrics>();
        if (!metrics || !targetCell || !IsInsideGrid(*metrics, targetCell->x, targetCell->y)) {
            return Reject(log, "Target is outside the battlefield");
        }
        if (targetCell == nullptr || !IsInRange(*actorCell, *targetCell, resolved->range)) {
            return Reject(log, UnitDisplayName(registry, actor) + " target is out of range");
        }

        if (points->skill < resolved->apCost) {
            return Reject(log, UnitDisplayName(registry, actor) + " lacks skill AP ("
                + std::to_string(points->skill) + "/" + std::to_string(resolved->apCost) + ")");
        }
        points->skill -= resolved->apCost;

        const std::string actorName = UnitDisplayName(registry, actor);
        const gameplay::FSkillContent* skillRow = content.skills.find(slot->skillId);
        const std::string skillName = skillRow != nullptr ? skillRow->displayName : slot->skillId;

        // Pierce hits the target cell plus the straight line beyond it, measured from the caster.
        const std::vector<FGridPosition> cells = ComputeSkillCells(*resolved, *targetCell, *actorCell);
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
                // Support skills: only skills with power heal; heal = magic attack + power.
                const int heal = resolved->power > 0 ? std::max(0, stats->magicAttack + resolved->power) : 0;
                if (auto* health = registry.try_get<FHealth>(target); health && heal > 0) {
                    const int before = health->current;
                    health->current = std::min(health->max, health->current + heal);
                    if (log && health->current > before) {
                        log->push(actorName + "'s " + skillName + " -> " + targetName + " heals "
                                  + std::to_string(health->current - before));
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
                    std::string line = actorName + "'s " + skillName + " -> " + targetName + " takes "
                        + std::to_string(damage) + " damage";
                    if (const auto* health = registry.try_get<FHealth>(target)) {
                        line += " (" + std::to_string(health->current) + "/" + std::to_string(health->max) + ")";
                    }
                    if (died) line += " - defeated";
                    log->push(std::move(line));
                    if (died) {
                        log->push(targetName + " falls!");
                    }
                }
                // §3.3 흡혈: 피해를 준 공격자의 HP를 회복한다(대상이 쓰러져도 적용).
                if (resolved->lifeOnHit > 0) {
                    if (auto* actorHealth = registry.try_get<FHealth>(actor);
                        actorHealth != nullptr && actorHealth->current > 0) {
                        const int before = actorHealth->current;
                        actorHealth->current = std::min(actorHealth->max, actorHealth->current + resolved->lifeOnHit);
                        if (log && actorHealth->current > before) {
                            log->push(actorName + " leeches +" + std::to_string(actorHealth->current - before));
                        }
                    }
                }
            } else if (log && !resolved->statuses.empty()) {
                log->push(actorName + "'s " + skillName + " -> " + targetName + " is afflicted");
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
