#pragma once

#include <algorithm>
#include <cstddef>
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
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTerrain.h"
#include "ecs/components/FPosition.h"
#include "ecs/systems/TurnSystem.h"

namespace game::ecs {

/// Applies queued battle actions: validates the actor, AP, target side and range, then resolves the
/// skill through the rune pipeline onto every affected target, or moves along a collision-checked path.
/// Every drained command produces one recorded FCommandResult (ECommandReject + the exact player-facing
/// text); the drain of a resolved battle consumes the queue without a single new FCombatLog line (WP-06).
struct ActionResolverSystem {
    void update(entt::registry& registry, float /*deltaTime*/) {
        auto* queue = registry.ctx().find<FActionQueue>();
        if (queue == nullptr) {
            return;
        }
        auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        auto* outcome = registry.ctx().find<FBattleOutcome>();
        auto* log = registry.ctx().find<FCombatLog>();
        auto* outbox = registry.ctx().find<FCommandOutbox>();
        auto* results = registry.ctx().find<FCommandResults>();

        const bool decided = outcome != nullptr && outcome->resolved;
        if (!decided && content == nullptr) {
            return; // cannot resolve yet; the queue waits for the content to exist
        }
        if (queue->pending.empty()) {
            // P6-1: meta without a batch is stale provenance (the queue is re-emplaced per battle, the
            // outbox is not) — it would desync the next API batch's pairing. Clear it.
            if (outbox != nullptr) {
                outbox->meta.clear();
            }
            return;
        }

        // One batch per update, decided or not: every drained command is recorded in its queue order.
        std::vector<FBattleAction> requests = std::move(queue->pending);
        queue->pending.clear();

        // P6-1 pairing: 1:1 only when the parallel FIFO holds exactly one meta per command; any other size
        // — direct pushes mixed in, a lost drain — marks the whole batch Unknown and never guesses by
        // index. The outbox is consumed either way.
        std::vector<FCommandMeta> metas;
        if (outbox != nullptr) {
            if (outbox->meta.size() == requests.size()) {
                metas = std::move(outbox->meta);
            }
            outbox->meta.clear();
        }

        for (std::size_t index = 0; index < requests.size(); ++index) {
            const FBattleAction& request = requests[index];
            const FCommandMeta meta = index < metas.size() ? metas[index] : FCommandMeta{};
            FRejection rejection;
            if (decided) {
                // Drain (P6-3): a decided battle consumes its queue without resolving anything and without
                // writing a single FCombatLog line — the pre-WP-06 texts are the only drama in the feed.
                rejection = FRejection{ECommandReject::OutcomeResolved, kOutcomeResolvedReason};
            } else {
                rejection = std::visit([&](const auto& typed) -> FRejection {
                    const auto* runtime = registry.ctx().find<FTurnRuntime>();
                    const auto snapshot = runtime ? runtime->controller.snapshot() : FTurnSnapshot{};
                    if (!snapshot.currentTurn || typed.token != *snapshot.currentTurn) {
                        return Reject(log, ECommandReject::StaleTurn, "Not your turn: stale action rejected.");
                    }
                    if (typed.actor != typed.token.actor) {
                        // Same text as a stale turn on purpose: the player gets one "not your turn" reason.
                        return Reject(log, ECommandReject::NotActor, "Not your turn: stale action rejected.");
                    }
                    if constexpr (std::is_same_v<std::decay_t<decltype(typed)>, FMoveRequest>) {
                        return Move(registry, typed, log);
                    } else {
                        return Resolve(registry, typed, *content, log);
                    }
                }, request);
            }
            if (results != nullptr) {
                RecordCommandResult(*results, CommandSessionId(request),
                    FCommandResult{FBattleCommand{request, meta.source, meta.seq}, rejection.code, rejection.reason});
            }
        }
    }

    [[nodiscard]] std::string name() const { return "ActionResolverSystem"; }

private:
    /// One drained command's outcome: the standardized code plus the exact player-facing text the
    /// pre-WP-06 log line carried. Acceptance is {None, ""}. Every reject site MUST name its code here —
    /// a site without one cannot compile.
    struct FRejection {
        ECommandReject code{ECommandReject::None};
        std::string reason;
    };

    static FRejection Reject(FCombatLog* log, ECommandReject code, std::string reason) {
        if (log != nullptr) {
            log->push(reason);
        }
        return FRejection{code, std::move(reason)};
    }

    static FRejection Move(entt::registry& registry, const FMoveRequest& request, FCombatLog* log) {
        auto* points = registry.try_get<FActionPoints>(request.actor);
        auto* cell = registry.try_get<FGridPosition>(request.actor);
        auto* position = registry.try_get<FPosition>(request.actor);
        const auto* grid = registry.ctx().find<FGridMetrics>();
        const auto* terrain = registry.ctx().find<FGridTerrain>();
        if (!IsAlive(registry, request.actor) || !points || !cell || !position || !grid) {
            return Reject(log, ECommandReject::Unavailable, "Cannot move: unit or grid is unavailable.");
        }
        if (points->move <= 0) {
            return Reject(log, ECommandReject::NoAp, "No movement AP remaining.");
        }
        const auto path = FindGridPath(registry, request.actor, request.destination, terrain ? terrain->moveRange : 1);
        if (path.empty()) {
            // P6-4: FindGridPath reports wall, occupancy and range as one empty result — there is no
            // separate "Occupied" code, the distinction cannot be made from here.
            return Reject(log, ECommandReject::Blocked, "Blocked: wall, occupied cell or movement range.");
        }
        --points->move;
        std::vector<FGridPosition> walked{*cell};
        for (const auto step : path) {
            *cell = step;
            walked.push_back(step);
            const auto world = GridCellTopLeftToWorld(*grid, cell->x, cell->y);
            *position = FPosition{world.x, world.y};
            if (terrain && terrain->at(cell->x, cell->y) == ETile::Trap) {
                const bool knockedOut = ApplyDamageToUnit(registry, request.actor, terrain->trapDamage);
                if (log) log->push(UnitDisplayName(registry, request.actor) + " crossed spikes: -" +
                                   std::to_string(terrain->trapDamage) + " HP.");
                if (knockedOut) break;
            }
        }
        if (log) log->push(UnitDisplayName(registry, request.actor) + " moved.");
        Emit(registry, FBattleEvent{.kind = FBattleEvent::EKind::Move, .actor = request.actor, .path = std::move(walked)});
        return FRejection{};
    }

    static void Emit(entt::registry& registry, FBattleEvent event) {
        if (auto* events = registry.ctx().find<FBattleEvents>()) events->pending.push_back(std::move(event));
    }

    static FRejection Resolve(entt::registry& registry, const FActionRequest& request,
                              const gameplay::FContentRegistry& content, FCombatLog* log) {
        const entt::entity actor = request.actor;
        if (!IsAlive(registry, actor)) {
            return Reject(log, ECommandReject::Dead, "Cannot act: knocked out.");
        }

        const auto* loadout = registry.try_get<FSkillLoadout>(actor);
        auto* points = registry.try_get<FActionPoints>(actor);
        const auto* stats = registry.try_get<FStats>(actor);
        const auto* actorCell = registry.try_get<FGridPosition>(actor);
        if (loadout == nullptr || points == nullptr || stats == nullptr || actorCell == nullptr) {
            return Reject(log, ECommandReject::Unavailable, "Cannot act: missing combat components.");
        }

        const FSkillSlot* slot = nullptr;
        for (const FSkillSlot& candidate : loadout->slots) {
            if (candidate.skillId == request.skillId) {
                slot = &candidate;
                break;
            }
        }
        if (slot == nullptr) {
            return Reject(log, ECommandReject::UnknownSkill, UnitDisplayName(registry, actor) + ": unknown skill.");
        }

        const auto resolved = ResolveSkillSlot(*slot, content);
        if (!resolved) {
            return Reject(log, ECommandReject::SkillResolutionFailed,
                          UnitDisplayName(registry, actor) + ": skill resolution failed.");
        }

        const bool cellTarget = resolved->target == "cell";
        if (!cellTarget && !IsAlive(registry, request.target)) {
            return Reject(log, ECommandReject::BadTarget, UnitDisplayName(registry, actor) + ": no living target.");
        }
        const bool targetIsParty = registry.all_of<FPartyMember>(request.target);
        // Target sides are read from the CASTER's perspective: "enemy"/"cell" hit the other side,
        // "ally"/"self" hit the caster's own side. This is what lets an enemy use the same strike skill.
        const bool casterIsParty = registry.all_of<FPartyMember>(actor);
        const bool targetsOwnSide = resolved->target == "ally" || resolved->target == "self";
        const bool affectsParty = targetsOwnSide ? casterIsParty : !casterIsParty;
        if (!cellTarget && affectsParty != targetIsParty) {
            return Reject(log, ECommandReject::WrongFaction, "Wrong target faction for this skill.");
        }
        if (resolved->target == "self" && request.target != actor) {
            return Reject(log, ECommandReject::BadTarget, "This skill can only target its caster.");
        }

        const auto* targetCell = cellTarget ? &request.cell : registry.try_get<FGridPosition>(request.target);
        const auto* metrics = registry.ctx().find<FGridMetrics>();
        if (!metrics || !targetCell || !IsInsideGrid(*metrics, targetCell->x, targetCell->y)) {
            return Reject(log, ECommandReject::OutsideGrid, "Target is outside the battlefield.");
        }
        if (targetCell == nullptr || !IsInRange(*actorCell, *targetCell, resolved->range)) {
            return Reject(log, ECommandReject::OutOfRange,
                          UnitDisplayName(registry, actor) + ": target out of range.");
        }
        if (!HasGridLineOfSight(registry, *actorCell, *targetCell)) {
            return Reject(log, ECommandReject::NoLos, "Line of sight blocked by stone.");
        }

        // 프리뷰 = 실제 계산(WP-03): 갱신된 컴포넌트로 같은 함수 경로(ComputeSkillDamage/Heal + 같은
        // 대상 규칙)를 한 번만 돌린다. 부작용이 없어(RNG·상태 변경 없음) AP 차감 전에 두고, 실패는
        // AP를 소모하지 않는다. 적용·로그는 아래에서 이 결과를 그대로 쓴다 — 두 번째 계산이 없다.
        const auto preview = PreviewSkill(registry, actor, *resolved, *targetCell, *actorCell);
        if (!preview) {
            return Reject(log, ECommandReject::PreviewFailed,
                          UnitDisplayName(registry, actor) + ": skill preview failed.");
        }

        if (points->skill < resolved->apCost) {
            return Reject(log, ECommandReject::NoAp, UnitDisplayName(registry, actor) + ": not enough skill AP ("
                + std::to_string(points->skill) + "/" + std::to_string(resolved->apCost) + ")");
        }
        points->skill -= resolved->apCost;

        const std::string actorName = UnitDisplayName(registry, actor);
        const gameplay::FSkillContent* skillRow = content.skills.find(slot->skillId);
        const std::string skillName = skillRow != nullptr ? skillRow->displayName : slot->skillId;

        // Shape semantics live in gameplay/combat so the resolver, the HUD and later slices share one
        // definition of what a skill affects (docs/design/skill-rune-progression.md §3.2).
        FBattleEvent event{.kind = FBattleEvent::EKind::Skill, .actor = actor, .skillId = slot->skillId,
                           .aim = *targetCell, .cells = preview->cells};
        for (const FPreviewTarget& target : preview->targets) {
            const std::string targetName = UnitDisplayName(registry, target.unit);
            const auto* healthBefore = registry.try_get<FHealth>(target.unit);
            FBattleEvent::FTarget reached{.unit = target.unit, .healthBefore = healthBefore ? healthBefore->current : 0,
                                          .hostile = !targetsOwnSide};
            bool died = false;

            if (targetsOwnSide) {
                // Zero-power support skills apply statuses without also becoming heals.
                if (auto* health = registry.try_get<FHealth>(target.unit); health && target.heal > 0) {
                    const int before = health->current;
                    health->current = std::min(health->max, health->current + target.heal);
                    if (log && health->current > before) {
                        log->push(actorName + " / " + skillName + " -> " + targetName + " +"
                                  + std::to_string(health->current - before) + " HP");
                    }
                }
            } else if (resolved->power > 0) {
                // §1.1: 감소 항도 damage_type이 고른다(물리: 방어력, 마법: 마법방어력).
                died = ApplyDamageToUnit(registry, target.unit, target.damage);
                if (log) {
                    std::string line = actorName + " / " + skillName + " -> " + targetName + " -"
                        + std::to_string(target.damage) + " HP";
                    if (const auto* health = registry.try_get<FHealth>(target.unit)) {
                        line += " (" + std::to_string(health->current) + "/" + std::to_string(health->max) + ")";
                    }
                    // 계산 근거: 프리뷰가 계산에 쓴 항 그대로(atk X + pwr Y - def Z = N). ComputeDamage의
                    // 하한(최소 1)이 걸려 원식과 결과가 다를 때만 (min)을 붙인다 — 로그는 실제 산술을 보여준다.
                    line += " [atk " + std::to_string(target.offense) + " + pwr " + std::to_string(target.power)
                        + " - def " + std::to_string(target.defense) + " = " + std::to_string(target.damage);
                    if (target.offense + target.power - target.defense < target.damage) line += " (min)";
                    line += "]";
                    if (died) line += " / KO";
                    log->push(std::move(line));
                    if (died) {
                        log->push(targetName + " knocked out!");
                    }
                }
                // §3.3 흡혈은 WP-04에서 훅으로 이관: FSkillResolver가 on_hit -> heal_self 훅을 합성하고
                // FActiveHooks가 OnHit에 발화한다. 여기서 직접 회복하지 않는다(이중 회복 방지).
                // WP-04 §3: typed hooks fire per hit in a fixed order — OnHit(attacker) first, then either
                // OnKill (the target died) or OnDamageTaken (the victim survived). Hooks can kill or heal
                // either side, so `died` is recomputed from IsAlive after the dispatch and the status
                // application below is skipped for a target dead at that point. Trap and status-tick
                // damage never reach this branch, so they can never fire these events.
                const FHookContext hit{.owner = actor, .other = target.unit, .amount = target.damage,
                                       .skillId = slot->skillId};
                (void)FireHooks(registry, gameplay::EHookEvent::OnHit, hit);
                const bool deadAfterHooks = !IsAlive(registry, target.unit);
                const FHookContext reaction{.owner = deadAfterHooks ? actor : target.unit,
                                            .other = deadAfterHooks ? target.unit : actor,
                                            .amount = target.damage, .skillId = slot->skillId};
                (void)FireHooks(registry,
                                deadAfterHooks ? gameplay::EHookEvent::OnKill : gameplay::EHookEvent::OnDamageTaken,
                                reaction);
                if (deadAfterHooks && !died && log) {
                    log->push(targetName + " knocked out!"); // a hook finished the target after the hit
                }
                died = deadAfterHooks;
            } else if (log && !resolved->statuses.empty()) {
                log->push(actorName + " / " + skillName + " -> " + targetName + " status applied");
            }

            reached.died = died;
            const auto* healthAfter = registry.try_get<FHealth>(target.unit);
            reached.healthAfter = healthAfter ? healthAfter->current : reached.healthBefore;
            event.targets.push_back(reached);
            if (died) {
                continue;
            }
            for (const FResolvedStatus& status : target.statuses) {
                const gameplay::FStatusContent* definition = content.statuses.find(status.statusId);
                if (definition == nullptr) continue;
                if (auto* active = registry.try_get<FActiveStatuses>(target.unit)) {
                    ApplyStatus(*active, *definition, status.durationDelta, status.magnitudeDelta);
                    // WP-06 (P6-6, ★3): the application can change what the sheet derives (WP-13's
                    // speed_pct reads it on the next collect). Ember-only guard: forest passives are added
                    // inline in FBattleState and a refresh would flatten them back to base (FCombatSheet.h).
                    if (registry.ctx().contains<FDungeonRun>()) {
                        RefreshCombatSheet(registry, target.unit, content);
                    }
                }
            }
        }
        Emit(registry, std::move(event));
        return FRejection{};
    }
};

} // namespace game::ecs
