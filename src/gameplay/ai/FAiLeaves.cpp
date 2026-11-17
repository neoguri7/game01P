#include "gameplay/ai/FAiLeaves.h"

#include <algorithm>
#include <limits>

#include "ecs/components/FActionPoints.h"
#include "ecs/components/FBossGoals.h"
#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTypes.h"

namespace game::ai {
namespace {

constexpr int kRetreatDistanceCap = 8;

[[nodiscard]] std::string MoveKey(const FGridPosition& destination) {
    return "move@" + std::to_string(destination.x) + "," + std::to_string(destination.y);
}

/// Reads back what the previous tick issued: did the resolver actually carry it out? An attempt that did
/// not take effect is remembered for the rest of the turn so the tree cannot ask for it again.
[[nodiscard]] EBtStatus VerifyIssued(FBtContext& ctx, FAiPendingAction::EKind expected) {
    FAiBlackboard& blackboard = ctx.blackboard;
    const FAiPendingAction pending = blackboard.pending;
    blackboard.pending = FAiPendingAction{};
    if (pending.kind != expected) {
        return EBtStatus::Failure; // a new turn wiped it, or another leaf's action is in flight
    }
    bool tookEffect = false;
    if (expected == FAiPendingAction::EKind::Skill) {
        const auto* points = ctx.registry.try_get<FActionPoints>(ctx.actor);
        tookEffect = points != nullptr && points->skill < pending.skillApBefore;
    } else {
        const auto* cell = ctx.registry.try_get<FGridPosition>(ctx.actor);
        tookEffect = cell != nullptr && !(*cell == pending.cellBefore);
    }
    if (!tookEffect) {
        blackboard.rejected.push_back(pending.attemptKey);
    }
    return tookEffect ? EBtStatus::Success : EBtStatus::Failure;
}

/// The cells the actor could end this tick's move on: where it stands, plus everything its move AP reaches.
[[nodiscard]] std::vector<FGridPosition> StandingCandidates(const FBtContext& ctx, const FUnitView& self) {
    std::vector<FGridPosition> cells{self.cell};
    if (ctx.world.moveAp > 0) {
        for (const FGridPosition& cell : ComputeReachableCells(ctx.registry, ctx.actor, ctx.world.moveRange)) {
            cells.push_back(cell);
        }
    }
    return cells;
}

struct FStandingScore {
    FGridPosition cell{};
    int score{std::numeric_limits<int>::min()};
};

/// Scores every legal cell with `scoreCell`, then moves only when the best other cell clearly beats standing
/// still. Returns Running (move issued) or Failure (staying put is best / no move possible).
template <typename TScore>
[[nodiscard]] EBtStatus IssueBestMove(FBtContext& ctx, std::string_view why, TScore&& scoreCell) {
    const FUnitView* self = ctx.world.self();
    if (self == nullptr || ctx.world.moveAp <= 0) {
        return EBtStatus::Failure;
    }
    int stayScore = 0;
    FStandingScore best;
    for (const FGridPosition& cell : StandingCandidates(ctx, *self)) {
        const bool staying = cell == self->cell;
        const int trapLoss = staying ? 0 : TrapDamageOnRoute(ctx, cell);
        if (!staying && (trapLoss >= self->hp || ctx.blackboard.isRejected(MoveKey(cell)))) {
            continue; // never walk into lethal spikes, never repeat a move the resolver refused
        }
        const int score = scoreCell(cell) - trapLoss * ctx.tuning.trapPercent / 100;
        if (staying) {
            stayScore = score;
        } else if (score > best.score) {
            best = FStandingScore{cell, score};
        }
    }
    if (best.score == std::numeric_limits<int>::min() || best.score < stayScore + ctx.tuning.moveHysteresis) {
        return EBtStatus::Failure;
    }
    FAiPendingAction pending;
    pending.kind = FAiPendingAction::EKind::Move;
    pending.cellBefore = self->cell;
    pending.attemptKey = MoveKey(best.cell);
    const std::string reason = std::string(why) + " " + MoveKey(best.cell) + " score " + std::to_string(best.score) +
                               " (staying " + std::to_string(stayScore) + ")";
    if (!ctx.blackboard.issue(FMoveRequest{ctx.actor, best.cell, ctx.token}, std::move(pending), reason)) {
        return EBtStatus::Failure;
    }
    return EBtStatus::Running;
}

} // namespace

std::string DescribeFilter(const FSkillFilter& filter) {
    std::string out;
    const auto add = [&out](const std::string& part) {
        if (!out.empty()) out += ", ";
        out += part;
    };
    for (const ESkillRole role : {ESkillRole::Damage, ESkillRole::Heal, ESkillRole::Buff, ESkillRole::Debuff, ESkillRole::Utility}) {
        if ((filter.roles & RoleBit(role)) != 0 && filter.roles != kRolesAny) {
            add(std::string(SkillRoleName(role)));
        }
    }
    for (const std::string& id : filter.skillIds) add(id);
    for (const std::string& tag : filter.tags) add("#" + tag);
    if (filter.selfOnly) add("self");
    if (filter.minHits > 1) add("hits>=" + std::to_string(filter.minHits));
    return out.empty() ? "any" : out;
}

bool SelfBossPhaseAtLeast(const FBtContext& ctx, int phase) {
    const auto* goals = ctx.registry.try_get<FBossGoals>(ctx.actor);
    return goals != nullptr && goals->behaviorPhase + 1 >= phase;
}

std::string FUseSkill::detail() const { return DescribeFilter(filter_); }

EBtStatus FUseSkill::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    if (own.counter == 1) {
        return VerifyIssued(ctx, FAiPendingAction::EKind::Skill);
    }
    const FUnitView* self = ctx.world.self();
    if (self == nullptr) {
        return EBtStatus::Failure;
    }
    const std::optional<FSkillOption> best = FindBestOption(ctx, self->cell, filter_, true);
    if (!best) {
        return EBtStatus::Failure;
    }
    const FResolvedSkill& skill = *ctx.world.selfSkills[static_cast<std::size_t>(best->slot)];

    FAiPendingAction pending;
    pending.kind = FAiPendingAction::EKind::Skill;
    pending.skillApBefore = ctx.world.skillAp;
    pending.cellBefore = self->cell;
    pending.attemptKey = best->key;
    std::string reason = skill.skillId + " -> ";
    reason += best->target != entt::null ? UnitDisplayName(ctx.registry, best->target)
                                         : "(" + std::to_string(best->cell.x) + "," + std::to_string(best->cell.y) + ")";
    reason += " value " + std::to_string(best->value) + " hits " + std::to_string(best->hits);
    if (best->kills) reason += " kill";

    if (!ctx.blackboard.issue(
            FActionRequest{ctx.actor, best->target, skill.skillId, ctx.token, best->cell}, std::move(pending), reason)) {
        return EBtStatus::Failure;
    }
    if (best->role == ESkillRole::Damage && best->target != entt::null) {
        ctx.blackboard.focusTarget = best->target; // keep hitting the same unit while it is a good choice
    }
    own.counter = 1;
    return EBtStatus::Running;
}

std::string FEngage::detail() const { return DescribeFilter(filter_); }

EBtStatus FEngage::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    if (own.counter == 1) {
        return VerifyIssued(ctx, FAiPendingAction::EKind::Move);
    }
    const int rangeCap = FilterRange(ctx, filter_);
    const std::vector<int> field = ComputeFiringField(ctx, filter_);
    const FAiTuning& tuning = ctx.tuning;

    const EBtStatus result = IssueBestMove(ctx, "engage", [&](const FGridPosition& cell) {
        const std::optional<FSkillOption> strike = FindBestOption(ctx, cell, filter_, true);
        const bool engaged = strike.has_value() || FindBestOption(ctx, cell, filter_, false).has_value();
        int score = strike ? strike->value : 0;
        if (engaged) {
            // In position: prefer the far end of the skill's range and cells few enemies can hit next turn.
            score += tuning.rangeKeepPoints * std::min(NearestHostileDistance(ctx, cell), rangeCap);
            score -= DangerAt(ctx, cell) * tuning.dangerPercent / 100;
        } else {
            // Not there yet: every step closer to a firing position is progress.
            score -= field[static_cast<std::size_t>(cell.y * kGridWidth + cell.x)] * tuning.progressPoints;
        }
        return score;
    });
    if (result == EBtStatus::Running) {
        own.counter = 1;
    }
    return result;
}

EBtStatus FRetreat::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    if (own.counter == 1) {
        return VerifyIssued(ctx, FAiPendingAction::EKind::Move);
    }
    // WP-14: generalise the Retreat node: a unit already out of every hostile's reach gains nothing from
    // backing away, so it stays put instead of kiting across the floor.
    if (const FUnitView* self = ctx.world.self(); self != nullptr && DangerAt(ctx, self->cell) == 0) {
        return EBtStatus::Failure;
    }
    const FAiTuning& tuning = ctx.tuning;
    const EBtStatus result = IssueBestMove(ctx, "retreat", [&](const FGridPosition& cell) {
        return tuning.rangeKeepPoints * std::min(NearestHostileDistance(ctx, cell), kRetreatDistanceCap) -
               DangerAt(ctx, cell);
    });
    if (result == EBtStatus::Running) {
        own.counter = 1;
    }
    return result;
}

} // namespace game::ai
