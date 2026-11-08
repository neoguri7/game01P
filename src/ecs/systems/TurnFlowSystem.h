#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FAutoAction.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FBattleFlow.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "presentation/FSettings.h"

namespace game::ecs {

/// AI think pause (WP-06 P6-10): armed at every turn start, counted down by real dt scaled by
/// FSettings::animSpeed (absent = 1x). autoPlay bypasses the wait entirely. Pinned by ThinkDelayScalesWithSpeed.
inline constexpr float kAiThinkDelaySeconds = 0.45f;

/// Per-turn bookkeeping the turn controller deliberately does not own: AP refill, the status tick at the
/// actor's turn start, and one auto action through gameplay/combat (FAutoAction). Player turns wait for
/// the HUD to queue an action.
struct TurnFlowSystem {
    void update(entt::registry& registry, float deltaTime) {
        auto* runtime = registry.ctx().find<FTurnRuntime>();
        auto* combat = registry.ctx().find<FCombatRuntime>();
        auto* outcome = registry.ctx().find<FBattleOutcome>();
        auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        if (!runtime || !combat || !outcome || !content || outcome->resolved) {
            return;
        }

        // WP-05b driver guard: the turn flow waits for the presentation to settle, exactly like TurnSystem
        // defers FEndTurn. Everything below is held while the gate is busy: the AP refill, the status tick,
        // OnTurnStart, the skipTurn FEndTurn, the think-delay countdown, the auto action and its cap, and
        // the stalemate resolution. `turnStarted` is not consumed while held either (scheduledTurnSerial is
        // below), so the first settled frame still runs the new turn's refill and tick exactly once.
        //
        // This placement cannot drop an OnTurnEnd:
        //  * a Completed FTurnEnded is only produced when TurnSystem (earlier in this same frame, 54) processes
        //    an FEndTurn — and 05a defers those while unsettled, so a busy frame produces none;
        //  * this system (55) reads the same gate (assigned later in the frame by BattleAnimationSystem, 57)
        //    and the same watch (advanced last by BattleFlowSystem) — the same settled value TurnSystem saw
        //    in that frame, so any frame that produced a Completed FTurnEnded reaches the consumption below;
        //  * Removed/Stopped turn ends can be produced while unsettled (synchronize, session stop), but
        //    FireTurnEndedHooks ignores them (FEffectHooks.cpp:392-396).
        if (!IsPresentationSettled(registry)) {
            return;
        }

        // WP-04: consume this frame's FTurnEnded events (TurnSystem queued them earlier in the same frame)
        // before the new turn's start hooks. Dedupe inside the helper keeps a second update in the same
        // frame from double-firing. The `outcome->resolved` guard above intentionally drops the final
        // turn's OnTurnEnd: a won battle is closed by OnRoomClear (CombatOutcomeSystem), not by a turn hook.
        if (const auto* bus = registry.ctx().find<FEventBus>()) {
            const std::vector<FTurnEnded>& ended = bus->frameEvents<FTurnEnded>();
            if (!ended.empty()) {
                std::vector<FTurnEvent> turnEvents;
                turnEvents.reserve(ended.size());
                for (const FTurnEnded& event : ended) {
                    turnEvents.emplace_back(event);
                }
                FireTurnEventHooks(registry, turnEvents);
            }
        }

        const FTurnSnapshot snapshot = runtime->controller.snapshot();
        if (snapshot.state != ETurnState::Running || !snapshot.currentTurn) {
            return;
        }

        const FTurnToken token = *snapshot.currentTurn;
        const bool turnStarted = token.turnSerial != combat->scheduledTurnSerial;
        combat->scheduledTurnSerial = token.turnSerial;
        if (turnStarted) {
            combat->autoActionsThisTurn = 0;
            combat->thinkDelay = kAiThinkDelaySeconds;
        }

        auto* log = registry.ctx().find<FCombatLog>();
        if (snapshot.roundNumber > kMaxBattleRounds) {
            // Stalemate guard: nobody can finish the fight, so end it as a loss instead of spinning.
            LOG_WARN("[turn] Round limit {} reached - ending battle as a stalemate.", kMaxBattleRounds);
            if (log) log->push("Stalemate - the battle ends.");
            outcome->resolved = true;
            outcome->victory = false;
            runtime->requests.push_back(FStopTurnSession{});
            return;
        }
        const entt::entity actor = token.actor;
        if (!IsAlive(registry, actor)) {
            return; // synchronize() advances past a dead actor
        }

        // Turn start: refill both AP pools (design §4: 서로 전환되지 않는 두 풀).
        if (auto* points = registry.try_get<FActionPoints>(actor); turnStarted && points) {
            points->move = points->moveMax;
            points->skill = points->skillMax;
            LOG_DEBUG("[turn] {} AP refilled - move {}/{} skill {}/{}", UnitDisplayName(registry, actor),
                      points->move, points->moveMax, points->skill, points->skillMax);
        }

        // Turn start: status tick (docs/design/skill-rune-progression.md §4).
        bool skipTurn = false;
        if (auto* active = registry.try_get<FActiveStatuses>(actor); turnStarted && active && !active->effects.empty()) {
            const std::vector<const gameplay::FStatusContent*> definitions = CollectStatuses(*active, *content);
            const FStatusTickResult tick = TickStatuses(*active, definitions, combat->rng);
            // WP-06 (P6-6, ★3): the tick can change what the sheet derives (WP-13's speed_pct reads it on
            // its next collect), and the refresh must beat both the status-death early return below and
            // the skip turn. Idempotent; Ember-only guard — a forest refresh would flatten the inline
            // passives to base (FCombatSheet.h × FBattleState.cpp).
            if (registry.ctx().contains<FDungeonRun>()) {
                RefreshCombatSheet(registry, actor, *content);
            }
            if (tick.damage > 0) {
                if (log) log->push(UnitDisplayName(registry, actor) + " takes " + std::to_string(tick.damage) + " status damage");
                if (ApplyDamageToUnit(registry, actor, tick.damage) && log) {
                    log->push(UnitDisplayName(registry, actor) + " falls!");
                }
            }
            if (!IsAlive(registry, actor)) {
                return; // a unit that dies from its status tick gets no OnTurnStart
            }
            skipTurn = tick.skipTurn;
        }

        // WP-04: OnTurnStart fires once per turn, after the tick, even when the turn will be skipped — and
        // before the skipTurn FEndTurn is queued, so a skipped unit still runs its turn-start hooks. The
        // live path calls the same helper FireTurnEventHooks uses for FTurnStarted.
        if (turnStarted) {
            FireTurnStartedHooks(registry, token);
        }

        if (skipTurn) {
            if (log) log->push(UnitDisplayName(registry, actor) + " cannot act - turn skipped");
            runtime->requests.push_back(FEndTurn{token});
            return;
        }

        const bool isParty = registry.all_of<FPartyMember>(actor);
        if (isParty && !combat->autoPlay) {
            return; // party turns wait for player input
        }
        // WP-06 (P6-10): the countdown runs on real dt scaled by the presentation speed setting — the
        // context value only; the simulation never loads it from disk (the title view owns that file IO).
        // Absent = 1x.
        const auto* settings = registry.ctx().find<FSettings>();
        const float scale = settings != nullptr ? settings->animSpeed : 1.0f;
        combat->thinkDelay -= deltaTime * scale;
        if (combat->thinkDelay > 0.f && !combat->autoPlay) return;
        combat->thinkDelay = kAiThinkDelaySeconds;

        // Auto action (enemies; both sides when auto-play is on). The decision lives in gameplay/combat;
        // this system only applies it to the queue and closes the turn.
        auto* queue = registry.ctx().find<FActionQueue>();
        const auto* loadout = registry.try_get<FSkillLoadout>(actor);
        if (queue == nullptr || loadout == nullptr || loadout->slots.empty()) {
            runtime->requests.push_back(FEndTurn{token});
            return;
        }
        if (!queue->pending.empty()) return;
        if (combat->autoActionsThisTurn >= kMaxAutoActionsPerTurn) {
            LOG_WARN("[turn] {} hit the auto-action cap - ending turn.", UnitDisplayName(registry, actor));
            runtime->requests.push_back(FEndTurn{token});
            return;
        }

        if (auto request = ChooseAutoAction(registry, token, !isParty, *content)) {
            // WP-06: one submit path — the command states its source and issues its session seq. A refused
            // submit (PresentationBusy / OutcomeResolved) must cost nothing: no action was taken, the cap
            // and the turn stay untouched, and the same plan retries next frame. thinkDelay 0 takes away
            // the think pause just restored above, so a non-autoplay enemy retries immediately.
            if (!SubmitBattleCommand(registry, FBattleCommand{*request, ECommandSource::Ai, 0})) {
                combat->thinkDelay = 0.f;
                return;
            }
            ++combat->autoActionsThisTurn;
        } else {
            runtime->requests.push_back(FEndTurn{token});
        }
    }

    [[nodiscard]] std::string name() const { return "TurnFlowSystem"; }
};

} // namespace game::ecs
