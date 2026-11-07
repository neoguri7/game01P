#include "gameplay/combat/FEffectHooks.h"

#include <algorithm>
#include <optional>
#include <variant>

#include "core/Logger.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {
namespace {

[[nodiscard]] FEffectRegistry& EnsureEffectRegistry(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<FEffectRegistry>()) {
        return *existing;
    }
    return registry.ctx().emplace<FEffectRegistry>();
}

[[nodiscard]] FEffectDispatchState& EnsureDispatchState(entt::registry& registry) {
    if (auto* existing = registry.ctx().find<FEffectDispatchState>()) {
        return *existing;
    }
    return registry.ctx().emplace<FEffectDispatchState>();
}

[[nodiscard]] const char* ScopeName(gameplay::FEffectScope scope) {
    return scope == gameplay::FEffectScope::Battle ? "battle" : "run";
}

/// Heals a living unit by `amount`, clamped to max; a dead unit is a no-op (heal_target contract, WP-04 §3).
void HealUnit(entt::registry& registry, entt::entity unit, int amount) {
    if (amount <= 0 || !IsAlive(registry, unit)) {
        return;
    }
    auto* health = registry.try_get<FHealth>(unit);
    if (health == nullptr) {
        return;
    }
    const int before = health->current;
    health->current = std::min(health->max, health->current + amount);
    if (health->current > before) {
        if (auto* log = registry.ctx().find<FCombatLog>()) {
            log->push(UnitDisplayName(registry, unit) + " heals +" +
                      std::to_string(health->current - before) + " HP");
        }
    }
}

/// Applies a status spec to a living unit and refreshes its sheet (WP-03 contract: every apply_status_*
/// hook effect calls RefreshCombatSheet). duration/magnitude are the same rune-style deltas ApplyStatus
/// folds into the definition (0 keeps the definition's values).
void ApplyStatusSpec(entt::registry& registry, entt::entity unit, const gameplay::FEffectSpec& spec) {
    if (!IsAlive(registry, unit)) {
        return;
    }
    auto* active = registry.try_get<FActiveStatuses>(unit);
    auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (active == nullptr || content == nullptr) {
        return;
    }
    const gameplay::FStatusContent* definition = content->statuses.find(spec.statusId);
    if (definition == nullptr) {
        LOG_WARN("[hook] status '{}' is not in the content tables; effect skipped.", spec.statusId);
        return;
    }
    ApplyStatus(*active, *definition, spec.duration, spec.magnitude);
    RefreshCombatSheet(registry, unit, *content);
}

} // namespace

const FEffectRegistry::FKind* FEffectRegistry::find(std::string_view kind) const {
    for (const FEntry& entry : kinds) {
        if (entry.kind == kind) {
            return &entry.impl;
        }
    }
    return nullptr;
}

void RegisterEffect(entt::registry& registry, std::string_view kind, gameplay::FEffectScope scope,
                    FEffectApplier applier, FEffectValidator validator) {
    FEffectRegistry& effects = EnsureEffectRegistry(registry);
    FEffectRegistry::FKind impl{scope, std::move(applier), std::move(validator)};
    for (FEffectRegistry::FEntry& entry : effects.kinds) {
        if (entry.kind == kind) {
            entry.impl = std::move(impl); // re-registration replaces (last wins; see the header contract)
            return;
        }
    }
    effects.kinds.push_back(FEffectRegistry::FEntry{std::string{kind}, std::move(impl)});
}

void RegisterCoreEffects(entt::registry& registry) {
    using gameplay::FEffectScope;
    using gameplay::FEffectSpec;
    constexpr FEffectScope battle = FEffectScope::Battle;

    RegisterEffect(registry, gameplay::kEffectKindDamageSelf, battle,
        [](entt::registry& r, const FEffectContext& context, const FEffectSpec& spec) {
            // Player-facing lines, like the status-tick damage in TurnFlowSystem: the audit line alone does
            // not tell the player why a health bar moved.
            auto* log = r.ctx().find<FCombatLog>();
            if (log != nullptr) {
                log->push(UnitDisplayName(r, context.owner) + " takes " + std::to_string(spec.amount) + " damage");
            }
            if (ApplyDamageToUnit(r, context.owner, spec.amount) && log != nullptr) {
                log->push(UnitDisplayName(r, context.owner) + " falls!");
            }
        },
        [](const FEffectSpec& spec) { return spec.amount > 0; });

    RegisterEffect(registry, gameplay::kEffectKindHealSelf, battle,
        [](entt::registry& r, const FEffectContext& context, const FEffectSpec& spec) {
            HealUnit(r, context.owner, spec.amount);
        },
        [](const FEffectSpec& spec) { return spec.amount > 0; });

    RegisterEffect(registry, gameplay::kEffectKindHealTarget, battle,
        [](entt::registry& r, const FEffectContext& context, const FEffectSpec& spec) {
            HealUnit(r, context.other, spec.amount);
        },
        [](const FEffectSpec& spec) { return spec.amount > 0; });

    RegisterEffect(registry, gameplay::kEffectKindApplyStatusSelf, battle,
        [](entt::registry& r, const FEffectContext& context, const FEffectSpec& spec) {
            ApplyStatusSpec(r, context.owner, spec);
        },
        [](const FEffectSpec& spec) { return !spec.statusId.empty(); });

    RegisterEffect(registry, gameplay::kEffectKindApplyStatusTarget, battle,
        [](entt::registry& r, const FEffectContext& context, const FEffectSpec& spec) {
            ApplyStatusSpec(r, context.other, spec);
        },
        [](const FEffectSpec& spec) { return !spec.statusId.empty(); });

    RegisterEffect(registry, gameplay::kEffectKindLog, battle,
        [](entt::registry& r, const FEffectContext&, const FEffectSpec& spec) {
            if (auto* log = r.ctx().find<FCombatLog>()) {
                log->push(spec.text);
            }
        },
        [](const FEffectSpec& spec) { return !spec.text.empty(); });
}

bool ApplyEffect(entt::registry& registry, const FEffectContext& context, const gameplay::FEffectSpec& spec) {
    auto* effects = registry.ctx().find<FEffectRegistry>();
    if (effects == nullptr) {
        LOG_WARN("[effect] '{}' cannot be applied: no effect kind is registered.", spec.kind);
        return false;
    }
    const FEffectRegistry::FKind* kind = effects->find(spec.kind);
    if (kind == nullptr) {
        LOG_WARN("[effect] '{}' cannot be applied: the kind is unknown.", spec.kind);
        return false;
    }
    if (kind->scope != context.scope) {
        LOG_WARN("[effect] '{}' is {} scoped and cannot run in a {} scope.", spec.kind,
                 ScopeName(kind->scope), ScopeName(context.scope));
        return false;
    }
    kind->apply(registry, context, spec);
    return true;
}

bool ValidateEffectContent(entt::registry& registry) {
    auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (content == nullptr) {
        LOG_ERROR("effect validation needs the loaded content tables (FContentRegistry is missing).");
        return false;
    }
    const auto* effects = registry.ctx().find<FEffectRegistry>();
    bool valid = true;

    // Every failure names the hook's source, id and kind (WP-04 accepted change C): one line per offence.
    const auto checkHook = [&](const gameplay::FHook& hook, const std::string& owner) {
        const char* kind = hook.effect.kind.c_str();
        if (!gameplay::ParseHookEvent(hook.eventName)) {
            LOG_ERROR("effect content: hook '{}' (source '{}') of {} kind '{}': unknown event '{}'.",
                      hook.id, hook.source, owner, kind, hook.eventName);
            valid = false;
            return;
        }
        if (const std::optional<std::string> shape = gameplay::ValidateEffectSpec(hook.effect, gameplay::FEffectScope::Battle)) {
            LOG_ERROR("effect content: hook '{}' (source '{}') of {} kind '{}': {}.",
                      hook.id, hook.source, owner, kind, *shape);
            valid = false;
            return;
        }
        if (effects == nullptr) {
            LOG_ERROR("effect content: hook '{}' (source '{}') of {} kind '{}': no effect kind is registered.",
                      hook.id, hook.source, owner, kind);
            valid = false;
            return;
        }
        const FEffectRegistry::FKind* registered = effects->find(hook.effect.kind);
        if (registered == nullptr) {
            LOG_ERROR("effect content: hook '{}' (source '{}') of {} kind '{}': the kind is not registered.",
                      hook.id, hook.source, owner, kind);
            valid = false;
            return;
        }
        if (registered->scope != gameplay::FEffectScope::Battle) {
            LOG_ERROR("effect content: hook '{}' (source '{}') of {} kind '{}': the kind is {} scoped, "
                      "but skill/rune hooks are battle scoped.",
                      hook.id, hook.source, owner, kind, ScopeName(registered->scope));
            valid = false;
            return;
        }
        if (!registered->validate || !registered->validate(hook.effect)) {
            LOG_ERROR("effect content: hook '{}' (source '{}') of {} kind '{}': required parameters are missing or invalid.",
                      hook.id, hook.source, owner, kind);
            valid = false;
        }
    };

    for (const gameplay::FSkillContent& skill : content->skills.rows) {
        for (const gameplay::FHook& hook : skill.hooks) {
            checkHook(hook, "skill '" + skill.id + "'");
        }
    }
    for (const gameplay::FRuneContent& rune : content->runes.rows) {
        for (const gameplay::FHook& hook : rune.hooks) {
            checkHook(hook, "rune '" + rune.id + "'");
        }
    }
    return valid;
}

void RegisterHookListener(entt::registry& registry, gameplay::EHookEvent event, FEffectListener listener) {
    EnsureDispatchState(registry).listeners.emplace_back(event, std::move(listener));
}

void BuildActiveHooks(entt::registry& registry, entt::entity owner) {
    FActiveHooks built;
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    const auto* loadout = registry.try_get<FSkillLoadout>(owner);
    if (content != nullptr && loadout != nullptr) {
        for (const FSkillSlot& slot : loadout->slots) {
            // ResolveSkillSlot merges the skill's own hooks, then each rune's hooks in equip order, and
            // synthesizes the leech hook at its rune's position — the active list is exactly that merge.
            const std::optional<FResolvedSkill> resolved = ResolveSkillSlot(slot, *content);
            if (!resolved) {
                continue;
            }
            for (const gameplay::FHook& hook : resolved->hooks) {
                built.hooks.push_back(FActiveHook{hook, slot.skillId});
            }
        }
    }
    registry.emplace_or_replace<FActiveHooks>(owner, std::move(built));
}

int FireHooks(entt::registry& registry, gameplay::EHookEvent event, const FHookContext& context) {
    if (!IsAlive(registry, context.owner)) {
        return 0; // dead-owner rule: a dead unit's hooks never fire (any event)
    }
    // Depth at entry: 0 = top-level, 1 = the one allowed nested level (a listener calling FireHooks),
    // > kMaxHookDepth = refused. Only listener-initiated nesting can reach here: hook-effect damage goes
    // through ApplyDamageToUnit directly and never re-enters. A second nested level is a wiring bug, so
    // it stops with a warning instead of recursing.
    int depth = 0;
    if (const auto* state = registry.ctx().find<FEffectDispatchState>()) {
        depth = state->dispatchDepth;
    }
    if (depth > kMaxHookDepth) {
        LOG_WARN("[hook] nested FireHooks for '{}' refused at depth {} (kMaxHookDepth = {}).",
                 gameplay::HookEventName(event), depth, kMaxHookDepth);
        return 0;
    }
    const bool nested = depth > 0; // one nested level is allowed; its lines carry a `nested` marker
    // The depth counter lives in the ctx entry; it is re-read on scope exit, so a ctx insertion during
    // dispatch (a listener wiring another service) can never leave a dangling reference behind.
    EnsureDispatchState(registry).dispatchDepth = depth + 1;
    struct FDispatchScope {
        entt::registry& registry;
        ~FDispatchScope() {
            if (auto* state = registry.ctx().find<FEffectDispatchState>()) {
                state->dispatchDepth = std::max(0, state->dispatchDepth - 1);
            }
        }
    } scope{registry};

    int fired = 0;
    if (nested) {
        if (auto* log = registry.ctx().find<FCombatLog>()) {
            log->push("[hook] nested " + std::string(gameplay::HookEventName(event)));
        }
    }
    if (!registry.all_of<FActiveHooks>(context.owner)) {
        BuildActiveHooks(registry, context.owner); // lazy: the first fire sees the applied loadout
    }
    // Local copy of the hook list: a nested dispatch can lazily build FActiveHooks on another entity,
    // which may move the component pool's storage and invalidate a reference into it. The once-per-turn
    // marks are re-read from the live component per hook instead.
    std::vector<FActiveHook> hooks;
    if (auto* active = registry.try_get<FActiveHooks>(context.owner)) {
        hooks = active->hooks;
    }
    // Slot-bound events: only the hooks of the slot that carries the acting skill fire, so leech on
    // skill A never triggers when the unit uses skill B (accepted change D). An empty context skillId
    // matches only hooks with an empty slot — hand-built test hooks.
    const bool slotBound = event == gameplay::EHookEvent::OnHit || event == gameplay::EHookEvent::OnKill;
    for (const FActiveHook& entry : hooks) {
        if (!IsAlive(registry, context.owner)) {
            break; // damage_self killed the owner: the rest of this owner's dispatch stops
        }
        if (entry.hook.event != event) {
            continue;
        }
        if (slotBound && entry.slotSkillId != context.skillId) {
            continue;
        }
        if (entry.hook.oncePerTurn) {
            const auto* active = registry.try_get<FActiveHooks>(context.owner);
            if (active == nullptr ||
                std::ranges::find(active->firedThisTurn, entry.hook.id) != active->firedThisTurn.end()) {
                continue;
            }
        }
        FEffectContext effect;
        effect.scope = gameplay::FEffectScope::Battle;
        effect.owner = context.owner;
        effect.other = context.other;
        effect.amount = context.amount;
        effect.skillId = context.skillId;
        effect.source = entry.hook.source;
        if (ApplyEffect(registry, effect, entry.hook.effect)) {
            if (entry.hook.oncePerTurn) {
                if (auto* active = registry.try_get<FActiveHooks>(context.owner)) {
                    active->firedThisTurn.push_back(entry.hook.id);
                }
            }
            if (auto* log = registry.ctx().find<FCombatLog>()) {
                // Audit trail (WP-04 §4): one line per fired data hook, marked when the dispatch is nested.
                log->push(std::string("[hook] ") + (nested ? "nested " : "") + entry.hook.source + " " +
                          gameplay::HookEventName(event) + " -> " + entry.hook.effect.kind);
            }
            ++fired;
        }
    }
    // Code listeners fire even when there is not a single data hook, in registration order over the size
    // captured before the loop. A listener may append another listener, and a nested dispatch may replace
    // the whole ctx state, so the state is re-read each step and the function copied before the call —
    // neither can reallocate the vector under an in-flight range-for.
    std::size_t listenerCount = 0;
    if (const auto* state = registry.ctx().find<FEffectDispatchState>()) {
        listenerCount = state->listeners.size();
    }
    for (std::size_t index = 0; index < listenerCount; ++index) {
        const auto* state = registry.ctx().find<FEffectDispatchState>();
        if (state == nullptr || index >= state->listeners.size()) {
            break;
        }
        if (state->listeners[index].first != event) {
            continue;
        }
        const FEffectListener listener = state->listeners[index].second; // copy: the call may append
        listener(registry, context);
        ++fired;
    }
    return fired;
}

void FireTurnStartedHooks(entt::registry& registry, const FTurnToken& token) {
    if (!IsAlive(registry, token.actor)) {
        return; // a unit that dies from its status tick gets no OnTurnStart
    }
    {
        FEffectDispatchState& state = EnsureDispatchState(registry);
        if (state.lastTurnStart == token) {
            return; // a second update in the same frame must not reset or double-fire the same turn
        }
        state.lastTurnStart = token;
    }
    if (auto* active = registry.try_get<FActiveHooks>(token.actor)) {
        active->firedThisTurn.clear(); // oncePerTurn reset on the owner's turn start
    }
    (void)FireHooks(registry, gameplay::EHookEvent::OnTurnStart, FHookContext{.owner = token.actor});
}

void FireTurnEndedHooks(entt::registry& registry, const FTurnToken& token, ETurnEndReason reason) {
    // Removed (dead unit), Stopped/InvalidSpeed (session over) do not fire; victory's OnRoomClear owns the win.
    if (reason != ETurnEndReason::Completed || !IsAlive(registry, token.actor)) {
        return;
    }
    {
        FEffectDispatchState& state = EnsureDispatchState(registry);
        if (state.lastTurnEnd == token) {
            return; // dedupe: the same turn must not fire OnTurnEnd twice in one frame
        }
        state.lastTurnEnd = token;
    }
    (void)FireHooks(registry, gameplay::EHookEvent::OnTurnEnd, FHookContext{.owner = token.actor});
}

void FireTurnEventHooks(entt::registry& registry, std::span<const FTurnEvent> events) {
    for (const FTurnEvent& event : events) {
        if (const auto* ended = std::get_if<FTurnEnded>(&event)) {
            FireTurnEndedHooks(registry, ended->token, ended->reason);
        } else if (const auto* started = std::get_if<FTurnStarted>(&event)) {
            FireTurnStartedHooks(registry, started->token);
        }
    }
}

} // namespace game
