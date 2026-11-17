#include "gameplay/combat/FEngineIgnition.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunLog.h"

namespace game {
namespace {

/// find-or-emplace: entt 3.16's ctx has no find_or_emplace (CONSTRAINTS §49).
[[nodiscard]] FEngineStats& EnsureEngineStats(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<FEngineStats>()) {
        return *existing;
    }
    return registry.ctx().emplace<FEngineStats>();
}

/// The slot the resolver would act through: the FIRST slot naming the skill (ActionResolverSystem's own
/// rule). The engine reads the runes of that same slot, so the combo is "this slot's build", never the
/// union of the whole loadout.
[[nodiscard]] const FSkillSlot* FirstSkillSlot(const FSkillLoadout* loadout, const std::string& skillId) {
    if (loadout == nullptr) {
        return nullptr;
    }
    for (const FSkillSlot& slot : loadout->slots) {
        if (slot.skillId == skillId) {
            return &slot;
        }
    }
    return nullptr;
}

/// `slot.runeIds ⊇ requires_runes`. requires_runes is non-empty at boot, so the loop is the whole check.
[[nodiscard]] bool SlotSocketsAll(const FSkillSlot& slot, const std::vector<std::string>& required) {
    for (const std::string& runeId : required) {
        if (std::ranges::find(slot.runeIds, runeId) == slot.runeIds.end()) {
            return false;
        }
    }
    return true;
}

/// The target's PRE-HIT statuses must carry the trigger at `min_magnitude`. Freeze is authored with
/// magnitude 0 and min_magnitude defaults to 0, so presence alone satisfies it (W6 ★6: forcing >= 1 would
/// make freeze permanently false). 중첩 = 갱신 keeps one entry per status id, so one match is all there is.
[[nodiscard]] bool StatusTriggerSatisfied(const FActiveStatuses* active, const gameplay::FEngineContent& engine) {
    if (active == nullptr) {
        return false;
    }
    for (const FStatusEffect& effect : active->effects) {
        if (effect.statusId == engine.triggerStatusId && effect.magnitude >= engine.minMagnitude) {
            return true;
        }
    }
    return false;
}

/// The current 1-based round through WP-19a's gameplay-side battle mirror — NOT the turn runtime's
/// `roundNumber`: gameplay code must not reach into system headers (pinned by battle_flow_tests), and
/// FRoomBattleStats counts the current session's FRoundStarted events, which is that same number. An
/// absent (headless) or inactive (no battle running) mirror reports 0 — the schema key is still written.
[[nodiscard]] int CurrentRound(const entt::registry& registry) {
    const auto* stats = registry.ctx().find<FRoomBattleStats>();
    return (stats != nullptr && stats->active) ? stats->rounds : 0;
}

/// One firing: journal line + run log line. Without a run log (headless test, module not set up)
/// LogRunEvent is a harmless no-op; without FCombatLog only the journal line is skipped.
void EmitIgnition(entt::registry& registry, entt::entity actor, const gameplay::FEngineContent& engine) {
    if (auto* log = registry.ctx().find<FCombatLog>()) {
        log->push(UnitDisplayName(registry, actor) + ": " + engine.log);
    }
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) {
        return; // CheckEngineIgnition's guard already covers this; defends direct/forgotten callers
    }
    // WP-19a: exactly one line per firing — depth/room/round/engine. No accessor, no stored state.
    LogRunEvent(registry, nlohmann::json{{"t", "engine_ignition"},
                                         {"depth", run->depth},
                                         {"room", run->room},
                                         {"round", CurrentRound(registry)},
                                         {"engine", engine.id}});
}

} // namespace

int CheckEngineIgnition(entt::registry& registry, entt::entity actor, entt::entity target,
                        const gameplay::FContentRegistry& content, const std::string& skillId) {
    // Ignition is a RUN feature: the seed guard needs run.seed for "once per run". Without a run there is
    // nothing to ignite once-per — return before touching FEngineStats.
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) {
        return 0;
    }
    // ① Party owner only. OnHit fires for hostile power>0 hits, so the enemy's own attacks reach the
    // listener too — they must never ignite (WP-23's rune-carrying elites stay silent).
    if (!registry.all_of<FPartyMember>(actor)) {
        return 0;
    }
    // ② The acting slot (the resolver's first-matching-slot rule).
    const FSkillSlot* slot = FirstSkillSlot(registry.try_get<FSkillLoadout>(actor), skillId);
    if (slot == nullptr) {
        return 0;
    }
    const FActiveStatuses* active = registry.try_get<FActiveStatuses>(target);

    // ⑤ Seed guard: a different seed is a new run — reset, then adopt. The same seed is a Continue and
    // keeps the firing record (FEmberBattleState creates a fresh seed per run; the save restores it).
    FEngineStats& stats = EnsureEngineStats(registry);
    if (stats.runSeed != run->seed) {
        stats.runSeed = run->seed;
        stats.ignited.clear();
    }

    int ignited = 0;
    for (const gameplay::FEngineContent& engine : content.engines.rows) {
        if (std::ranges::find(stats.ignited, engine.id) != stats.ignited.end()) {
            continue; // already fired this run: a refresh/re-application is not a new ignition
        }
        if (!SlotSocketsAll(*slot, engine.requiresRunes)) {
            continue; // ③
        }
        if (!StatusTriggerSatisfied(active, engine)) {
            continue; // ④
        }
        stats.ignited.push_back(engine.id);
        EmitIgnition(registry, actor, engine);
        ++ignited;
    }
    return ignited;
}

void RegisterEngineIgnitionListener(entt::registry& registry) {
    // Ordering assumption (FEffectHooks::FireHooks): the owner's DATA hooks are applied before the code
    // listeners run, and the resolver fires OnHit AFTER damage but BEFORE the skill's statuses land — so
    // the target's FActiveStatuses read here is still the pre-hit state, which is the design's whole
    // point. No shipped OnHit data hook applies a status (only push and the synthesized leech hook do);
    // if a future hook ever applies one, revisit this seam.
    RegisterHookListener(registry, gameplay::EHookEvent::OnHit,
                         [](entt::registry& r, const FHookContext& context) {
                             const auto* content = r.ctx().find<gameplay::FContentRegistry>();
                             if (content == nullptr) {
                                 return;
                             }
                             (void)CheckEngineIgnition(r, context.owner, context.other, *content, context.skillId);
                         });
}

} // namespace game
