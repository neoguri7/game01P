#pragma once

#include <string>

#include "gameplay/ai/FBtNode.h"

namespace game::ai {

/// §6.3.5: a branch with exactly one child that adds a rule around it.
class FDecorator : public FBtNode {
public:
    explicit FDecorator(FBtNodePtr child) : child_(std::move(child)) {}

    [[nodiscard]] std::span<const FBtNodePtr> children() const noexcept override { return {&child_, 1}; }

protected:
    void onTerminate(FBtContext& ctx, EBtStatus status) const override {
        if (status == EBtStatus::Aborted) {
            child_->abort(ctx);
        }
    }

    FBtNodePtr child_;
};

/// Runs the child `times` times in a row; a failing child fails the repeat (the article's Repeat).
/// An unbounded repeat is deliberately not offered: a child that succeeds without acting would never
/// return control to the turn flow.
class FRepeat final : public FDecorator {
public:
    FRepeat(FBtNodePtr child, int times) : FDecorator(std::move(child)), times_(times < 1 ? 1 : times) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "Repeat"; }
    [[nodiscard]] std::string detail() const override { return std::to_string(times_); }

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).counter = 0; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;

private:
    int times_;
};

/// §6.3.4 negation: swaps Success and Failure, passes Running through.
class FInverter final : public FDecorator {
public:
    using FDecorator::FDecorator;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Inverter"; }

protected:
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;
};

/// Hides the child's failure: used to make a step optional inside a sequence.
class FSucceeder final : public FDecorator {
public:
    using FDecorator::FDecorator;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Succeeder"; }

protected:
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;
};

/// §6.3.4 monitoring mode for a condition: keeps running while the condition holds, fails when it stops
/// holding. This is what lets a Parallel supervise the assumptions of a long behavior (§6.3.11).
class FWatch final : public FDecorator {
public:
    using FDecorator::FDecorator;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Watch"; }

protected:
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;
};

/// §6.3.8 filter by time: the child may start again only `turns` of the actor's own turns after it last
/// did something. turns = 1 therefore means "once per turn". The stamp is taken when the child succeeds
/// or keeps running; a child that failed immediately costs nothing.
class FCooldown final : public FDecorator {
public:
    FCooldown(FBtNodePtr child, std::string key, int turns)
        : FDecorator(std::move(child)), key_(std::move(key)), turns_(turns < 1 ? 1 : turns) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "Cooldown"; }
    [[nodiscard]] std::string detail() const override { return key_ + ", " + std::to_string(turns_); }

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).counter = 0; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;

private:
    std::string key_;
    int turns_;
};

/// The child may do something once per battle (a phase transition, an enrage cry, ...).
class FOnce final : public FDecorator {
public:
    FOnce(FBtNodePtr child, std::string key) : FDecorator(std::move(child)), key_(std::move(key)) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "Once"; }
    [[nodiscard]] std::string detail() const override { return key_; }

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).counter = 0; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;

private:
    std::string key_;
};

} // namespace game::ai
