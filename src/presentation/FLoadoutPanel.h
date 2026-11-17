#pragma once

#include <optional>
#include <string>
#include <vector>

namespace game {

/// WP-10b: the loadout panel — a pure display of a host-built model that returns at most one edit request per
/// frame (ImGui immediate-mode idiom: the widget returns its interaction). The panel never touches the
/// registry, never validates and never applies a swap: the host fills every display string (names from
/// FContentRegistry, refusal reasons from ValidateDungeonSkillSwap) and applies the returned edit through
/// FLoadoutSwap. Host today: the camp view (FCampLoadoutHost); the preparation screen integration is a later WP.
/// Extension (WP-34 synergy display): add fields to these view structs; existing hosts keep compiling.

struct FLoadoutSlotView {
    std::string skillId;
    std::string displayName;
    int runeCount{0};  // fitted runes; a swap of this slot returns all of them (the pre-swap notice)
};

struct FLoadoutPoolEntry {
    std::string skillId;
    std::string displayName;
    bool equippedElsewhere{false};  // in another slot of the same member (drawn greyed); FLoadoutSwap refuses it
    std::string swapProblem;        // host-filled ValidateDungeonSkillSwap reason for the selected slot; empty = can swap
};

struct FLoadoutMemberView {
    std::string memberId;
    std::string name;                       // display name
    std::vector<FLoadoutSlotView> slots;    // the member's active slots (4, or more with slot promotion), in order
    std::vector<FLoadoutPoolEntry> pool;    // swappable pool (MemberSkillPool — unlocks included), in pool order
};

struct FLoadoutPanelModel {
    std::vector<FLoadoutMemberView> members;
    int selectedMember{-1};  // host-owned selection (input); -1 = none
    int selectedSlot{-1};
};

/// One panel interaction. Empty newSkillId = a selection change (slot clicked), else an apply request (pool
/// skill clicked for the selected slot).
struct FLoadoutEdit {
    int memberIndex{-1};
    int slotIndex{-1};
    std::string newSkillId;
};

/// Draws the panel into the current ImGui window. Returns the interaction of this frame, if any.
[[nodiscard]] std::optional<FLoadoutEdit> DrawLoadoutPanel(const FLoadoutPanelModel& model);

} // namespace game
