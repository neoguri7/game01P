#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/events/FEventBus.h"
#include "ecs/components/FBossGoals.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/systems/FBossGoalSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/factory/FEntityDefinitions.h"

namespace game::ecs {

/// Ends the battle the moment one side has no living unit left. The turn session is stopped and the
/// result is written once into FBattleOutcome (the HUD shows it and offers the way back to the hub).
struct CombatOutcomeSystem {
    void update(entt::registry& registry, float /*deltaTime*/) {
        // WP-19a: accumulate first, so the resolving frame's last damage/round/command is counted too. The
        // stats live in the gameplay layer (FRunLog.h) because gameplay must not include this header.
        AccumulateRoomStats(registry);

        // WP-15: the boss evaluation also sits before the resolved guard — the frame that lands the killing
        // blow must announce the transition it caused (a killing blow changes the cache silently), and a
        // continued room must still refresh the cache it never announces.
        EvaluateBossGoals(registry);

        auto* outcome = registry.ctx().find<FBattleOutcome>();
        if (!outcome || outcome->resolved) {
            return;
        }

        // No battle at all (e.g., setup failed) is not a result.
        const int party = AliveCount(registry, true);
        const int enemies = AliveCount(registry, false);
        if (party + enemies == 0 || (party > 0 && enemies > 0)) {
            return;
        }

        // WP-15: a boss room is won only when every goal is complete (every body dead, no part left alive).
        // Defensive while parts/body are enemies — "enemies == 0" already implies it — but the rule is
        // explicit here so a future non-enemy part cannot silently end the fight.
        if (enemies == 0 && !BossGoalsComplete(registry)) {
            return;
        }

        outcome->resolved = true;
        outcome->victory = enemies == 0;
        if (auto* log = registry.ctx().find<FCombatLog>()) {
            log->push(outcome->victory ? "Victory!" : "The party has fallen...");
        }
        if (auto* runtime = registry.ctx().find<FTurnRuntime>()) {
            runtime->requests.push_back(FStopTurnSession{});
        }

        // WP-04: a won room fires OnRoomClear once per living party member, in a deterministic order
        // (sort by entity id — there is no party slot component). The `continuedAfterRoom` path sets
        // `resolved` directly (FEmberBattleState), so this early-returns and nothing refires. The heals
        // land this frame, before the next frame's CaptureRunParty records the party HP.
        if (outcome->victory) {
            // WP-15/WP-20 cross contract: a won boss battle appends BossDefeated exactly once (this branch
            // runs once — `resolved` above and the `continuedAfterRoom` path that sets `resolved` itself both
            // keep it single). A fallback boss still appends (the FBossBattle marker carries its bossId).
            // A battle without FDungeonRun (test fixtures) must not append: AppendRunEvent creates a log when
            // none exists.
            if (registry.ctx().contains<FDungeonRun>()) {
                if (const auto* boss = registry.ctx().find<FBossBattle>()) {
                    AppendRunEvent(registry, ERunEventKind::BossDefeated, boss->bossId);
                }
            }

            std::vector<entt::entity> survivors;
            for (const entt::entity member : registry.view<FPartyMember>()) {
                if (IsAlive(registry, member)) {
                    survivors.push_back(member);
                }
            }
            std::ranges::sort(survivors, [](entt::entity left, entt::entity right) {
                return entt::to_integral(left) < entt::to_integral(right);
            });
            for (const entt::entity member : survivors) {
                (void)FireHooks(registry, gameplay::EHookEvent::OnRoomClear, FHookContext{.owner = member});
            }
        }
    }

    [[nodiscard]] std::string name() const { return "CombatOutcomeSystem"; }

private:
    /// One battle = one turn session. When a new session id appears the stats start over and snapshot the
    /// party's HP (ApplyRunParty already ran in the battle's OnEnter). Rounds are counted from the bus's
    /// frame events — never from `roundNumber`, which resets when a session finishes. Skills are counted
    /// from the accepted command record (moves have no skillId and never count).
    static void AccumulateRoomStats(entt::registry& registry) {
        if (!registry.ctx().contains<FBattleOutcome>()) return;
        auto* runtime = registry.ctx().find<FTurnRuntime>();
        if (runtime == nullptr) return;
        const FTurnSnapshot snapshot = runtime->controller.snapshot();
        auto* stats = registry.ctx().find<FRoomBattleStats>();
        if (stats == nullptr) stats = &registry.ctx().emplace<FRoomBattleStats>();
        if (snapshot.sessionId != 0 && snapshot.sessionId != stats->sessionId) {
            *stats = FRoomBattleStats{};
            stats->sessionId = snapshot.sessionId;
            stats->active = true;
            for (const entt::entity member : registry.view<FPartyMember, FUnitData, FHealth>()) {
                const auto& id = registry.get<FUnitData>(member).definition.id;
                const int current = registry.get<FHealth>(member).current;
                stats->hpStart.insert_or_assign(id, current);
                stats->hpMin.insert_or_assign(id, current);
            }
        }
        if (!stats->active) return;
        if (const auto* bus = registry.ctx().find<FEventBus>()) {
            for (const auto& round : bus->frameEvents<FRoundStarted>()) {
                if (round.snapshot.sessionId == stats->sessionId) ++stats->rounds;
            }
        }
        for (const entt::entity member : registry.view<FPartyMember, FUnitData, FHealth>()) {
            const auto& id = registry.get<FUnitData>(member).definition.id;
            const int current = registry.get<FHealth>(member).current;
            stats->hpStart.try_emplace(id, current);
            stats->hpMin.try_emplace(id, current);
            stats->hpMin[id] = std::min(stats->hpMin[id], current);
            if (current <= 0) stats->ko = true;
        }
        auto* results = registry.ctx().find<FCommandResults>();
        if (results == nullptr || results->sessionId != stats->sessionId) return;
        std::uint64_t highest = stats->lastSeq;
        for (const auto& result : results->recent) {
            const std::uint64_t seq = result.command.seq;
            if (seq == 0 || seq <= stats->lastSeq) continue;  // 0 = no provenance; <= cursor = already counted
            highest = std::max(highest, seq);
            if (result.reject != ECommandReject::None) continue;
            const auto* action = std::get_if<FActionRequest>(&result.command.action);
            if (action != nullptr && !action->skillId.empty()) ++stats->skillsUsed[action->skillId];
        }
        stats->lastSeq = highest;
    }
};

} // namespace game::ecs
