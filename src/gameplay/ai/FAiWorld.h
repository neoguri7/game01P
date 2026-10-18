#pragma once

#include <optional>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FStats.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"

namespace game::ai {

/// One living unit as the AI sees it. Everything here is derived from game state through the same rules the
/// resolver uses (FSkillResolver / FStatusRules), never from authored guesses.
struct FUnitView {
    entt::entity entity{entt::null};
    FGridPosition cell{};
    int hp{0};
    int hpMax{1};
    FStats stats{};
    int attackModifier{0}; // stat-status sum, already applied by the resolver to every attack
    bool party{false};
    bool hasMoveAp{false};
    bool canDamage{false};
    bool canSupport{false}; // owns a heal or a buff
    int damageRange{0};     // longest range among its damage skills
    int threat{1};          // expected HP it swings per turn: best damage (or heal) per AP times its AP pool
    int damageToSelf{0};    // best single hit this unit could land on the acting unit (hostile units only)
    int incoming{0};        // damage the other side could put on this unit next round (reach and best hits)
    std::vector<FStatusEffect> effects;

    [[nodiscard]] int hpPercent() const { return hp * 100 / hpMax; }
};

/// A frozen picture of the battle for one decision. Built once per tick (nothing mutates while a tick
/// runs) so leaves can ask cheap questions instead of each re-walking the registry.
struct FAiWorld {
    std::vector<FUnitView> units; // living units, registry view order (deterministic)
    int selfIndex{-1};
    std::vector<std::optional<FResolvedSkill>> selfSkills; // the actor's loadout, resolved with its runes
    int moveRange{1};
    int trapDamage{0};
    int moveAp{0};  // the actor's remaining move AP
    int skillAp{0}; // the actor's remaining skill AP

    [[nodiscard]] const FUnitView* self() const {
        return selfIndex >= 0 ? &units[static_cast<std::size_t>(selfIndex)] : nullptr;
    }

    [[nodiscard]] const FUnitView* find(entt::entity entity) const {
        for (const FUnitView& unit : units) {
            if (unit.entity == entity) {
                return &unit;
            }
        }
        return nullptr;
    }
};

[[nodiscard]] FAiWorld BuildAiWorld(
    entt::registry& registry, entt::entity actor, const gameplay::FContentRegistry& content);

} // namespace game::ai
