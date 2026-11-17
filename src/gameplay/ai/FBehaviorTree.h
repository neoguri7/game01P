#pragma once

#include <string>

#include "gameplay/ai/FBtNode.h"

namespace game::ai {

/// Owns an immutable node tree and hands out per-actor memory for it (§6.2.2 / §6.4.2). One tree is built
/// per AI profile and shared by every actor running that profile; actors only differ in their FBtMemory.
class FBehaviorTree {
public:
    explicit FBehaviorTree(FBtNodePtr root);

    /// The article's tick(): one traversal from the root. In this game one tick is one decision.
    [[nodiscard]] EBtStatus tick(FBtContext& ctx) const { return root_->tick(ctx); }
    void abort(FBtContext& ctx) const { root_->abort(ctx); }

    [[nodiscard]] FBtMemory makeMemory() const;
    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodeCount_; }
    [[nodiscard]] const FBtNode& root() const { return *root_; }

    /// Indented dump of the tree, one node per line, for diagnostics and the analysis report.
    [[nodiscard]] std::string describe() const;

private:
    FBtNodePtr root_;
    std::size_t nodeCount_{0};
};

} // namespace game::ai
