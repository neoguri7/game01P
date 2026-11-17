#include "gameplay/data/FEffectContent.h"

#include <utility>

namespace game::gameplay {

const char* HookEventName(EHookEvent event) {
    switch (event) {
    case EHookEvent::OnHit:
        return "on_hit";
    case EHookEvent::OnKill:
        return "on_kill";
    case EHookEvent::OnDamageTaken:
        return "on_damage_taken";
    case EHookEvent::OnTurnStart:
        return "on_turn_start";
    case EHookEvent::OnTurnEnd:
        return "on_turn_end";
    case EHookEvent::OnRoomClear:
        return "on_room_clear";
    }
    return "unknown";
}

std::optional<EHookEvent> ParseHookEvent(std::string_view name) {
    if (name == "on_hit") return EHookEvent::OnHit;
    if (name == "on_kill") return EHookEvent::OnKill;
    if (name == "on_damage_taken") return EHookEvent::OnDamageTaken;
    if (name == "on_turn_start") return EHookEvent::OnTurnStart;
    if (name == "on_turn_end") return EHookEvent::OnTurnEnd;
    if (name == "on_room_clear") return EHookEvent::OnRoomClear;
    return std::nullopt;
}

std::vector<FHook> DecodeHooks(std::span<const FContentRow> rows, std::string_view source) {
    std::vector<FHook> hooks;
    hooks.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const FContentRow& row = rows[index];
        FHook hook;
        hook.source = std::string{source};
        hook.id = hook.source + "#" + std::to_string(index);
        hook.eventName = row.text(kHookFieldEvent);
        // An unknown name keeps the default event value here; ValidateEffectContent rejects it by
        // eventName, because a decoder cannot fail (WP-04 §1, accepted change B).
        if (const std::optional<EHookEvent> event = ParseHookEvent(hook.eventName)) {
            hook.event = *event;
        }
        // `effect` is a required Object field: the loader guarantees exactly one dense child row.
        if (const FContentRow* effect = row.object(kHookFieldEffect)) {
            hook.effect.kind = effect->text(kEffectFieldKind);
            hook.effect.amount = effect->integer(kEffectFieldAmount, 0);
            hook.effect.statusId = effect->text(kEffectFieldStatusId);
            hook.effect.duration = effect->integer(kEffectFieldDuration, 0);
            hook.effect.magnitude = effect->integer(kEffectFieldMagnitude, 0);
            hook.effect.distance = effect->integer(kEffectFieldDistance, 0);
            hook.effect.from = effect->text(kEffectFieldFrom);
            hook.effect.stat = effect->text(kEffectFieldStat);
            hook.effect.pool = effect->text(kEffectFieldPool);
            hook.effect.member = effect->text(kEffectFieldMember);
            hook.effect.text = effect->text(kEffectFieldText);
        }
        hook.oncePerTurn = row.boolean(kHookFieldOncePerTurn, false);
        const std::string authoredId = row.text(kHookFieldId);
        if (!authoredId.empty()) {
            hook.id = authoredId;
        }
        hooks.push_back(std::move(hook));
    }
    return hooks;
}

std::optional<std::string> ValidateEffectSpec(const FEffectSpec& spec, FEffectScope scope) {
    if (spec.kind.empty()) {
        return "effect kind is empty";
    }
    // `pool`/`member` address run-scoped state (the Run vocabulary). A Battle-scope spec that authors them
    // is a shape error: battle effects never reach FDungeonRun, so the parameters could never be honored.
    if (scope == FEffectScope::Battle && (!spec.pool.empty() || !spec.member.empty())) {
        return "run-only parameters (pool/member) on a battle-scoped effect";
    }
    return std::nullopt;
}

} // namespace game::gameplay
