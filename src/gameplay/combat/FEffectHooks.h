#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/data/FEffectContent.h"
#include "gameplay/turn/TurnTypes.h"

namespace game {

/// One hook dispatch context: who owns the dispatch, the other unit of the interaction, the triggering
/// amount and the provenance (skill id + hook source) for logs and diagnostics.
struct FHookContext {
    entt::entity owner{entt::null};
    entt::entity other{entt::null};
    int amount{0};
    std::string skillId;
    std::string source;
};

/// One effect application's context: the hook context plus the scope the caller applies in.
struct FEffectContext {
    gameplay::FEffectScope scope{gameplay::FEffectScope::Battle};
    entt::entity owner{entt::null};
    entt::entity other{entt::null};
    int amount{0};
    std::string skillId;
    std::string source;
};

using FEffectApplier = std::function<void(entt::registry&, const FEffectContext&, const gameplay::FEffectSpec&)>;
using FEffectValidator = std::function<bool(const gameplay::FEffectSpec&)>;
using FEffectListener = std::function<void(entt::registry&, const FHookContext&)>;

/// kind -> {scope, applier, validator}. Ctx-owned (WP-04 §2): the composition root registers, Shutdown
/// erases. Registered Battle effects never touch FDungeonRun; Run effects are the other scopes' business.
struct FEffectRegistry {
    struct FKind {
        gameplay::FEffectScope scope{gameplay::FEffectScope::Battle};
        FEffectApplier apply;
        FEffectValidator validate;
    };
    struct FEntry {
        std::string kind;
        FKind impl;
    };
    std::vector<FEntry> kinds; // linear scan: registration order is boot order and the table is small (R16)

    [[nodiscard]] const FKind* find(std::string_view kind) const;
};

/// The hooks one unit carries into a battle, built once (lazily) from its loadout.
struct FActiveHook {
    gameplay::FHook hook;
    std::string slotSkillId; // the slot the hook came from; OnHit/OnKill match this against the acting skill
};

struct FActiveHooks {
    std::vector<FActiveHook> hooks;
    std::vector<std::string> firedThisTurn; // FHook::id set for oncePerTurn; reset on the owner's turn start
};

/// Ctx-owned dispatch state: the code listeners, the recursion guard and the same-frame double-update
/// guards. Created on first use; Shutdown erases it so listeners never lapse into the next battle.
struct FEffectDispatchState {
    std::vector<std::pair<gameplay::EHookEvent, FEffectListener>> listeners; // registration order
    int dispatchDepth{0};
    FTurnToken lastTurnEnd{};   // FTurnEnded dispatch dedupe (a second update in the same frame)
    FTurnToken lastTurnStart{}; // OnTurnStart guard: a second update in the same frame must not rerun the turn
};

/// Recursion guard (WP-04 §4): one nested level is allowed — a code listener that explicitly calls
/// FireHooks again gets that dispatch (logged as `nested`, its data-hook audit lines carry the marker).
/// A second level is refused with a warning. Hook-effect damage calls ApplyDamageToUnit directly and
/// never re-enters, so the cap bounds only listener-initiated nesting.
inline constexpr int kMaxHookDepth = 1;

/// Registers one effect kind. Re-registering an existing kind replaces it: the composition root registers
/// once per boot, and tests deliberately re-register to probe validation; last registration wins.
void RegisterEffect(entt::registry& registry, std::string_view kind, gameplay::FEffectScope scope,
                    FEffectApplier applier, FEffectValidator validator);

/// Registers the six core Battle effects (WP-04 owns them): damage_self, heal_self, heal_target,
/// apply_status_self, apply_status_target, log. Run-scope effects are registered by WP-09.
void RegisterCoreEffects(entt::registry& registry);

/// Applies one typed effect. False (with the reason logged) when the kind is unregistered or its scope
/// does not match the context — a Battle context must refuse a Run-scope kind and the reverse.
[[nodiscard]] bool ApplyEffect(
    entt::registry& registry, const FEffectContext& context, const gameplay::FEffectSpec& spec);

/// Validates every skill/rune hook at boot: the event name parses, the effect kind is registered for the
/// content's scope (Battle), and the kind's validator accepts the parameters. False = boot fails; every
/// offending hook is logged with its source, id and kind.
[[nodiscard]] bool ValidateEffectContent(entt::registry& registry);

void RegisterHookListener(entt::registry& registry, gameplay::EHookEvent event, FEffectListener listener);

/// Lazy build: stamps (or rebuilds) the unit's FActiveHooks from its loadout — slot order, then the skill's
/// own hooks, then each rune's hooks in equip order (the synthesized leech hook sits at its rune's
/// position, FSkillResolver). FireHooks calls this only when the owner has no FActiveHooks yet.
void BuildActiveHooks(entt::registry& registry, entt::entity owner);

/// Fires the owner's data hooks matching `event`, then every code listener in registration order.
/// Returns the number of data hooks + listeners actually fired. A dead owner fires nothing (WP-04 §3).
/// One nested level (a listener calling FireHooks again) is allowed and logged as `nested`; deeper
/// nesting is refused with a warning (kMaxHookDepth).
[[nodiscard]] int FireHooks(entt::registry& registry, gameplay::EHookEvent event, const FHookContext& context);

/// Consumes turn lifecycle events. FTurnEnded fires OnTurnEnd only for a Completed, living actor;
/// FTurnStarted resets the once-per-turn marks and then fires OnTurnStart. Today's producer cannot flush
/// several turns in one frame — one TurnSystem update yields at most one Completed FTurnEnded, and
/// TurnFlowSystem starts every turn it sees (the `turnStarted` serial check) — so no event is skipped in
/// practice. The span form is the single entry point if a future producer ever flushes several turns
/// per frame.
void FireTurnEventHooks(entt::registry& registry, std::span<const FTurnEvent> events);

/// Fires OnTurnStart for one turn: resets the actor's once-per-turn marks first. Deduped by token, so a
/// second call for the same turn (a second update in the same frame) cannot reset or double-fire.
void FireTurnStartedHooks(entt::registry& registry, const FTurnToken& token);

/// Fires OnTurnEnd for one ended turn: Completed + living actor only, deduped by token.
void FireTurnEndedHooks(entt::registry& registry, const FTurnToken& token, ETurnEndReason reason);

} // namespace game
