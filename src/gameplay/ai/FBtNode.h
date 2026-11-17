#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/turn/TurnTypes.h"

namespace game::gameplay {
struct FContentRegistry;
}

namespace game::ai {

struct FAiBlackboard;
struct FAiTuning;
struct FAiWorld;

/// Return statuses (docs/GameAIPro_Chapter06 §6.3.2). Success/Failure are completion statuses, Running is
/// the execution hint. `Aborted` is only ever seen by onTerminate(): the parent cut the behavior short
/// (§6.3.10, "immediate termination" school).
enum class EBtStatus : std::uint8_t { Invalid, Success, Failure, Running, Aborted };

[[nodiscard]] std::string_view BtStatusName(EBtStatus status) noexcept;

/// Transient per-actor data of one node (§6.4.2 "Task"). The node tree itself is immutable and shared by
/// every actor that uses the same AI profile; everything an actor changes while executing lives here.
struct FBtSlot {
    EBtStatus status{EBtStatus::Invalid};
    std::int32_t cursor{0};  // composites: index of the active child
    std::int32_t counter{0}; // decorators and leaves: repeat count, "fresh" flag, ...
};

/// One slot per node, indexed by FBtNode::index(). Dense and contiguous (§6.4.3 transient data allocation).
struct FBtMemory {
    std::vector<FBtSlot> slots;

    void reset() {
        for (FBtSlot& slot : slots) {
            slot = FBtSlot{};
        }
    }
};

/// Everything a node may read or write while it runs. The tree stays game-agnostic; leaves reach the
/// battle through this context instead of through globals.
struct FBtContext {
    entt::registry& registry;
    const gameplay::FContentRegistry& content;
    const FAiTuning& tuning;
    const FAiWorld& world; // read-only snapshot of the battle, valid for exactly this tick
    FAiBlackboard& blackboard;
    FBtMemory& memory;
    entt::entity actor{entt::null};
    FTurnToken token{};
    bool hostilePartySide{false}; // the side the actor is fighting against
};

class FBtNode;
using FBtNodePtr = std::unique_ptr<FBtNode>;

/// The Behavior interface of §6.3.1: activated (onInitialize), run (update), deactivated (onTerminate).
/// tick() is the single entry point that enforces the contract, exactly like the wrapper in the article:
///  - onInitialize() runs once, immediately before the first update() of a run;
///  - update() runs once per tick until it stops returning Running;
///  - onTerminate() runs once, immediately after the run ended (including by abort()).
class FBtNode {
public:
    FBtNode() = default;
    FBtNode(const FBtNode&) = delete;
    FBtNode& operator=(const FBtNode&) = delete;
    virtual ~FBtNode() = default;

    [[nodiscard]] EBtStatus tick(FBtContext& ctx) const;

    /// Cuts a running behavior short. No-op when the node is not running.
    void abort(FBtContext& ctx) const;

    [[nodiscard]] EBtStatus status(const FBtContext& ctx) const { return ctx.memory.slots[static_cast<std::size_t>(index_)].status; }
    [[nodiscard]] int index() const noexcept { return index_; }

    [[nodiscard]] virtual std::string_view typeName() const noexcept = 0;
    /// Parameters for the tree dump used by analysis and diagnostics.
    [[nodiscard]] virtual std::string detail() const { return {}; }
    [[nodiscard]] virtual std::span<const FBtNodePtr> children() const noexcept { return {}; }

protected:
    virtual void onInitialize(FBtContext&) const {}
    [[nodiscard]] virtual EBtStatus update(FBtContext& ctx) const = 0;
    virtual void onTerminate(FBtContext&, EBtStatus) const {}

    [[nodiscard]] FBtSlot& slot(FBtContext& ctx) const { return ctx.memory.slots[static_cast<std::size_t>(index_)]; }

private:
    friend class FBehaviorTree;
    int index_{-1};
};

} // namespace game::ai
