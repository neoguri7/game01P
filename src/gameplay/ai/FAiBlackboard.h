#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FGridPosition.h"
#include "gameplay/combat/FCombatTypes.h"

namespace game::ai {

/// What the brain issued on the previous tick. A leaf that issues an action stays Running; on the next
/// tick it uses this record to learn whether the resolver actually carried the action out (AP spent /
/// position changed) and answers Success or Failure accordingly.
struct FAiPendingAction {
    enum class EKind : std::uint8_t { None, Skill, Move };

    EKind kind{EKind::None};
    int skillApBefore{0};
    FGridPosition cellBefore{};
    std::string attemptKey; // identifies the attempt in the per-turn rejected list
};

/// Per-actor knowledge that outlives a single tick (§6.2 "blackboard"). Behavior state that belongs to the
/// tree lives in FBtMemory; this holds the facts the tree shares with the rest of the battle.
struct FAiBlackboard {
    // --- lives for the whole battle ---
    bool started{false};
    std::uint64_t lastTurnSerial{0};
    int turnsTaken{0}; // the actor's own turns so far, 1 during its first turn
    entt::entity focusTarget{entt::null}; // sticky target: the last unit this actor attacked
    std::vector<std::pair<std::string, int>> marks; // cooldown stamps and once-flags, keyed by authored name

    // --- lives for one turn ---
    FAiPendingAction pending;
    std::vector<std::string> rejected; // attempts the resolver did not carry out; never retried this turn

    // --- lives for one tick ---
    std::optional<FBattleAction> decision;
    std::string reason; // why the decision was taken; diagnostics only

    [[nodiscard]] int mark(std::string_view key, int fallback) const {
        for (const auto& [name, value] : marks) {
            if (name == key) {
                return value;
            }
        }
        return fallback;
    }

    void setMark(std::string_view key, int value) {
        for (auto& [name, stored] : marks) {
            if (name == key) {
                stored = value;
                return;
            }
        }
        marks.emplace_back(std::string(key), value);
    }

    [[nodiscard]] bool isRejected(std::string_view key) const {
        for (const std::string& entry : rejected) {
            if (entry == key) {
                return true;
            }
        }
        return false;
    }

    /// A tick may issue at most one action. False when a leaf already issued one this tick.
    bool issue(FBattleAction action, FAiPendingAction record, std::string why) {
        if (decision.has_value()) {
            return false;
        }
        decision = std::move(action);
        pending = std::move(record);
        reason = std::move(why);
        return true;
    }
};

} // namespace game::ai
