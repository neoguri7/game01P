#pragma once

#include <functional>
#include <string>

#include "gameplay/ai/FAiQueries.h"
#include "gameplay/ai/FBtNode.h"

namespace game::ai {

/// A condition (§6.3.4, instant-check mode): one yes/no question about the battle. Conditions are small
/// closures built from authored parameters by FAiNodeRegistry; they carry no state.
class FCondition final : public FBtNode {
public:
    using FTest = std::function<bool(FBtContext&)>;

    FCondition(std::string name, std::string detail, FTest test)
        : name_(std::move(name)), detail_(std::move(detail)), test_(std::move(test)) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return name_; }
    [[nodiscard]] std::string detail() const override { return detail_; }

protected:
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override {
        return test_(ctx) ? EBtStatus::Success : EBtStatus::Failure;
    }

private:
    std::string name_;
    std::string detail_;
    FTest test_;
};

/// Deliberately does nothing and succeeds: the tree ends the actor's turn. Useful as an explicit last
/// resort ("hold position") inside a selector.
class FWait final : public FBtNode {
public:
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Wait"; }

protected:
    [[nodiscard]] EBtStatus update(FBtContext&) const override { return EBtStatus::Success; }
};

/// Action: use the best skill that passes the filter, valued in expected HP swing per skill AP. The leaf issues
/// the action and stays Running; on the next tick it learns from the AP pool whether the resolver really
/// carried it out (Success) or rejected it (Failure, and the attempt is not repeated this turn).
class FUseSkill final : public FBtNode {
public:
    explicit FUseSkill(FSkillFilter filter) : filter_(std::move(filter)) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "UseSkill"; }
    [[nodiscard]] std::string detail() const override;

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).counter = 0; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;

private:
    FSkillFilter filter_;
};

/// Action: take the move that puts the actor in the best place to use its skills — close in until a strike is
/// possible, stay at the far end of its range, keep out of reach of what can hit it. Fails when standing
/// still is already the best choice, so a sequence can follow it with UseSkill.
class FEngage final : public FBtNode {
public:
    explicit FEngage(FSkillFilter filter) : filter_(std::move(filter)) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "Engage"; }
    [[nodiscard]] std::string detail() const override;

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).counter = 0; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;

private:
    FSkillFilter filter_;
};

/// Action: move to the reachable cell where the least can hit the actor. Fails when no cell is clearly safer.
class FRetreat final : public FBtNode {
public:
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Retreat"; }

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).counter = 0; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;
};

/// WP-15b condition helper (FAiNodeRegistry's `BossPhaseAtLeast`): true when the actor is a boss body whose
/// action-side phase (`FBossGoals::behaviorPhase`, promoted only after the transition's round) is at least
/// `phase` (1-based — the "<name> enters phase K" numbering). A unit without FBossGoals (the fallback
/// guardian, every ordinary unit) is never at a phase, so the leaf can gate existing profiles safely.
[[nodiscard]] bool SelfBossPhaseAtLeast(const FBtContext& ctx, int phase);

[[nodiscard]] std::string DescribeFilter(const FSkillFilter& filter);

} // namespace game::ai
