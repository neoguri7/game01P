#include "presentation/FCampLoadoutHost.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FLoadoutSwap.h"
#include "gameplay/dungeon/FRunStart.h"
#include "presentation/FDungeonViews.h"

namespace game {

namespace {

/// A negative index can only come from a caller bug; the maximum size lands out of range, so the swap rules
/// answer with their own refusal instead of this layer inventing one.
std::size_t RuleIndex(int index) {
    return index >= 0 ? static_cast<std::size_t>(index) : std::numeric_limits<std::size_t>::max();
}

/// The skill in the slot right now, or empty when the index does not resolve. Read as a copy: comparing the
/// pre-swap state with the post-swap slot detects "the swap applied despite the returned reason" (the
/// failed-save case) without mistaking the already-in-this-slot refusal for an applied swap.
std::string SlotSkillId(entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr || memberIndex >= run->party.size()) return {};
    const std::vector<FSkillSlot>& skills = run->party[memberIndex].skills;
    if (slotIndex >= skills.size()) return {};
    return skills[slotIndex].skillId;
}

/// The plural of "rune" for `count` — call sites keep their own adjectives ("returned").
const char* RuneNoun(std::size_t count) {
    return count == 1 ? "rune" : "runes";
}

} // namespace

FCampLoadoutSelection& CampLoadoutSelection(entt::registry& registry) {
    if (auto* selection = registry.ctx().find<FCampLoadoutSelection>()) return *selection;
    return registry.ctx().emplace<FCampLoadoutSelection>();
}

FLoadoutPanelModel BuildCampLoadoutModel(entt::registry& registry, const FCampLoadoutSelection& selection) {
    FLoadoutPanelModel model;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return model;
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();

    // The stored selection may point at a party that has changed: only a member+slot pair that indexes the
    // current run is reported as selected; anything else displays as "none" (the stored value is left
    // alone — a later click overwrites it).
    const bool memberSelected = selection.member >= 0 &&
        static_cast<std::size_t>(selection.member) < run->party.size();
    const bool slotSelected = memberSelected && selection.slot >= 0 &&
        static_cast<std::size_t>(selection.slot) <
            run->party[static_cast<std::size_t>(selection.member)].skills.size();
    model.selectedMember = slotSelected ? selection.member : -1;
    model.selectedSlot = slotSelected ? selection.slot : -1;

    model.members.reserve(run->party.size());
    for (std::size_t memberIndex = 0; memberIndex < run->party.size(); ++memberIndex) {
        const FRunMember& member = run->party[memberIndex];
        FLoadoutMemberView view;
        view.memberId = member.id;
        view.name = MemberName(member);
        const bool isSelectedMember = static_cast<int>(memberIndex) == model.selectedMember;
        view.slots.reserve(member.skills.size());
        for (const FSkillSlot& slot : member.skills) {
            FLoadoutSlotView slotView;
            slotView.skillId = slot.skillId;
            const auto* row = content != nullptr ? content->skills.find(slot.skillId) : nullptr;
            slotView.displayName = ContentDisplayName(row, slot.skillId);
            slotView.runeCount = static_cast<int>(slot.runeIds.size());
            view.slots.push_back(std::move(slotView));
        }
        for (const std::string& skillId : MemberSkillPool(registry, member.id)) {
            FLoadoutPoolEntry entry;
            entry.skillId = skillId;
            const auto* row = content != nullptr ? content->skills.find(skillId) : nullptr;
            entry.displayName = ContentDisplayName(row, skillId);
            for (std::size_t slotIndex = 0; slotIndex < member.skills.size(); ++slotIndex) {
                if (member.skills[slotIndex].skillId != skillId) continue;
                // A skill fitted in the slot being replaced is "here", not elsewhere; a second copy cannot
                // exist (the duplicate rule), so the first hit decides.
                entry.equippedElsewhere = !(isSelectedMember &&
                    slotIndex == static_cast<std::size_t>(model.selectedSlot));
                break;
            }
            if (isSelectedMember) {
                // Host-filled refusal reason for the selected member only; the panel shows it as the hover
                // tooltip and never validates again.
                entry.swapProblem = ValidateDungeonSkillSwap(
                    registry, static_cast<std::size_t>(model.selectedMember),
                    static_cast<std::size_t>(model.selectedSlot), skillId).value_or("");
            }
            view.pool.push_back(std::move(entry));
        }
        model.members.push_back(std::move(view));
    }
    return model;
}

std::optional<std::string> ApplyCampLoadoutEdit(entt::registry& registry, FCampLoadoutSelection& selection,
                                                const FLoadoutEdit& edit) {
    if (edit.newSkillId.empty()) {
        selection.member = edit.memberIndex;
        selection.slot = edit.slotIndex;
        return std::nullopt;
    }
    const std::size_t memberIndex = RuleIndex(edit.memberIndex);
    const std::size_t slotIndex = RuleIndex(edit.slotIndex);
    // Both reads must happen before the swap moves the runes out and changes the slot.
    const std::size_t returnedRunes = SkillSwapReturnedRunes(registry, memberIndex, slotIndex);
    const std::string previousSkill = SlotSkillId(registry, memberIndex, slotIndex);
    const std::optional<std::string> result =
        SwapDungeonSkill(registry, memberIndex, slotIndex, edit.newSkillId);
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return result;
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    const auto* row = content != nullptr ? content->skills.find(edit.newSkillId) : nullptr;
    const std::string skillName = ContentDisplayName(row, edit.newSkillId);
    if (result) {
        // A failed save returns a reason after the swap already applied: the slot now holds the new skill,
        // so "Cannot swap skill" would contradict what the player sees. A refusal leaves the slot unchanged
        // and keeps the refusal wording.
        const bool applied = previousSkill != edit.newSkillId &&
                             SlotSkillId(registry, memberIndex, slotIndex) == edit.newSkillId;
        run->message = applied ? skillName + " equipped, but the expedition could not be saved."
                               : "Cannot swap skill: " + *result;
        return result;
    }
    std::string message = skillName + " equipped.";
    if (returnedRunes > 0) {
        message += " " + std::to_string(returnedRunes) + " " + RuneNoun(returnedRunes) +
                   " returned to the collected inventory.";
    }
    run->message = std::move(message);
    return std::nullopt;
}

} // namespace game
