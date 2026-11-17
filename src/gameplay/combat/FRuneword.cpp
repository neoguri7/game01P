#include "gameplay/combat/FRuneword.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/dungeon/FDungeonRun.h"

namespace game {
namespace {

/// Everything a listener needs once the common gate passed.
struct FRunewordCall {
    const gameplay::FContentRegistry* content{nullptr};
    const gameplay::FEngineContent* engine{nullptr};
    const gameplay::FStatusContent* row{nullptr};
    FStatusEffect trigger;
};

[[nodiscard]] bool SlotHoldsAll(const FSkillSlot& slot, const std::vector<std::string>& required) {
    for (const std::string& runeId : required) {
        if (std::ranges::find(slot.runeIds, runeId) == slot.runeIds.end()) return false;
    }
    return true;
}

[[nodiscard]] bool OnHitKind(std::string_view kind) {
    return kind == gameplay::kEngineEffectDetonate || kind == gameplay::kEngineEffectSpread ||
           kind == gameplay::kEngineEffectArc;
}

/// The common gate of both listeners (WP-45 spec section 5); false = no effect.
[[nodiscard]] bool Gate(entt::registry& registry, const FHookContext& context, bool onKill, FRunewordCall& call) {
    call.content = registry.ctx().find<gameplay::FContentRegistry>();
    if (call.content == nullptr) return false;
    if (const auto* guard = registry.ctx().find<FRunewordGuard>(); guard != nullptr && guard->fired) return false;
    if (!registry.valid(context.owner)) return false;
    if (!registry.all_of<FPartyMember, FSkillLoadout>(context.owner)) return false;
    const FSkillSlot* slot = nullptr;
    for (const FSkillSlot& candidate : registry.get<FSkillLoadout>(context.owner).slots) {
        if (candidate.skillId == context.skillId) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) return false;
    call.engine = ActiveRuneword(*call.content, *slot);
    if (call.engine == nullptr) return false;
    const bool carry = call.engine->effectKind == gameplay::kEngineEffectCarry;
    if (onKill ? !carry : !OnHitKind(call.engine->effectKind)) return false;
    if (!registry.valid(context.other) || !registry.all_of<FEnemy, FGridPosition>(context.other)) return false;
    const auto* active = registry.try_get<FActiveStatuses>(context.other);
    if (active == nullptr) return false;
    bool found = false;
    for (const FStatusEffect& effect : active->effects) {
        if (effect.statusId == call.engine->triggerStatusId && effect.magnitude >= call.engine->minMagnitude) {
            call.trigger = effect;
            found = true;
            break;
        }
    }
    if (!found) return false;
    call.row = call.content->statuses.find(call.engine->triggerStatusId);
    return call.row != nullptr;
}

void MarkFired(entt::registry& registry) {
    if (auto* guard = registry.ctx().find<FRunewordGuard>()) {
        guard->fired = true;
    } else {
        registry.ctx().emplace<FRunewordGuard>().fired = true;
    }
}

void Feed(entt::registry& registry, const std::string& line) {
    if (auto* log = registry.ctx().find<FCombatLog>()) log->push(line);
}

/// Other living enemies (not `target`) accepted by `keep`, in registry order.
template <typename TKeep>
[[nodiscard]] std::vector<entt::entity> OtherEnemies(entt::registry& registry, entt::entity target, TKeep keep) {
    std::vector<entt::entity> result;
    for (const entt::entity candidate : registry.view<FEnemy, FGridPosition>()) {
        if (candidate == target || !IsAlive(registry, candidate)) continue;
        if (keep(candidate)) result.push_back(candidate);
    }
    return result;
}

void DamageAll(entt::registry& registry, const std::vector<entt::entity>& victims, int amount) {
    for (const entt::entity victim : victims) {
        const std::string name = UnitDisplayName(registry, victim);
        if (ApplyDamageToUnit(registry, victim, amount)) Feed(registry, name + " falls!");
    }
}

[[nodiscard]] bool CanReceive(entt::registry& registry, entt::entity unit, const gameplay::FStatusContent& row) {
    const auto* active = registry.try_get<FActiveStatuses>(unit);
    if (active == nullptr || IsStatusImmune(*active, row.id)) return false;
    return std::ranges::none_of(active->effects, [&row](const FStatusEffect& e) { return e.statusId == row.id; });
}

[[nodiscard]] entt::entity LowestId(const std::vector<entt::entity>& units) {
    entt::entity best = entt::null;
    for (const entt::entity unit : units) {
        if (best == entt::null || entt::to_integral(unit) < entt::to_integral(best)) best = unit;
    }
    return best;
}

} // namespace

const gameplay::FEngineContent* ActiveRuneword(const gameplay::FContentRegistry& content, const FSkillSlot& slot) {
    for (const gameplay::FEngineContent& engine : content.engines.rows) {
        if (engine.effectKind.empty() || engine.requiresRunes.empty()) continue;
        if (SlotHoldsAll(slot, engine.requiresRunes)) return &engine;
    }
    return nullptr;
}

void BeginRunewordAction(entt::registry& registry) {
    registry.ctx().insert_or_assign(FRunewordGuard{});
}

void RunewordOnHit(entt::registry& registry, const FHookContext& context) {
    FRunewordCall call;
    if (!Gate(registry, context, false, call)) return;
    const gameplay::FEngineContent& engine = *call.engine;
    const std::string actor = UnitDisplayName(registry, context.owner);
    const FGridPosition targetCell = registry.get<FGridPosition>(context.other);

    if (engine.effectKind == gameplay::kEngineEffectDetonate) {
        const int amount = call.trigger.magnitude * call.trigger.turnsRemaining * engine.effectPercent / 100;
        if (amount <= 0) return;
        const auto victims = OtherEnemies(registry, context.other, [&](entt::entity e) {
            return GridDistance(targetCell, registry.get<FGridPosition>(e)) == 1;
        });
        if (victims.empty()) return;
        DamageAll(registry, victims, amount);
        Feed(registry, actor + ": " + engine.name + " scorches " + std::to_string(victims.size()) +
                           " nearby for " + std::to_string(amount));
        MarkFired(registry);
    } else if (engine.effectKind == gameplay::kEngineEffectSpread) {
        const auto candidates = OtherEnemies(registry, context.other, [&](entt::entity e) {
            return GridDistance(targetCell, registry.get<FGridPosition>(e)) == 1 && CanReceive(registry, e, *call.row);
        });
        const entt::entity victim = LowestId(candidates);
        if (victim == entt::null) return;
        ApplyStatus(registry.get<FActiveStatuses>(victim), *call.row,
                    engine.effectTurns - EffectiveStatusDuration(*call.row, 0), 0);
        if (registry.ctx().contains<FDungeonRun>()) RefreshCombatSheet(registry, victim, *call.content);
        Feed(registry, actor + ": " + engine.name + " spreads " + call.row->displayName + " to " +
                           UnitDisplayName(registry, victim));
        MarkFired(registry);
    } else if (engine.effectKind == gameplay::kEngineEffectArc) {
        if (context.amount <= 0) return;
        const int amount = std::max(1, context.amount * engine.effectPercent / 100);
        const auto victims = OtherEnemies(registry, context.other, [&](entt::entity e) {
            const auto* active = registry.try_get<FActiveStatuses>(e);
            return active != nullptr && std::ranges::any_of(active->effects, [&](const FStatusEffect& s) {
                       return s.statusId == call.row->id;
                   });
        });
        if (victims.empty()) return;
        DamageAll(registry, victims, amount);
        Feed(registry, actor + ": " + engine.name + " arcs to " + std::to_string(victims.size()) + " for " +
                           std::to_string(amount));
        MarkFired(registry);
    }
}

void RunewordOnKill(entt::registry& registry, const FHookContext& context) {
    FRunewordCall call;
    if (!Gate(registry, context, true, call)) return;
    const FGridPosition slainCell = registry.get<FGridPosition>(context.other);
    entt::entity destination = entt::null;
    int bestDistance = 0;
    for (const entt::entity candidate : OtherEnemies(registry, context.other, [&](entt::entity e) {
             return CanReceive(registry, e, *call.row);
         })) {
        const int distance = GridDistance(slainCell, registry.get<FGridPosition>(candidate));
        if (destination == entt::null || distance < bestDistance ||
            (distance == bestDistance && entt::to_integral(candidate) < entt::to_integral(destination))) {
            destination = candidate;
            bestDistance = distance;
        }
    }
    if (destination == entt::null) return;
    ApplyStatus(registry.get<FActiveStatuses>(destination), *call.row,
                call.trigger.turnsRemaining - EffectiveStatusDuration(*call.row, 0),
                call.trigger.magnitude - EffectiveStatusMagnitude(*call.row, 0));
    Feed(registry, UnitDisplayName(registry, context.owner) + ": " + call.engine->name + " carries " +
                       call.row->displayName + " to " + UnitDisplayName(registry, destination));
    MarkFired(registry);
}

void RegisterRunewordListeners(entt::registry& registry) {
    RegisterHookListener(registry, gameplay::EHookEvent::OnHit,
                         [](entt::registry& r, const FHookContext& context) { RunewordOnHit(r, context); });
    RegisterHookListener(registry, gameplay::EHookEvent::OnKill,
                         [](entt::registry& r, const FHookContext& context) { RunewordOnKill(r, context); });
}

} // namespace game
