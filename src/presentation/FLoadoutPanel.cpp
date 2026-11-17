#include "presentation/FLoadoutPanel.h"

#include <cstddef>
#include <optional>
#include <string>

#include <imgui.h>

#include "presentation/FUiTheme.h"

namespace game {

namespace {

/// Dimming factor for an unselected slot's frame: the style's own header colour, faded, so every slot
/// reads as a clickable plate while the selected one keeps the full accent (no new hard-coded colour).
constexpr float kUnselectedSlotAlpha = 0.4f;

/// Compact slot item width: the label plus a little room inside the selection frame, so a member's slots
/// stay on one line (the camp modal is 580 px wide). Selectable() with an explicit width draws at that
/// width instead of filling the whole row.
float SlotItemWidth(const std::string& label) {
    return ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.f;
}

/// The plural of "rune" for `count` — call sites keep their own adjectives ("fitted").
const char* RuneNoun(int count) {
    return count == 1 ? "rune" : "runes";
}

} // namespace

std::optional<FLoadoutEdit> DrawLoadoutPanel(const FLoadoutPanelModel& model) {
    std::optional<FLoadoutEdit> edit;
    ImGui::TextDisabled("Click a skill slot to replace it.");
    for (std::size_t memberIndex = 0; memberIndex < model.members.size(); ++memberIndex) {
        const FLoadoutMemberView& member = model.members[memberIndex];
        const int memberOrdinal = static_cast<int>(memberIndex);
        ImGui::PushID(memberOrdinal);
        ImGui::TextColored(ui::palette::kGoldV, "%s", member.name.c_str());
        const bool memberSelected = memberOrdinal == model.selectedMember;
        for (std::size_t slotIndex = 0; slotIndex < member.slots.size(); ++slotIndex) {
            if (slotIndex > 0) ImGui::SameLine();
            const FLoadoutSlotView& slot = member.slots[slotIndex];
            std::string label = slot.displayName;
            if (slot.runeCount > 0) {
                label += " (" + std::to_string(slot.runeCount) + " " + RuneNoun(slot.runeCount) + ")";
            }
            const bool selected = memberSelected && static_cast<int>(slotIndex) == model.selectedSlot;
            ImGui::PushID(static_cast<int>(slotIndex));
            // Every slot passes selected=true so its frame always draws (imgui renders the frame only for a
            // highlighted or selected Selectable); the colour carries the selection instead: unselected
            // slots push a dimmed header colour, the selected one keeps the full accent.
            // NoAutoClosePopups: the panel lives in the camp modal (a popup window) — a click must not
            // close it.
            if (!selected) {
                ImVec4 faint = ImGui::GetStyleColorVec4(ImGuiCol_Header);
                faint.w *= kUnselectedSlotAlpha;
                ImGui::PushStyleColor(ImGuiCol_Header, faint);
            }
            if (ImGui::Selectable(label.c_str(), true, ImGuiSelectableFlags_NoAutoClosePopups,
                                  ImVec2(SlotItemWidth(label), 0.f))) {
                if (!edit) {
                    edit = FLoadoutEdit{memberOrdinal, static_cast<int>(slotIndex), std::string{}};
                }
            }
            if (!selected) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        const bool slotSelected = memberSelected && model.selectedSlot >= 0 &&
            static_cast<std::size_t>(model.selectedSlot) < member.slots.size();
        if (slotSelected) {
            const std::size_t slotIndex = static_cast<std::size_t>(model.selectedSlot);
            const FLoadoutSlotView& target = member.slots[slotIndex];
            ImGui::Indent();
            ImGui::Text("Replace %s:", target.displayName.c_str());
            if (target.runeCount > 0) {
                ImGui::TextDisabled("Swapping returns %d fitted %s to the collected inventory.",
                                    target.runeCount, RuneNoun(target.runeCount));
            }
            // Pool entries nest under the selected slot's scope, so an entry index can never hash to the
            // same widget id as a slot index of the same member.
            ImGui::PushID(static_cast<int>(slotIndex));
            for (std::size_t poolIndex = 0; poolIndex < member.pool.size(); ++poolIndex) {
                const FLoadoutPoolEntry& entry = member.pool[poolIndex];
                const bool disabled = !entry.swapProblem.empty();
                std::string entryLabel = entry.displayName;
                if (entry.equippedElsewhere) entryLabel += " (equipped)";
                const bool greyed = entry.equippedElsewhere && !disabled;
                if (greyed) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                }
                ImGui::PushID(static_cast<int>(poolIndex));
                ImGui::BeginDisabled(disabled);
                if (ImGui::Selectable(entryLabel.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups) && !edit) {
                    edit = FLoadoutEdit{memberOrdinal, static_cast<int>(slotIndex), entry.skillId};
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                if (greyed) ImGui::PopStyleColor();
                if (disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", entry.swapProblem.c_str());
                }
            }
            ImGui::PopID();
            ImGui::Unindent();
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Skill swaps are free (D-17); fitted runes return to the collected inventory.");
    return edit;
}

} // namespace game
