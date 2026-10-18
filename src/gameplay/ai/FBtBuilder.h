#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "gameplay/ai/FBtComposites.h"
#include "gameplay/ai/FBtDecorators.h"
#include "gameplay/ai/FBtLeaf.h"

namespace game::ai {

/// The Tree Builder pattern of §6.2.1: constructing a tree is separated from the node classes, and reads
/// like the tree it builds.
///
///     FBtBuilder().activeSelector()
///         .sequence().condition("InRange", inRange).action("Fire", fire).end()
///         .action("Approach", approach)
///         .build();
///
/// Composites stay open until end(); decorators wrap exactly the next node and close themselves.
/// Misuse (end() without an open branch, two roots, an unfinished decorator) is a programming error and
/// throws std::logic_error — trees that come from data go through FAiNodeRegistry instead, which reports
/// problems as values.
class FBtBuilder {
public:
    FBtBuilder& sequence() { return open(EKind::Sequence); }
    FBtBuilder& selector() { return open(EKind::Selector); }
    FBtBuilder& activeSelector() { return open(EKind::ActiveSelector); }
    FBtBuilder& parallel(EParallelPolicy success, EParallelPolicy failure) {
        Frame& frame = push(EKind::Parallel);
        frame.success = success;
        frame.failure = failure;
        return *this;
    }

    FBtBuilder& repeat(int times) {
        push(EKind::Repeat).times = times;
        return *this;
    }
    FBtBuilder& inverter() { return open(EKind::Inverter); }
    FBtBuilder& succeeder() { return open(EKind::Succeeder); }
    FBtBuilder& watch() { return open(EKind::Watch); }
    FBtBuilder& cooldown(std::string key, int turns) {
        Frame& frame = push(EKind::Cooldown);
        frame.key = std::move(key);
        frame.times = turns;
        return *this;
    }
    FBtBuilder& once(std::string key) {
        push(EKind::Once).key = std::move(key);
        return *this;
    }

    FBtBuilder& condition(std::string name, FBtLeaf::FUpdate update) { return node(makeLeaf(std::move(name), std::move(update))); }
    FBtBuilder& action(std::string name, FBtLeaf::FUpdate update) { return node(makeLeaf(std::move(name), std::move(update))); }

    /// Adds any prebuilt node (a game leaf, or a whole subtree).
    FBtBuilder& node(FBtNodePtr child) {
        attach(std::move(child));
        return *this;
    }

    FBtBuilder& end() {
        if (frames_.empty() || isDecorator(frames_.back().kind)) {
            throw std::logic_error("FBtBuilder::end() without an open branch");
        }
        closeTop();
        return *this;
    }

    [[nodiscard]] FBtNodePtr build() {
        while (!frames_.empty()) {
            if (isDecorator(frames_.back().kind)) {
                throw std::logic_error("FBtBuilder: decorator without a child");
            }
            closeTop();
        }
        if (root_ == nullptr) {
            throw std::logic_error("FBtBuilder: empty tree");
        }
        return std::move(root_);
    }

private:
    enum class EKind { Sequence, Selector, ActiveSelector, Parallel, Repeat, Inverter, Succeeder, Watch, Cooldown, Once };

    struct Frame {
        EKind kind;
        std::vector<FBtNodePtr> kids;
        int times{1};
        std::string key;
        EParallelPolicy success{EParallelPolicy::RequireOne};
        EParallelPolicy failure{EParallelPolicy::RequireOne};
    };

    [[nodiscard]] static bool isDecorator(EKind kind) {
        return kind == EKind::Repeat || kind == EKind::Inverter || kind == EKind::Succeeder || kind == EKind::Watch ||
               kind == EKind::Cooldown || kind == EKind::Once;
    }

    [[nodiscard]] static FBtNodePtr makeLeaf(std::string name, FBtLeaf::FUpdate update) {
        return std::make_unique<FBtLeaf>(std::move(name), std::move(update));
    }

    FBtBuilder& open(EKind kind) {
        push(kind);
        return *this;
    }

    Frame& push(EKind kind) {
        frames_.push_back(Frame{kind, {}, 1, {}, EParallelPolicy::RequireOne, EParallelPolicy::RequireOne});
        return frames_.back();
    }

    [[nodiscard]] static FBtNodePtr make(Frame&& frame) {
        switch (frame.kind) {
        case EKind::Sequence: return std::make_unique<FSequence>(std::move(frame.kids));
        case EKind::Selector: return std::make_unique<FSelector>(std::move(frame.kids));
        case EKind::ActiveSelector: return std::make_unique<FActiveSelector>(std::move(frame.kids));
        case EKind::Parallel:
            return std::make_unique<FParallel>(std::move(frame.kids), frame.success, frame.failure);
        case EKind::Repeat: return std::make_unique<FRepeat>(std::move(frame.kids.front()), frame.times);
        case EKind::Inverter: return std::make_unique<FInverter>(std::move(frame.kids.front()));
        case EKind::Succeeder: return std::make_unique<FSucceeder>(std::move(frame.kids.front()));
        case EKind::Watch: return std::make_unique<FWatch>(std::move(frame.kids.front()));
        case EKind::Cooldown:
            return std::make_unique<FCooldown>(std::move(frame.kids.front()), std::move(frame.key), frame.times);
        case EKind::Once: return std::make_unique<FOnce>(std::move(frame.kids.front()), std::move(frame.key));
        }
        throw std::logic_error("FBtBuilder: unknown node kind");
    }

    void closeTop() {
        Frame frame = std::move(frames_.back());
        frames_.pop_back();
        attach(make(std::move(frame)));
    }

    void attach(FBtNodePtr child) {
        if (frames_.empty()) {
            if (root_ != nullptr) {
                throw std::logic_error("FBtBuilder: a tree has exactly one root");
            }
            root_ = std::move(child);
            return;
        }
        Frame& top = frames_.back();
        top.kids.push_back(std::move(child));
        if (isDecorator(top.kind)) {
            closeTop(); // a decorator wraps exactly one node
        }
    }

    std::vector<Frame> frames_;
    FBtNodePtr root_;
};

} // namespace game::ai
