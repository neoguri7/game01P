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
    /// Telegraph contract (WP-15 §전이): defense/rule changes (immune set, gate floor, phase index) apply at the
    /// transition; ACTION changes (phase skill choice, AI guard) apply from the next round. `phaseRound` is the
    /// room round counter (FRoomBattleStats::rounds) when `activePhase` last changed; EvaluateBossGoals promotes
    /// `behaviorPhase = activePhase` once the counter has moved past it. The AI leaf BossPhaseAtLeast reads
    /// `behaviorPhase`, never the live phase — a committed or same-round action is never re-aimed.
    int behaviorPhase{0};
    int phaseRound{0};
    /// The first part row's display name (bosses.json `parts[0].name`), named in the player-facing gate
    /// guidance. Empty for a boss without parts.
    std::string partName;
};

// --- player-facing boss lines (WP-15c) -----------------------------------------------------------

/// The battle feed shows ONE line in a fixed-width box: headless captures at 1280x800 and 1920x1080 show 36
/// characters of average text before the edge clips. Every player-facing boss line puts the action first and
/// stays within this budget (2 characters of margin for wide glyphs) with the shipped names; the numbers (floor
/// HP) go to the dev log only.
inline constexpr std::size_t kBossFeedLineBudget = 34;

/// "Guardian heart x3" — the part named with how many still stand ("its parts" without a part name).
[[nodiscard]] inline std::string BossPartsLabel(const FBossGoals& goals, int partsAlive) {
    if (goals.partName.empty()) return "its parts";
    return goals.partName + " x" + std::to_string(partsAlive);
}

/// Battle start, while the gate holds: "Break Guardian heart x3 first!".
[[nodiscard]] inline std::string BossGateEntryLine(const FBossGoals& goals, int partsAlive) {
    return "Break " + BossPartsLabel(goals, partsAlive) + " first!";
}

/// The first hit the gate blocks: "Shielded! Break Guardian heart x2".
[[nodiscard]] inline std::string BossGateBlockedLine(const FBossGoals& goals, int partsAlive) {
    return "Shielded! Break " + BossPartsLabel(goals, partsAlive);
}

/// A phase transition: "Stone Guardian: Core exposed" when the phase has notes, else
/// "Stone Guardian enters phase 2." (`phase` is the 0-based index).
[[nodiscard]] inline std::string BossPhaseLine(const FBossGoals& goals, int phase) {
    const auto& notes = goals.phases[static_cast<std::size_t>(phase)].notes;
    const std::string number = std::to_string(phase + 1);
    return notes.empty() ? goals.name + " enters phase " + number + "." : goals.name + ": " + notes;
}

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
        const std::string line = BossGateBlockedLine(goals, BossPartsAlive(registry, target));
        if (auto* log = registry.ctx().find<FCombatLog>()) {
            log->push(line);
        } else {
            LOG_INFO("[boss] {}", line);
        }
    }
    return landed;
}

} // namespace game
