#include "gameplay/ai/FBtComposites.h"

namespace game::ai {

void FComposite::onTerminate(FBtContext& ctx, EBtStatus status) const {
    if (status != EBtStatus::Aborted) {
        return;
    }
    for (const FBtNodePtr& child : children_) {
        child->abort(ctx);
    }
}

EBtStatus FSequence::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    const auto count = static_cast<std::int32_t>(children_.size());
    while (own.cursor < count) {
        const EBtStatus result = children_[static_cast<std::size_t>(own.cursor)]->tick(ctx);
        if (result != EBtStatus::Success) {
            return result; // failed or still running: the sequence does the same
        }
        ++own.cursor;
    }
    return EBtStatus::Success;
}

EBtStatus FSelector::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    const auto count = static_cast<std::int32_t>(children_.size());
    while (own.cursor < count) {
        const EBtStatus result = children_[static_cast<std::size_t>(own.cursor)]->tick(ctx);
        if (result != EBtStatus::Failure) {
            return result; // succeeded or still running: the selector does the same
        }
        ++own.cursor;
    }
    return EBtStatus::Failure;
}

EBtStatus FActiveSelector::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    const std::int32_t previous = own.cursor; // -1 when nothing was running
    const auto count = static_cast<std::int32_t>(children_.size());

    std::int32_t chosen = -1;
    EBtStatus result = EBtStatus::Failure;
    for (std::int32_t index = 0; index < count; ++index) {
        result = children_[static_cast<std::size_t>(index)]->tick(ctx);
        if (result != EBtStatus::Failure) {
            chosen = index;
            break;
        }
    }
    own.cursor = chosen;
    if (previous >= 0 && previous != chosen) {
        children_[static_cast<std::size_t>(previous)]->abort(ctx);
    }
    return chosen < 0 ? EBtStatus::Failure : result;
}

std::string FParallel::detail() const {
    const auto name = [](EParallelPolicy policy) { return policy == EParallelPolicy::RequireOne ? "one" : "all"; };
    return std::string("success=") + name(success_) + " failure=" + name(failure_);
}

void FParallel::onInitialize(FBtContext& ctx) const {
    // Children keep their last status between runs; clear it so "terminated" below only means "this run".
    for (const FBtNodePtr& child : children_) {
        ctx.memory.slots[static_cast<std::size_t>(child->index())].status = EBtStatus::Invalid;
    }
}

EBtStatus FParallel::update(FBtContext& ctx) const {
    if (children_.empty()) {
        return EBtStatus::Success;
    }
    std::size_t successCount = 0;
    std::size_t failureCount = 0;
    for (const FBtNodePtr& child : children_) {
        EBtStatus state = child->status(ctx);
        if (state != EBtStatus::Success && state != EBtStatus::Failure) {
            state = child->tick(ctx);
        }
        if (state == EBtStatus::Success) {
            ++successCount;
            if (success_ == EParallelPolicy::RequireOne) {
                return EBtStatus::Success;
            }
        } else if (state == EBtStatus::Failure) {
            ++failureCount;
            if (failure_ == EParallelPolicy::RequireOne) {
                return EBtStatus::Failure;
            }
        }
    }
    // Failure wins over success: the tree assumes the worst case and deals with it (§6.3.10).
    if (failure_ == EParallelPolicy::RequireAll && failureCount == children_.size()) {
        return EBtStatus::Failure;
    }
    if (success_ == EParallelPolicy::RequireAll && successCount == children_.size()) {
        return EBtStatus::Success;
    }
    return EBtStatus::Running;
}

void FParallel::onTerminate(FBtContext& ctx, EBtStatus) const {
    for (const FBtNodePtr& child : children_) {
        child->abort(ctx); // no-op for children that already finished
    }
}

} // namespace game::ai
