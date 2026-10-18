#include "gameplay/ai/FBtNode.h"

#include "gameplay/ai/FBehaviorTree.h"

namespace game::ai {

std::string_view BtStatusName(EBtStatus status) noexcept {
    switch (status) {
    case EBtStatus::Invalid: return "Invalid";
    case EBtStatus::Success: return "Success";
    case EBtStatus::Failure: return "Failure";
    case EBtStatus::Running: return "Running";
    case EBtStatus::Aborted: return "Aborted";
    }
    return "Unknown";
}

EBtStatus FBtNode::tick(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    if (own.status != EBtStatus::Running) {
        onInitialize(ctx);
    }
    own.status = update(ctx);
    if (own.status != EBtStatus::Running) {
        onTerminate(ctx, own.status);
    }
    return own.status;
}

void FBtNode::abort(FBtContext& ctx) const {
    FBtSlot& own = slot(ctx);
    if (own.status != EBtStatus::Running) {
        return;
    }
    own.status = EBtStatus::Aborted;
    onTerminate(ctx, EBtStatus::Aborted);
}

FBehaviorTree::FBehaviorTree(FBtNodePtr root) : root_(std::move(root)) {
    if (root_ == nullptr) {
        return;
    }
    // Pre-order indices: a parent always precedes its children, so a run walks memory front to back.
    std::vector<FBtNode*> pending{root_.get()};
    while (!pending.empty()) {
        FBtNode* node = pending.back();
        pending.pop_back();
        node->index_ = static_cast<int>(nodeCount_++);
        const std::span<const FBtNodePtr> kids = node->children();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

FBtMemory FBehaviorTree::makeMemory() const {
    return FBtMemory{std::vector<FBtSlot>(nodeCount_)};
}

namespace {

void describeNode(const FBtNode& node, int depth, std::string& out) {
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
    out += node.typeName();
    if (const std::string detail = node.detail(); !detail.empty()) {
        out += '(';
        out += detail;
        out += ')';
    }
    out += '\n';
    for (const FBtNodePtr& child : node.children()) {
        describeNode(*child, depth + 1, out);
    }
}

} // namespace

std::string FBehaviorTree::describe() const {
    std::string out;
    if (root_ != nullptr) {
        describeNode(*root_, 0, out);
    }
    return out;
}

} // namespace game::ai
