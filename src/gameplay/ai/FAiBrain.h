#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiLibrary.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/ai/FBehaviorTree.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"

namespace game::ai {

/// ECS component: one actor's mind. The tree and the tuning come from a shared profile; the memory and the
/// blackboard are this actor's own (§6.4.2: shared nodes, per-actor tasks).
struct FAiBrain {
    std::shared_ptr<const FBehaviorTree> tree;
    FAiTuning tuning;
    FBtMemory memory;
    FAiBlackboard blackboard;
    std::string profileId;
};

[[nodiscard]] FAiBrain MakeBrain(const FAiProfile& profile);

/// Runs one decision for the actor named by `token`: detects a new turn (aborting and clearing whatever the
/// tree was doing), builds the battle snapshot, ticks the tree once and returns the action it issued.
/// Nullopt means the tree had nothing (more) to do this turn — the caller ends the turn. An actor without a
/// brain gets one from the default profile on first use; without an FAiLibrary in the context the answer is
/// nullopt (and the turn simply ends).
[[nodiscard]] std::optional<FBattleAction> RunBrain(
    entt::registry& registry,
    FTurnToken token,
    bool hostilePartySide,
    const gameplay::FContentRegistry& content);

} // namespace game::ai
