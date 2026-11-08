#pragma once

#include <string>

#include <entt/entt.hpp>

#include "core/Logger.h"
#include "ecs/components/FBossGoals.h"
#include "gameplay/combat/FCombatTypes.h"

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

} // namespace detail

/// WP-15 evaluation helper — deliberately NOT a registered ISystem (CombatOutcomeSystem calls it before its
/// `resolved` guard, so the frame that resolves the battle is evaluated too). Derives every boss body's live
/// phase (BossActivePhase — HP + parts only) and announces each phase exactly once:
///  - the first evaluation caches the phase; if the parts gate holds it writes the gate-entry line;
///  - a later change caches the new phase and, while the body is alive, writes "<name> enters phase K."
///    A killing blow only updates the cache (no "enters phase" for the blow that ends the fight).
/// The cached `activePhase` is never read by rules — the gate and the phase functions derive the live phase,
/// so no frame order can make two readers disagree.
inline void EvaluateBossGoals(entt::registry& registry) {
    for (const entt::entity body : registry.view<FBossGoals>()) {
        auto& goals = registry.get<FBossGoals>(body);
        const int phase = BossActivePhase(registry, body);
        if (phase < 0) continue;  // no FHealth/phases — nothing to announce

        if (goals.activePhase < 0) {
            goals.activePhase = phase;
            if (const auto floor = BossGateFloor(registry, body)) {
                detail::AnnounceBossLine(
                    registry,
                    goals.name + " is guarded by " + std::to_string(BossPartsAlive(registry, body)) +
                        " part(s): it cannot fall below " + std::to_string(*floor) +
                        " HP while they stand.");
            }
            continue;
        }

        if (phase == goals.activePhase) continue;
        goals.activePhase = phase;
        if (BossUnitAlive(registry, body)) {
            detail::AnnounceBossLine(registry, goals.name + " enters phase " + std::to_string(phase + 1) + ".");
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
