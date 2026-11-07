#include "presentation/FDungeonViews.h"

#include <string>

#include <imgui.h>

#include "core/EngineState.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FUiTheme.h"

namespace game {

/// The shared leave/abandon confirm. `allowLeave` offers "Save and leave" (the battle's option);
/// `viaIntent` posts the run intent instead of setting the run's flags (the camp's host owns its phase).
void DrawAbandonConfirm(entt::registry& registry, bool allowLeave, bool viaIntent) {
    auto& ui = registry.ctx().get<FDungeonUiState>();
    if (!ui.confirmAbandon) return;
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) {
        ui.confirmAbandon = false;
        return;
    }
    ImGui::OpenPopup("Abandon expedition?");
    if (ImGui::BeginPopupModal("Abandon expedition?", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        ui::HeadingText("LEAVE EXPEDITION?", 22.f);
        ImGui::Text("The expedition is saved at every room. Leaving mid-battle restarts this room");
        ImGui::Text("when you continue. Abandoning deletes the save: run EXP, nodes, runes and materials");
        ImGui::Text("are lost. The cleared-dungeon checkpoint remains.");
        if (ui::Button("Keep exploring", {180, 36})) {
            ui.confirmAbandon = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (allowLeave) {
            if (ui::Button("Save and leave", {180, 36})) {
                run->leaveRequested = true;
                ui.confirmAbandon = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ui::Button("Abandon expedition (checkpoint kept)", {260, 36})) {
            if (viaIntent) {
                PostRunIntent(registry, ERunIntent::AbandonRun);
            } else {
                run->abandonRequested = true;
            }
            ui.confirmAbandon = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/// The camp screen. The body is the original RunChoice camp branch moved verbatim; the descent button
/// posts the run intent, the "end expedition" button opens the shared abandon confirm, and the loadout
/// edits stay direct calls (they are not run transitions).
void RenderDungeonCampView(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    auto& ui = registry.ctx().get<FDungeonUiState>();
    const auto& window = registry.ctx().get<FEngineWindowState>();
    ImGui::SetNextWindowPos({window.width * .5f, window.height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({580, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({580, 0}, {580, static_cast<float>(window.height - 80)});
    ImGui::OpenPopup("Expedition choice");
    if (ImGui::BeginPopupModal("Expedition choice", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        DrawDungeonIcon(registry, EDungeonSprite::Stairs);
        ImGui::SameLine();
        ui::HeadingText("DUNGEON CLEARED / CAMP", 26.f);
        ImGui::TextWrapped("%s", run.message.c_str());
        ImGui::Separator();
        const auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
        if (!checkpoint.error.empty()) ImGui::TextColored({.9f, .31f, .31f, 1}, "%s", checkpoint.error.c_str());
        ImGui::Text("Checkpoint: dungeon %d cleared  |  materials %d", run.cleared, run.materials);
        DrawRuneOfferForecast(registry);
        ImGui::TextWrapped("Healed up to %d HP. Allocate nodes, reinforce stats and fit runes for free before descending.",
                          DungeonBalance(registry, "camp_heal"));
        const auto* growth = registry.ctx().find<FDungeonGrowth>();
        for (std::size_t memberIndex = 0; memberIndex < run.party.size(); ++memberIndex) {
            auto& member = run.party[memberIndex];
            ImGui::PushID(static_cast<int>(memberIndex));
            ImGui::SetNextItemOpen(memberIndex == 0, ImGuiCond_FirstUseEver);
            if (ImGui::TreeNode(MemberName(member).c_str())) {
                ImGui::PushID("runes");
                for (std::size_t slotIndex = 0; slotIndex < member.skills.size(); ++slotIndex) {
                    const auto& slot = member.skills[slotIndex];
                    ImGui::PushID(static_cast<int>(slotIndex));
                    ImGui::Text("%s", slot.skillId.c_str());
                    for (std::size_t runeIndex = 0; runeIndex < slot.runeIds.size(); ++runeIndex) {
                        const auto label = slot.runeIds[runeIndex] + "  [remove]";
                        if (ImGui::SmallButton(label.c_str())) {
                            if (const auto error = RemoveDungeonRune(registry, memberIndex, slotIndex, runeIndex)) {
                                run.message = "Cannot remove rune: " + *error;
                            } else {
                                run.message = "Rune returned to the collected inventory.";
                            }
                            break;
                        }
                    }
                    if (ImGui::BeginCombo("Fit rune", "Choose collected rune")) {
                        for (std::size_t index = 0; index < run.runes.size(); ++index) {
                            ImGui::PushID(static_cast<int>(index));
                            const auto problem = ValidateDungeonRuneEquip(registry, memberIndex, slotIndex, index);
                            ImGui::BeginDisabled(problem.has_value());
                            if (ImGui::Selectable(run.runes[index].c_str())) {
                                if (const auto error = EquipDungeonRune(registry, memberIndex, slotIndex, index)) {
                                    run.message = "Cannot fit rune: " + *error;
                                } else {
                                    run.message = "Rune fitted to the selected skill.";
                                }
                                ImGui::EndDisabled();
                                ImGui::PopID();
                                break;
                            }
                            ImGui::EndDisabled();
                            if (problem && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                ImGui::SetTooltip("%s", problem->c_str());
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
                if (growth != nullptr) {
                    ImGui::SeparatorText("Stats");
                    for (std::size_t upgradeIndex = 0; upgradeIndex < growth->statUpgrades.size(); ++upgradeIndex) {
                        const auto& upgrade = growth->statUpgrades[upgradeIndex];
                        const int current = DungeonMemberStatBonus(member, upgrade.stat);
                        const auto problem = ValidateDungeonStatUpgrade(registry, memberIndex, upgradeIndex);
                        const std::string label = upgrade.displayName + " +" + std::to_string(upgrade.amount)
                            + "  (" + std::to_string(upgrade.cost) + " materials, " + std::to_string(current)
                            + "/" + std::to_string(upgrade.cap) + ")";
                        ImGui::BeginDisabled(problem.has_value());
                        if (ImGui::SmallButton(label.c_str())) {
                            if (const auto error = UpgradeDungeonStat(registry, memberIndex, upgradeIndex)) {
                                run.message = "Cannot reinforce: " + *error;
                            } else {
                                run.message = upgrade.displayName + " reinforced.";
                            }
                        }
                        ImGui::EndDisabled();
                        if (problem && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("%s", problem->c_str());
                        }
                    }
                    ImGui::SeparatorText("Passive nodes");
                    DrawGrowthPanel(registry, *growth, run, memberIndex);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (ui::Button("Descend to the next dungeon", {-1, 42})) {
            PostRunIntent(registry, ERunIntent::LeaveCamp);
            ImGui::CloseCurrentPopup();
        }
        if (ui::Button("End expedition at checkpoint", {-1, 30})) {
            ui.confirmAbandon = true;
        }
        ImGui::EndPopup();
    }
    DrawAbandonConfirm(registry, false, true);
}

} // namespace game
