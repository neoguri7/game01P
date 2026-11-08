#pragma once

#include <optional>
#include <string>

#include <entt/entt.hpp>

#include "presentation/FLoadoutPanel.h"

namespace game {

/// WP-10b: the camp host of the loadout panel. The camp view calls these three steps each frame
/// (Selection → BuildCampLoadoutModel → DrawLoadoutPanel → ApplyCampLoadoutEdit); tests drive the same calls.

/// The panel's UI selection, kept between frames in the registry ctx. View-owned, never saved.
struct FCampLoadoutSelection {
    int member{-1};
    int slot{-1};
};

/// The ctx selection, emplaced on first use.
[[nodiscard]] FCampLoadoutSelection& CampLoadoutSelection(entt::registry& registry);

/// Builds the panel model from the run: one member view per run.party entry (name = MemberName, slots =
/// member.skills with ContentDisplayName and runeIds.size(), pool = MemberSkillPool(member.id) with
/// equippedElsewhere and — for the selected member/slot only — swapProblem = ValidateDungeonSkillSwap). A
/// selection that no longer indexes the run is reported as -1 (the stored selection is not changed here).
/// Empty model without a run.
[[nodiscard]] FLoadoutPanelModel BuildCampLoadoutModel(entt::registry& registry, const FCampLoadoutSelection& selection);

/// Applies one panel request. Empty newSkillId → selection = {edit.memberIndex, edit.slotIndex}, nothing else
/// changes. Otherwise SwapDungeonSkill(edit…): success sets run.message to the swap result (naming the returned
/// rune count) and keeps the selection; failure sets run.message = "Cannot swap skill: <reason>". Returns the
/// SwapDungeonSkill result (nullopt for a selection change).
std::optional<std::string> ApplyCampLoadoutEdit(entt::registry& registry, FCampLoadoutSelection& selection,
                                                const FLoadoutEdit& edit);

} // namespace game
