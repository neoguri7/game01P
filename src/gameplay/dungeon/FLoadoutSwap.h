#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

namespace game {

/// WP-10b: the camp skill swap (확정 game-design §1/§6.1, D-17 — "skills and runes swap freely, at no cost").
/// One rule set, three entry points; the camp loadout panel only displays what these return.
///
/// Validation (each failure is the English reason the UI shows; the first failing check wins):
///   no run                                   → "Run state is unavailable."
///   run.phase != Camp                        → "Skills can be changed at camp only."
///   memberIndex out of range                 → "Unknown party member."
///   slotIndex out of range                   → "Unknown skill slot."
///   newSkillId == the slot's current skill   → "That skill is already in this slot."
///   newSkillId not in MemberSkillPool(member.id) (authored pool minus locked unlocks — the single pool rule,
///   FRunStart.h; never re-derived here)       → "That skill is not in this member's pool."
///   newSkillId in another slot of the member → "That skill is already equipped in another slot."
/// Tags are not checked: every fitted rune is returned, so the new slot holds zero runes and
/// ValidateSkillSlot({newSkillId, {}}) holds by the boot validation (ValidateRosterContent).
///
/// The registry is non-const because the pool rule reads the meta unlock state through a non-const API
/// (W10 review §5 item 6 — same behaviour, matched signature).
[[nodiscard]] std::optional<std::string> ValidateDungeonSkillSwap(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::string_view newSkillId);

/// Validates, then applies: every rune fitted to the slot moves back to run.runes (in slot order, appended),
/// the slot's skillId becomes newSkillId, and the run is saved once (SaveDungeonRun — the existing skills +
/// runes serialization; no new save section). Free: run.materials and the camp counters are untouched, and
/// no run-log line is written (the swap log is a later WP). A failed save does not roll the swap back (the
/// rune/stat camp APIs' rule); it returns "Skill swapped, but the expedition could not be saved.".
[[nodiscard]] std::optional<std::string> SwapDungeonSkill(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::string_view newSkillId);

/// How many fitted runes a swap of this slot would return (= slot.runeIds.size()); 0 without a run or for an
/// out-of-range index. The panel's pre-swap notice ("returns N runes") reads this.
[[nodiscard]] std::size_t SkillSwapReturnedRunes(
    const entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex);

} // namespace game
