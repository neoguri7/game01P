#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FDead.h"
#include "ecs/components/FHealth.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/dungeon/FDungeonRun.h"

namespace game {

/// WP-15: one phase of a multi-stage boss, copied from bosses.json at battle setup (the component is
/// self-contained — no catalog lookup at query time). Phases are ordered by `hpAbove` descending; the last
/// phase has `hpAbove == 0` (loader-enforced).
struct FBossPhase {
    int hpAbove{0};       ///< % of max HP; the phase is left once HP <= ceil(max * hpAbove / 100)...
    bool partsGate{false}; ///< ...and, when gated, only after every part is dead (gate "parts").
    std::vector<std::string> bodyImmunities; ///< status ids the body ignores while this phase is active
    std::string notes;
};

/// The boss body (the unit `bosses.json` names). Attached by SetupCombatUnits only in a mapped boss slot
/// (FBossBattle + catalog entry). A fallback boss (unmapped slot) gets no FBossGoals: single phase, no gate.
struct FBossGoals {
    std::string bossId;
    std::string name;
    std::vector<FBossPhase> phases;
    /// The phase EvaluateBossGoals last announced; -1 = not evaluated yet (the first evaluation logs the
    /// gate entry, later changes log "<name> enters phase N"). Rules never read this cache — they derive
    /// the live phase (BossActivePhase) so no frame order can make them disagree.
    int activePhase{-1};
    bool gateBlockLogged{false}; ///< the "first blocked hit" line is written once per battle
};

/// A boss part: an ordinary enemy unit whose life gates its body.
struct FBossPart {
    entt::entity body{entt::null};
};

/// Registry-context marker of a boss-slot battle (set by BuildDungeonBattlefield for a boss slot, erased
/// for every other battle). `bossId` = the bosses.json id, or `generation.boss_unit` for an unmapped slot.
/// `slot` = "mid" | "final". The BossDefeated producer (CombatOutcomeSystem) reads it.
struct FBossBattle {
    std::string bossId;
    std::string slot;
};

// --- gate rules (shared by preview = canon, ApplyDamageToUnit = safety net, AI value) ---------------

/// The HP floor of a phase threshold: ceil(max * hpAbove / 100) (110 / 50% -> 55; ceil keeps it >= 1 for
/// any hpAbove > 0).
[[nodiscard]] inline int BossPhaseFloor(int maxHealth, int hpAbove) {
    return (std::max(0, maxHealth) * std::max(0, hpAbove) + 99) / 100;
}

[[nodiscard]] inline bool BossUnitAlive(const entt::registry& registry, entt::entity unit) {
    if (!registry.valid(unit) || registry.all_of<FDead>(unit)) return false;
    const auto* health = registry.try_get<FHealth>(unit);
    return health != nullptr && health->current > 0;
}

/// Living parts linked to `body`.
[[nodiscard]] inline int BossPartsAlive(const entt::registry& registry, entt::entity body) {
    int alive = 0;
    for (const auto [entity, part] : registry.view<const FBossPart>().each()) {
        if (part.body == body && BossUnitAlive(registry, entity)) ++alive;
    }
    return alive;
}

/// The live phase index of a body (pure — HP + parts only): the first phase whose exit condition is not
/// met. Exit = HP <= floor && (!gated || no part alive). At the floor a gated phase therefore holds while a
/// part lives and is left the moment the last part dies. -1 for an entity without FBossGoals/FHealth.
[[nodiscard]] inline int BossActivePhase(const entt::registry& registry, entt::entity body) {
    const auto* goals = registry.try_get<FBossGoals>(body);
    const auto* health = registry.try_get<FHealth>(body);
    if (goals == nullptr || health == nullptr || goals->phases.empty()) return -1;
    const int partsAlive = BossPartsAlive(registry, body);
    for (std::size_t i = 0; i < goals->phases.size(); ++i) {
        const FBossPhase& phase = goals->phases[i];
        const bool belowFloor = health->current <= BossPhaseFloor(health->max, phase.hpAbove);
        const bool exited = belowFloor && (!phase.partsGate || partsAlive == 0);
        if (!exited) return static_cast<int>(i);
    }
    return static_cast<int>(goals->phases.size()) - 1;
}

/// The floor the gate holds the body at, or nullopt when no gate holds. Holds when: the body has
/// FBossGoals with a gated phase (<= 1, loader-enforced) && at least one part lives && an FDungeonRun exists
/// && its phase is Battle. The last condition skips the saved-phase continue (FEmberBattleState
/// `continuedAfterRoom` kill-all) so a cleared boss room never shows a surviving body.
[[nodiscard]] inline std::optional<int> BossGateFloor(const entt::registry& registry, entt::entity body) {
    const auto* goals = registry.try_get<FBossGoals>(body);
    const auto* health = registry.try_get<FHealth>(body);
    if (goals == nullptr || health == nullptr) return std::nullopt;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr || run->phase != EDungeonPhase::Battle) return std::nullopt;
    const auto gated = std::ranges::find_if(goals->phases, &FBossPhase::partsGate);
    if (gated == goals->phases.end() || BossPartsAlive(registry, body) == 0) return std::nullopt;
    return BossPhaseFloor(health->max, gated->hpAbove);
}

/// The damage that really lands on `target`: min(incoming, max(0, current - floor)) while the gate holds,
/// `incoming` otherwise (and for every non-boss unit). Applied AFTER ComputeDamage's minimum-1 rule, so a
/// gated hit at the floor is a real 0. Pure — preview, AI and execution share it.
[[nodiscard]] inline int BossBodyDamageCap(const entt::registry& registry, entt::entity target, int incoming) {
    const auto floor = BossGateFloor(registry, target);
    if (!floor) return incoming;
    const int current = registry.get<FHealth>(target).current;
    return std::min(incoming, std::max(0, current - *floor));
}

/// Execution-side cap (ApplyDamageToUnit's safety net): BossBodyDamageCap plus the one-per-battle
/// "first blocked hit" line, written when a hit lands while the gate holds the body at its floor.
[[nodiscard]] inline int ApplyBossGateToHit(entt::registry& registry, entt::entity target, int incoming) {
    const auto floor = BossGateFloor(registry, target);
    if (!floor) return incoming;
    const int current = registry.get<FHealth>(target).current;
    const int landed = std::min(std::max(0, incoming), std::max(0, current - *floor));
    auto& goals = registry.get<FBossGoals>(target);
    if (current - landed <= *floor && !goals.gateBlockLogged) {
        goals.gateBlockLogged = true;
        const std::string line = goals.name + " is shielded: destroy its parts to break the guard.";
        if (auto* log = registry.ctx().find<FCombatLog>()) {
            log->push(line);
        } else {
            LOG_INFO("[boss] {}", line);
        }
    }
    return landed;
}

} // namespace game
