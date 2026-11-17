#include "gameplay/ai/FAiWorld.h"

#include <algorithm>

#include "ecs/components/FActionPoints.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/ai/FSkillRole.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/grid/FGridTerrain.h"

namespace game::ai {
namespace {

using FResolvedLoadout = std::vector<std::optional<FResolvedSkill>>;

/// The defense a unit of `party` side meets on average: the mean over the units on the other side.
[[nodiscard]] FStats MeanDefenseFacing(const std::vector<FUnitView>& units, bool party) {
    int physical = 0;
    int magical = 0;
    int count = 0;
    for (const FUnitView& unit : units) {
        if (unit.party != party) {
            physical += unit.stats.defense;
            magical += unit.stats.magicDefense;
            ++count;
        }
    }
    FStats mean{};
    if (count > 0) {
        mean.defense = physical / count;
        mean.magicDefense = magical / count;
    }
    return mean;
}

} // namespace

FAiWorld BuildAiWorld(entt::registry& registry, entt::entity actor, const gameplay::FContentRegistry& content) {
    FAiWorld world;
    if (const auto* terrain = registry.ctx().find<FGridTerrain>()) {
        world.moveRange = terrain->moveRange;
        world.trapDamage = terrain->trapDamage;
    }

    std::vector<FResolvedLoadout> loadouts; // parallel to world.units
    std::vector<int> skillApPools;          // parallel to world.units
    for (const entt::entity entity : registry.view<FGridPosition, FHealth>()) {
        if (!IsAlive(registry, entity)) {
            continue;
        }
        FUnitView view;
        view.entity = entity;
        view.cell = registry.get<FGridPosition>(entity);
        const FHealth& health = registry.get<FHealth>(entity);
        view.hp = std::max(0, health.current);
        view.hpMax = std::max(1, health.max);
        if (const auto* stats = registry.try_get<FStats>(entity)) {
            view.stats = *stats;
        }
        view.party = registry.all_of<FPartyMember>(entity);
        int skillAp = 0;
        if (const auto* points = registry.try_get<FActionPoints>(entity)) {
            view.hasMoveAp = points->moveMax > 0;
            skillAp = points->skillMax;
            if (entity == actor) {
                world.moveAp = points->move;
                world.skillAp = points->skill;
            }
        }
        if (const auto* statuses = registry.try_get<FActiveStatuses>(entity); statuses && !statuses->effects.empty()) {
            view.effects = statuses->effects;
            view.attackModifier = StatusAttackModifier(*statuses, CollectStatuses(*statuses, content));
        }

        FResolvedLoadout resolved;
        if (const auto* loadout = registry.try_get<FSkillLoadout>(entity)) {
            resolved.reserve(loadout->slots.size());
            for (const FSkillSlot& slot : loadout->slots) {
                resolved.push_back(ResolveSkillSlot(slot, content));
            }
        }
        if (entity == actor) {
            world.selfIndex = static_cast<int>(world.units.size());
            world.selfSkills = resolved;
        }
        loadouts.push_back(std::move(resolved));
        skillApPools.push_back(skillAp);
        world.units.push_back(std::move(view));
    }

    // Capabilities: what each unit can do to the side it faces. Threat is the best swing per AP scaled to
    // the unit's whole AP pool, i.e. what it can put out in one turn.
    for (std::size_t index = 0; index < world.units.size(); ++index) {
        FUnitView& unit = world.units[index];
        const FStats facing = MeanDefenseFacing(world.units, unit.party);
        int bestPerApHundredths = 0;
        for (const std::optional<FResolvedSkill>& skill : loadouts[index]) {
            if (!skill) {
                continue;
            }
            int amount = 0;
            switch (ClassifySkill(*skill)) {
            case ESkillRole::Damage:
                unit.canDamage = true;
                unit.damageRange = std::max(unit.damageRange, skill->range);
                amount = ComputeSkillDamage(unit.stats, unit.attackModifier, *skill, facing);
                break;
            case ESkillRole::Heal:
                unit.canSupport = true;
                amount = ComputeSkillHeal(unit.stats, *skill);
                break;
            case ESkillRole::Buff:
                unit.canSupport = true;
                break;
            case ESkillRole::Debuff:
            case ESkillRole::Utility:
                break;
            }
            bestPerApHundredths = std::max(bestPerApHundredths, amount * 100 / std::max(1, skill->apCost));
        }
        unit.threat = std::max(1, bestPerApHundredths * skillApPools[index] / 100);
    }

    // What the other side could do to each unit next round: every enemy that can reach it, with its best hit
    // on that unit. This is what makes a heal a rescue (the unit would otherwise die) instead of a top-up.
    for (std::size_t attackerIndex = 0; attackerIndex < world.units.size(); ++attackerIndex) {
        const FUnitView& attacker = world.units[attackerIndex];
        if (!attacker.canDamage) {
            continue;
        }
        const int reach = (attacker.hasMoveAp ? world.moveRange : 0) + attacker.damageRange;
        for (FUnitView& victim : world.units) {
            if (victim.party == attacker.party || GridDistance(attacker.cell, victim.cell) > reach) {
                continue;
            }
            int best = 0;
            for (const std::optional<FResolvedSkill>& skill : loadouts[attackerIndex]) {
                if (skill && ClassifySkill(*skill) == ESkillRole::Damage) {
                    best = std::max(best, ComputeSkillDamage(attacker.stats, attacker.attackModifier, *skill, victim.stats));
                }
            }
            victim.incoming += best;
        }
    }

    // What every hostile unit could do to the acting unit specifically (used to judge where it is safe).
    if (const FUnitView* self = world.self()) {
        const FStats selfStats = self->stats;
        const bool selfParty = self->party;
        for (std::size_t index = 0; index < world.units.size(); ++index) {
            FUnitView& unit = world.units[index];
            if (unit.party == selfParty) {
                continue;
            }
            for (const std::optional<FResolvedSkill>& skill : loadouts[index]) {
                if (skill && ClassifySkill(*skill) == ESkillRole::Damage) {
                    unit.damageToSelf = std::max(
                        unit.damageToSelf, ComputeSkillDamage(unit.stats, unit.attackModifier, *skill, selfStats));
                }
            }
        }
    }
    return world;
}

} // namespace game::ai
