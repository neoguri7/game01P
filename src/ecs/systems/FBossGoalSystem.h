#pragma once

#include <string>

#include <entt/entt.hpp>

#include "core/Logger.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBossGoals.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/dungeon/FRunLog.h"

namespace game::ecs {
namespace detail {

/// One boss line: to the battle feed when the battle has one (its push mirrors the dev log), else straight
/// to the dev log (validation probes, tests).
inline void AnnounceBossLine(entt::registry& registry, const std::string& line) {
    if (auto* log = registry.ctx().find<FCombatLog>()) {
        log->push(line);
    } else {
        LOG_INFO("[boss] {}", line);
    }
}

/// WP-15b: mirrors the body's phase immune set into FActiveStatuses::immune. Called whenever `activePhase`
/// is (re-)cached — defense changes (the immune set) apply at the transition, not a round later.
inline void WriteBossImmunities(entt::registry& registry, entt::entity body, const FBossGoals& goals, int phase) {
    auto* active = registry.try_get<FActiveStatuses>(body);
    if (active == nullptr) {
        return;
    }
    active->immune = goals.phases[static_cast<std::size_t>(phase)].bodyImmunities;
}

} // namespace detail

/// WP-15 evaluation helper — deliberately NOT a registered ISystem (CombatOutcomeSystem calls it before its
/// `resolved` guard, so the frame that resolves the battle is evaluated too). Derives every boss body's live
/// phase (BossActivePhase — HP + parts only) and announces each phase exactly once:
///  - the first evaluation caches the phase; if the parts gate holds it writes the gate-entry line;
///  - a later change caches the new phase and, while the body is alive, writes "<name> enters phase K."
///    A killing blow only updates the cache (no "enters phase" for the blow that ends the fight).
/// The cached `activePhase` is never read by rules — the gate and the phase functions derive the live phase,
/// so no frame order can make two readers disagree.
/// WP-15b adds the transition's staged effects: every (re-)cache writes the body's immune set (defense
/// applies immediately) and records the room round of the transition; the action-side cache `behaviorPhase`
/// is promoted only once the round counter has moved past that round (telegraph contract — a committed or
/// same-round action is never re-aimed).
inline void EvaluateBossGoals(entt::registry& registry) {
    const auto* stats = registry.ctx().find<FRoomBattleStats>();
    const int rounds = stats != nullptr ? stats->rounds : 0; // no run stats (probes) = no round boundary yet
    for (const entt::entity body : registry.view<FBossGoals>()) {
        auto& goals = registry.get<FBossGoals>(body);
        const int phase = BossActivePhase(registry, body);
        if (phase < 0) continue;  // no FHealth/phases — nothing to announce

        if (goals.activePhase < 0) {
            goals.activePhase = phase;
            goals.phaseRound = rounds;
            detail::WriteBossImmunities(registry, body, goals, phase);
            if (const auto floor = BossGateFloor(registry, body)) {
                // WP-15c: the feed shows one short line (action first, names the part); the numbers go to the
                // dev log only.
                const int partsAlive = BossPartsAlive(registry, body);
                detail::AnnounceBossLine(registry, BossGateEntryLine(goals, partsAlive));
                LOG_INFO("[boss] {} gate: {} part(s) alive, HP floor {}", goals.bossId, partsAlive, *floor);
            }
        } else if (phase != goals.activePhase) {
            goals.activePhase = phase;
            goals.phaseRound = rounds;
            detail::WriteBossImmunities(registry, body, goals, phase);
            if (BossUnitAlive(registry, body)) {
                detail::AnnounceBossLine(registry, BossPhaseLine(goals, phase));
            }
        }

        // WP-15b telegraph contract: ACTION changes (phase skill choice, the AI phase guard) apply from the
        // NEXT round — promote once the room round counter has moved past the transition's round. Absent run
        // stats stay 0, so a probe never promotes on a cached transition alone.
        if (goals.behaviorPhase < goals.activePhase && rounds > goals.phaseRound) {
            goals.behaviorPhase = goals.activePhase;
        }
    }
}

/// The WP-15 victory condition: every boss body is dead and none of its parts lives. True when the battle
/// has no FBossGoals at all (an ordinary fight).
[[nodiscard]] inline bool BossGoalsComplete(const entt::registry& registry) {
    for (const entt::entity body : registry.view<const FBossGoals>()) {
        if (BossUnitAlive(registry, body)) return false;
        if (BossPartsAlive(registry, body) > 0) return false;
    }
    return true;
}

} // namespace game::ecs
