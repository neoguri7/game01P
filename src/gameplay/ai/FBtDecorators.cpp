#include "gameplay/ai/FBtDecorators.h"

#include <limits>

#include "gameplay/ai/FAiBlackboard.h"

namespace game::ai {
namespace {

constexpr int kNever = std::numeric_limits<int>::min();

} // namespace

EBtStatus FRepeat::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    // Bounded: every pass either returns or raises the counter toward times_.
    while (true) {
        const EBtStatus result = child_->tick(ctx);
        if (result != EBtStatus::Success) {
            return result; // still running, or a failing child fails the repeat
        }
        if (++own.counter >= times_) {
            return EBtStatus::Success;
        }
    }
}

EBtStatus FInverter::update(FBtContext& ctx) const {
    const EBtStatus result = child_->tick(ctx);
    if (result == EBtStatus::Success) {
        return EBtStatus::Failure;
    }
    if (result == EBtStatus::Failure) {
        return EBtStatus::Success;
    }
    return result;
}

EBtStatus FSucceeder::update(FBtContext& ctx) const {
    const EBtStatus result = child_->tick(ctx);
    return result == EBtStatus::Failure ? EBtStatus::Success : result;
}

EBtStatus FWatch::update(FBtContext& ctx) const {
    const EBtStatus result = child_->tick(ctx);
    return result == EBtStatus::Success ? EBtStatus::Running : result;
}

EBtStatus FCooldown::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    const std::string stampKey = "cooldown:" + key_;
    if (own.counter == 0) { // entering: the cooldown is checked once per run, not while the child is running
        const int last = ctx.blackboard.mark(stampKey, kNever);
        if (last != kNever && ctx.blackboard.turnsTaken - last < turns_) {
            return EBtStatus::Failure;
        }
        own.counter = 1;
    }
    const EBtStatus result = child_->tick(ctx);
    if (result != EBtStatus::Failure && own.counter == 1) {
        ctx.blackboard.setMark(stampKey, ctx.blackboard.turnsTaken);
        own.counter = 2;
    }
    return result;
}

EBtStatus FOnce::update(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    const std::string flagKey = "once:" + key_;
    if (own.counter == 0) {
        if (ctx.blackboard.mark(flagKey, 0) != 0) {
            return EBtStatus::Failure;
        }
        own.counter = 1;
    }
    const EBtStatus result = child_->tick(ctx);
    if (result != EBtStatus::Failure && own.counter == 1) {
        ctx.blackboard.setMark(flagKey, 1);
        own.counter = 2;
    }
    return result;
}

} // namespace game::ai
