#include "gameplay/ai/FAiQueries.h"

#include <algorithm>
#include <queue>

#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace game::ai {
namespace {

constexpr int kFarAway = 99;

struct FEffect {
    int value{0};
    bool kills{false};
};

[[nodiscard]] bool IsFriendlySkill(const FResolvedSkill& skill) {
    return skill.target == "ally" || skill.target == "self";
}

[[nodiscard]] bool PassesFilter(const FBtContext& ctx, const FResolvedSkill& skill, const FSkillFilter& filter) {
    if ((filter.roles & RoleBit(ClassifySkill(skill))) == 0) {
        return false;
    }
    if (!filter.skillIds.empty() && std::ranges::find(filter.skillIds, skill.skillId) == filter.skillIds.end()) {
        return false;
    }
    if (!filter.tags.empty()) {
        const gameplay::FSkillContent* row = ctx.content.skills.find(skill.skillId);
        if (row == nullptr) {
            return false;
        }
        const bool tagged = std::ranges::any_of(row->tags, [&](const std::string& tag) {
            return std::ranges::find(filter.tags, tag) != filter.tags.end();
        });
        if (!tagged) {
            return false;
        }
    }
    return true;
}

/// What one skill is worth on one affected unit. Damage is the real hit (minus overkill); a kill also
/// removes the victim's future output; support is measured by what it really restores or grants.
[[nodiscard]] FEffect EvaluateOnUnit(
    const FBtContext& ctx, const FResolvedSkill& skill, const FUnitView& unit, bool friendlySkill) {
    const FUnitView& self = *ctx.world.self();
    const FAiTuning& tuning = ctx.tuning;
    FEffect effect;
    if (friendlySkill) {
        const int heal = ComputeSkillHeal(self.stats, skill);
        const int restored = std::min(heal, unit.hpMax - unit.hp);
        effect.value += restored * tuning.healPercent / 100;
        // A heal that lifts a unit out of lethal range saves everything it would still do: the mirror image
        // of the kill bonus. Without it a heal is only a top-up, and top-ups lose to counter-attacks.
        if (restored > 0 && unit.incoming > 0 && unit.hp <= unit.incoming && unit.hp + restored > unit.incoming) {
            effect.value += unit.threat * tuning.killHorizonTurns;
        }
        for (const FResolvedStatus& status : skill.statuses) {
            effect.value += ValueOfStatus(ctx, status, unit, true);
        }
        return effect;
    }
    if (skill.power > 0) {
        const int damage = ComputeSkillDamage(self.stats, self.attackModifier, skill, unit.stats);
        const int dealt = std::min(damage, unit.hp);
        effect.value += dealt;
        if (damage >= unit.hp) {
            effect.kills = true;
            effect.value += unit.threat * tuning.killHorizonTurns;
        } else {
            // Wounded targets are worth more: finishing them is how a focused fight is won.
            effect.value += dealt * (100 - unit.hpPercent()) * tuning.focusPercent / 10000;
        }
        if (skill.lifeOnHit > 0) {
            effect.value += std::min(skill.lifeOnHit, self.hpMax - self.hp) * tuning.healPercent / 100;
        }
    }
    if (!effect.kills) {
        for (const FResolvedStatus& status : skill.statuses) {
            effect.value += ValueOfStatus(ctx, status, unit, false);
        }
    }
    return effect;
}

[[nodiscard]] bool IsBetter(const FSkillOption& candidate, const FSkillOption& best, const FGridPosition& origin) {
    const int candidateRate = candidate.value * 100 / candidate.apCost;
    const int bestRate = best.value * 100 / best.apCost;
    if (candidateRate != bestRate) return candidateRate > bestRate;
    if (candidate.value != best.value) return candidate.value > best.value;
    if (candidate.hits != best.hits) return candidate.hits > best.hits;
    if (candidate.slot != best.slot) return candidate.slot < best.slot; // authored order breaks the tie
    return GridDistance(origin, candidate.cell) < GridDistance(origin, best.cell);
}

template <typename TEmit>
void ForEachOption(
    const FBtContext& ctx, const FGridPosition& origin, const FSkillFilter& filter, bool requireAp, TEmit&& emit) {
    const FUnitView* self = ctx.world.self();
    if (self == nullptr) {
        return;
    }
    for (std::size_t slotIndex = 0; slotIndex < ctx.world.selfSkills.size(); ++slotIndex) {
        const std::optional<FResolvedSkill>& resolved = ctx.world.selfSkills[slotIndex];
        if (!resolved || !PassesFilter(ctx, *resolved, filter)) {
            continue;
        }
        const FResolvedSkill& skill = *resolved;
        if (requireAp && ctx.world.skillAp < skill.apCost) {
            continue;
        }
        const ESkillRole role = ClassifySkill(skill);
        const bool friendly = IsFriendlySkill(skill);
        const bool affectedSideIsParty = friendly ? !ctx.hostilePartySide : ctx.hostilePartySide;

        const auto consider = [&](entt::entity target, const FGridPosition& center) {
            if (!IsInRange(origin, center, skill.range) || !HasGridLineOfSight(ctx.registry, origin, center)) {
                return;
            }
            FSkillOption option;
            for (const entt::entity entity :
                 FindUnitsOnCells(ctx.registry, ComputeSkillCells(skill, center, origin), affectedSideIsParty)) {
                const FUnitView* unit = ctx.world.find(entity);
                if (unit == nullptr || !HasGridLineOfSight(ctx.registry, origin, unit->cell)) {
                    continue; // the resolver skips units the caster cannot see
                }
                const FEffect effect = EvaluateOnUnit(ctx, skill, *unit, friendly);
                option.value += effect.value;
                option.kills = option.kills || effect.kills;
                if (effect.value > 0) {
                    ++option.hits;
                }
            }
            if (option.value <= 0 || option.hits < filter.minHits) {
                return;
            }
            if ((role == ESkillRole::Heal || role == ESkillRole::Buff) && option.value < ctx.tuning.supportMinValue) {
                return; // not worth a skill AP
            }
            if (!friendly && target != entt::null && target == ctx.blackboard.focusTarget) {
                option.value += ctx.tuning.focusStickiness;
            }
            option.slot = static_cast<int>(slotIndex);
            option.target = target;
            option.cell = center;
            option.apCost = std::max(1, skill.apCost);
            option.role = role;
            option.key = skill.skillId + "@" + std::to_string(center.x) + "," + std::to_string(center.y);
            if (ctx.blackboard.isRejected(option.key)) {
                return;
            }
            emit(std::move(option));
        };

        if (skill.target == "cell") {
            for (int y = 0; y < kGridHeight; ++y) {
                for (int x = 0; x < kGridWidth; ++x) {
                    consider(entt::null, FGridPosition{x, y});
                }
            }
            continue;
        }
        for (const FUnitView& unit : ctx.world.units) {
            if (friendly) {
                if (unit.party == ctx.hostilePartySide) continue;
                if ((skill.target == "self" || filter.selfOnly) && unit.entity != self->entity) continue;
            } else if (unit.party != ctx.hostilePartySide) {
                continue;
            }
            // A skill the caster aims at itself is aimed at the cell it would stand on.
            consider(unit.entity, unit.entity == self->entity ? origin : unit.cell);
        }
    }
}

} // namespace

int ValueOfStatus(const FBtContext& ctx, const FResolvedStatus& status, const FUnitView& target, bool targetIsAlly) {
    const gameplay::FStatusContent* definition = ctx.content.statuses.find(status.statusId);
    if (definition == nullptr) {
        return 0;
    }
    const int duration = EffectiveStatusDuration(*definition, status.durationDelta);
    const int magnitude = EffectiveStatusMagnitude(*definition, status.magnitudeDelta);
    int remaining = 0;
    for (const FStatusEffect& effect : target.effects) {
        if (effect.statusId == status.statusId) {
            remaining = effect.turnsRemaining;
            break;
        }
    }
    const int gain = std::max(0, duration - remaining); // re-applying only refreshes up to the full duration
    if (gain == 0) {
        return 0;
    }

    int worth = 0;
    bool goodForTarget = false;
    if (definition->kind == gameplay::kStatusKindDot) {
        worth = std::min(magnitude * gain, target.hp);
    } else if (definition->kind == gameplay::kStatusKindControl) {
        // Control denies turns: each denied turn costs the victim's side one turn of its output.
        const int chance = magnitude > 0 ? magnitude : 100;
        worth = target.threat * chance / 100 * gain;
    } else {
        // Stat statuses add `magnitude` to every attack; a unit that never attacks gains nothing.
        worth = target.canDamage ? magnitude * gain : 0;
        goodForTarget = true;
    }
    const int signedWorth = goodForTarget == targetIsAlly ? worth : -worth;
    return signedWorth * ctx.tuning.statusPercent / 100;
}

std::optional<FSkillOption> FindBestOption(
    const FBtContext& ctx, const FGridPosition& origin, const FSkillFilter& filter, bool requireAp) {
    std::optional<FSkillOption> best;
    ForEachOption(ctx, origin, filter, requireAp, [&](FSkillOption&& option) {
        if (!best || IsBetter(option, *best, origin)) {
            best = std::move(option);
        }
    });
    return best;
}

int FilterRange(const FBtContext& ctx, const FSkillFilter& filter) {
    int range = 0;
    for (const std::optional<FResolvedSkill>& skill : ctx.world.selfSkills) {
        if (skill && PassesFilter(ctx, *skill, filter)) {
            range = std::max(range, skill->range);
        }
    }
    return range;
}

int DangerAt(const FBtContext& ctx, const FGridPosition& cell) {
    int total = 0;
    for (const FUnitView& unit : ctx.world.units) {
        if (unit.party != ctx.hostilePartySide || !unit.canDamage) {
            continue;
        }
        const int reach = (unit.hasMoveAp ? ctx.world.moveRange : 0) + unit.damageRange;
        if (GridDistance(unit.cell, cell) <= reach) {
            total += unit.damageToSelf;
        }
    }
    return total;
}

int NearestHostileDistance(const FBtContext& ctx, const FGridPosition& cell) {
    int nearest = kFarAway;
    for (const FUnitView& unit : ctx.world.units) {
        if (unit.party == ctx.hostilePartySide) {
            nearest = std::min(nearest, GridDistance(unit.cell, cell));
        }
    }
    return nearest;
}

std::vector<int> ComputeFiringField(const FBtContext& ctx, const FSkillFilter& filter) {
    std::vector<int> field(static_cast<std::size_t>(kGridWidth * kGridHeight), kUnreachable);
    const auto* terrain = ctx.registry.ctx().find<FGridTerrain>();
    const auto passable = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < kGridWidth && y < kGridHeight && (terrain == nullptr || terrain->at(x, y) != ETile::Wall);
    };
    const auto at = [](int x, int y) { return static_cast<std::size_t>(y * kGridWidth + x); };

    std::queue<FGridPosition> pending;
    for (int y = 0; y < kGridHeight; ++y) {
        for (int x = 0; x < kGridWidth; ++x) {
            if (passable(x, y) && FindBestOption(ctx, FGridPosition{x, y}, filter, false)) {
                field[at(x, y)] = 0;
                pending.push(FGridPosition{x, y});
            }
        }
    }
    while (!pending.empty()) {
        const FGridPosition current = pending.front();
        pending.pop();
        for (const FGridPosition next : OrthogonalNeighbors(current)) {
            if (!passable(next.x, next.y) || field[at(next.x, next.y)] != kUnreachable) {
                continue;
            }
            field[at(next.x, next.y)] = field[at(current.x, current.y)] + 1;
            pending.push(next);
        }
    }
    return field;
}

int TrapDamageOnRoute(const FBtContext& ctx, const FGridPosition& destination) {
    const auto* terrain = ctx.registry.ctx().find<FGridTerrain>();
    const FUnitView* self = ctx.world.self();
    if (terrain == nullptr || self == nullptr || destination == self->cell) {
        return 0;
    }
    int traps = 0;
    for (const FGridPosition& step : FindGridPath(ctx.registry, ctx.actor, destination, ctx.world.moveRange)) {
        if (terrain->at(step.x, step.y) == ETile::Trap) {
            ++traps;
        }
    }
    return traps * terrain->trapDamage;
}

} // namespace game::ai
