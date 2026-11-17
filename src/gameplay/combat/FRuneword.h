#pragma once

#include <entt/entt.hpp>

#include "ecs/components/FSkillLoadout.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {

/// Registry context. `fired` is true once a runeword has acted in the current skill use.
struct FRunewordGuard { bool fired{false}; };

/// The runeword of a slot: the first engine in content.engines.rows order that has an effect
/// (effectKind non-empty) and whose requiresRunes are all in slot.runeIds. nullptr = none.
[[nodiscard]] const gameplay::FEngineContent* ActiveRuneword(
    const gameplay::FContentRegistry& content, const FSkillSlot& slot);
/// Starts a skill use: the guard exists afterwards with fired == false.
void BeginRunewordAction(entt::registry& registry);
/// detonate, spread, arc. Safe to call for every OnHit of every unit.
void RunewordOnHit(entt::registry& registry, const FHookContext& context);
/// carry. Safe to call for every OnKill.
void RunewordOnKill(entt::registry& registry, const FHookContext& context);
/// Registers the two functions with RegisterHookListener for EHookEvent::OnHit and OnKill (boot, once).
void RegisterRunewordListeners(entt::registry& registry);

} // namespace game
