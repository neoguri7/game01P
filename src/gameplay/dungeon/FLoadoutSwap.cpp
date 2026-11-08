#include "gameplay/dungeon/FLoadoutSwap.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunStart.h"

namespace game {
namespace {

/// The shared front of the swap rules (WP-10b): the run must exist, sit at camp, and carry the member/slot.
/// ValidateCampSlot's sentence pattern is mirrored on purpose — it is anonymous to FDungeonRun.cpp, and its
/// window (camp + reward) is the rune window, not this one: skills change at camp only.
std::optional<std::string> ValidateSwapSlot(
    const entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return "Run state is unavailable.";
    if (run->phase != EDungeonPhase::Camp) return "Skills can be changed at camp only.";
    if (memberIndex >= run->party.size()) return "Unknown party member.";
    if (slotIndex >= run->party[memberIndex].skills.size()) return "Unknown skill slot.";
    return std::nullopt;
}

} // namespace

std::optional<std::string> ValidateDungeonSkillSwap(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::string_view newSkillId) {
    if (const auto error = ValidateSwapSlot(registry, memberIndex, slotIndex)) return error;
    const FDungeonRun& run = registry.ctx().get<FDungeonRun>();
    const FRunMember& member = run.party[memberIndex];
    if (newSkillId == member.skills[slotIndex].skillId) return "That skill is already in this slot.";
    // The pool is the single gate (R6 P6, FRunStart.h): the authored pool minus locked unlock rows.
    const std::vector<std::string> pool = MemberSkillPool(registry, member.id);
    if (std::ranges::find(pool, newSkillId) == pool.end()) return "That skill is not in this member's pool.";
    // One copy per member, anywhere: a duplicate would draw two HUD cells for one skill.
    for (std::size_t other = 0; other < member.skills.size(); ++other) {
        if (other != slotIndex && member.skills[other].skillId == newSkillId) {
            return "That skill is already equipped in another slot.";
        }
    }
    return std::nullopt;
}

std::optional<std::string> SwapDungeonSkill(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::string_view newSkillId) {
    if (const auto error = ValidateDungeonSkillSwap(registry, memberIndex, slotIndex, newSkillId)) return error;
    auto& run = registry.ctx().get<FDungeonRun>();
    FSkillSlot& slot = run.party[memberIndex].skills[slotIndex];
    // No bench holds a detached skill instance, so every fitted rune returns to the pool in slot order
    // (WP-10b §3); re-equipping is free (D-17), so the round trip costs clicks only.
    for (std::string& runeId : slot.runeIds) run.runes.push_back(std::move(runeId));
    slot.runeIds.clear();
    slot.skillId = std::string(newSkillId);
    // Free (D-17): materials, the camp counters and the run log are untouched; one save, no new section.
    if (!SaveDungeonRun(registry)) return "Skill swapped, but the expedition could not be saved.";
    return std::nullopt;
}

std::size_t SkillSwapReturnedRunes(
    const entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex) {
    // The panel's pre-swap notice ("returns N runes"): 0 when there is no run or the index does not resolve.
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return 0;
    if (memberIndex >= run->party.size()) return 0;
    if (slotIndex >= run->party[memberIndex].skills.size()) return 0;
    return run->party[memberIndex].skills[slotIndex].runeIds.size();
}

} // namespace game
