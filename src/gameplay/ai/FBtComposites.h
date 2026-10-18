#pragma once

#include <vector>

#include "gameplay/ai/FBtNode.h"

namespace game::ai {

/// Base of every branch with several children (§6.3.6). Aborting a composite aborts whatever it was running.
class FComposite : public FBtNode {
public:
    explicit FComposite(std::vector<FBtNodePtr> children) : children_(std::move(children)) {}

    [[nodiscard]] std::span<const FBtNodePtr> children() const noexcept override { return children_; }

protected:
    void onTerminate(FBtContext& ctx, EBtStatus status) const override;

    std::vector<FBtNodePtr> children_;
};

/// §6.3.7: runs children in order until one fails or keeps running. The next child starts in the same
/// update as soon as the previous one succeeds, so the tree never loses a decision to a frame boundary.
class FSequence final : public FComposite {
public:
    using FComposite::FComposite;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Sequence"; }

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).cursor = 0; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;
};

/// §6.3.9: the fallback counterpart of the sequence: first child that succeeds or keeps running wins.
/// Passive: once a child is running it keeps the selector until it finishes.
class FSelector final : public FComposite {
public:
    using FComposite::FComposite;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Selector"; }

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).cursor = 0; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;
};

/// §6.3.12: a selector that re-checks its decision every tick. Higher-priority children get another
/// chance before the one that was running; if a different child wins, the previous one is aborted.
class FActiveSelector final : public FComposite {
public:
    using FComposite::FComposite;
    [[nodiscard]] std::string_view typeName() const noexcept override { return "ActiveSelector"; }

protected:
    void onInitialize(FBtContext& ctx) const override { slot(ctx).cursor = -1; }
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;
};

/// §6.3.10: how many children must finish before the parallel does.
enum class EParallelPolicy : std::uint8_t { RequireOne, RequireAll };

/// §6.3.10: all children advance in the same tick (sequentially, not threaded). When the parallel ends
/// early every child that is still running is aborted.
class FParallel final : public FComposite {
public:
    FParallel(std::vector<FBtNodePtr> children, EParallelPolicy success, EParallelPolicy failure)
        : FComposite(std::move(children)), success_(success), failure_(failure) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "Parallel"; }
    [[nodiscard]] std::string detail() const override;

protected:
    void onInitialize(FBtContext& ctx) const override;
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override;
    void onTerminate(FBtContext& ctx, EBtStatus status) const override;

private:
    EParallelPolicy success_;
    EParallelPolicy failure_;
};

} // namespace game::ai
