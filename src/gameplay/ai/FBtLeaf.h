#pragma once

#include <functional>
#include <string>

#include "gameplay/ai/FBtNode.h"

namespace game::ai {

/// A leaf backed by a callable: the quickest way to write an action or a condition in C++ (the article's
/// `.condition(IsPlayerVisible)` / `.action(FireAtPlayer)`). Game leaves with parameters are proper classes
/// in FAiLeaves.h; this one serves tests, prototypes and one-off behaviors.
class FBtLeaf final : public FBtNode {
public:
    using FUpdate = std::function<EBtStatus(FBtContext&)>;
    using FTerminate = std::function<void(FBtContext&, EBtStatus)>;

    FBtLeaf(std::string name, FUpdate update, FTerminate terminate = {})
        : name_(std::move(name)), update_(std::move(update)), terminate_(std::move(terminate)) {}

    [[nodiscard]] std::string_view typeName() const noexcept override { return "Leaf"; }
    [[nodiscard]] std::string detail() const override { return name_; }

protected:
    [[nodiscard]] EBtStatus update(FBtContext& ctx) const override { return update_(ctx); }
    void onTerminate(FBtContext& ctx, EBtStatus status) const override {
        if (terminate_) {
            terminate_(ctx, status);
        }
    }

private:
    std::string name_;
    FUpdate update_;
    FTerminate terminate_;
};

} // namespace game::ai
