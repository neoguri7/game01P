#include "presentation/FDungeonOverlay.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include "core/EngineState.h"
#include "core/InputState.h"
#include "debug/DebugOverlay.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

void Menu(entt::registry& registry) {
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const float w = static_cast<float>(window.width);
    const float h = static_cast<float>(window.height);
    const float tile = 64.f;
    const float left = w * .1f;
    const float top = h * .5f - 128;
    // Drawn through ImGui's background list so the backdrop can be dimmed without dimming the preview.
    auto* back = ImGui::GetBackgroundDrawList();
    const auto place = [&](EDungeonSprite sprite, float x, float y) {
        DrawDungeonImage(registry, sprite, back, {x, y}, {x + tile, y + tile});
    };
    for (float y = 0; y < h; y += tile) {
        for (float x = 0; x < w; x += tile) place(EDungeonSprite::Backdrop, x, y);
    }
    back->AddRectFilled({0, 0}, {w, h}, IM_COL32(3, 5, 10, 190));
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 5; ++x) {
            place((y == 0 || x == 0 || x == 4) ? EDungeonSprite::Wall : EDungeonSprite::Floor, left + x * tile, top + y * tile);
        }
    }
    place(EDungeonSprite::Warrior, left + 64, top + 128);
    place(EDungeonSprite::Archer, left + 128, top + 128);
    place(EDungeonSprite::Mage, left + 192, top + 128);
    place(EDungeonSprite::Healer, left + 128, top + 192);
    place(EDungeonSprite::Imp, left + 128, top + 64);
    ui::BeginPanel("Ember dungeon", {w * .46f, h * .2f}, {w * .5f, h * .56f});
    ImGui::TextDisabled("GRID TACTICS  /  ROGUELIKE PROTOTYPE");
    ImGui::Spacing();
    ui::HeadingText("EMBER DUNGEON", 44.f);
    ImGui::Spacing();
    ImGui::TextWrapped("Four adventurers. One more descent.\nMove carefully, combine skills with runes, and survive the guardian.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("3 combat rooms + a shrine event per dungeon.\nClear a dungeon to unlock its checkpoint.\nDefeat resets run growth, not cleared checkpoints.");
    ImGui::Spacing();
    const auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
    auto& request = registry.ctx().get<FMenuRequest>();
    const std::string begin = checkpoint.cleared > 0
        ? "BEGIN AT DUNGEON " + std::to_string(checkpoint.cleared + 1) : "BEGIN EXPEDITION";
    if (ui::Button(begin.c_str(), {-1, 52})) request.value = EMenuRequest::Start;
    ImGui::Spacing();
    if (checkpoint.cleared > 0 && ui::Button("Fresh expedition from dungeon 1", {-1, 36})) {
        request.value = EMenuRequest::Start;
        request.fresh = true;
    }
    if (ui::Button("Quit", {-1, 34})) request.value = EMenuRequest::Quit;
    if (!checkpoint.error.empty()) ImGui::TextColored({.9f, .31f, .31f, 1}, "%s", checkpoint.error.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Mouse: move / target    WASD: one-cell move");
    ImGui::TextDisabled("1-4: skill    Space: end turn    Esc: abandon");
    ImGui::TextDisabled("` : developer overlay    No real-time pressure.");
    ui::EndPanel();
}

void RunChoice(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    auto& ui = registry.ctx().get<FDungeonUiState>();
    if (const auto* input = registry.ctx().find<FInputState>();
        input && input->isActionPressed(EInputAction::Cancel)) ui.confirmAbandon = true;
    if (ui.confirmAbandon) {
        ImGui::OpenPopup("Abandon expedition?");
        if (ImGui::BeginPopupModal("Abandon expedition?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ui::DecorateCurrentWindow();
            ui::HeadingText("ABANDON EXPEDITION?", 22.f);
            ImGui::Text("Run growth and runes will be lost. Cleared checkpoints remain.");
            if (ui::Button("Keep exploring", {220, 36})) {
                ui.confirmAbandon = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::Button("Abandon", {160, 36})) {
                run.abandonRequested = true;
                ui.confirmAbandon = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return;
    }
    if (run.phase == EDungeonPhase::Battle) return;
    const auto& window = registry.ctx().get<FEngineWindowState>();
    ImGui::SetNextWindowPos({window.width * .5f, window.height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({580, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({580, 0}, {580, static_cast<float>(window.height - 80)});
    ImGui::OpenPopup("Expedition choice");
    bool retry = false;
    if (ImGui::BeginPopupModal("Expedition choice", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        const char* heading = run.phase == EDungeonPhase::Reward ? "ROOM SECURED" :
            run.phase == EDungeonPhase::Event ? "A QUIET SHRINE" :
            run.phase == EDungeonPhase::Camp ? "DUNGEON CLEARED / CAMP" : "EXPEDITION LOST";
        DrawDungeonIcon(registry, run.phase == EDungeonPhase::Reward ? EDungeonSprite::Chest :
                        run.phase == EDungeonPhase::Event ? EDungeonSprite::Shrine :
                        run.phase == EDungeonPhase::Camp ? EDungeonSprite::Stairs : EDungeonSprite::Guardian);
        ImGui::SameLine();
        ui::HeadingText(heading, 26.f);
        ImGui::TextWrapped("%s", run.message.c_str());
        ImGui::Separator();
        if (run.phase == EDungeonPhase::Reward) {
            ImGui::TextDisabled("Run level %d  |  EXP %d  |  materials %d", run.level, run.experience, run.materials);
            const std::string labels[]{
                "VIGOR  /  +" + std::to_string(DungeonBalance(registry, "reward_health")) + " max HP and heal",
                "MIGHT  /  +" + std::to_string(DungeonBalance(registry, "reward_attack")) + " physical attack",
                "FOCUS  /  +" + std::to_string(DungeonBalance(registry, "reward_magic")) + " magic attack"};
            for (int choice = 0; choice < 3; ++choice) {
                if (ui::Button(labels[choice].c_str(), {-1, 42})) {
                    ChooseDungeonReward(registry, choice);
                    ImGui::CloseCurrentPopup();
                }
            }
        } else if (run.phase == EDungeonPhase::Event) {
            const auto restLabel = "Rest at the shrine  /  heal " +
                std::to_string(DungeonBalance(registry, "event_heal")) + " HP";
            const auto cacheLabel = "Open the cache  /  gain " +
                std::to_string(DungeonBalance(registry, "event_materials")) + " materials";
            if (ui::Button(restLabel.c_str(), {-1, 42})) {
                ChooseDungeonEvent(registry, true);
                ImGui::CloseCurrentPopup();
            }
            if (ui::Button(cacheLabel.c_str(), {-1, 42})) {
                ChooseDungeonEvent(registry, false);
                ImGui::CloseCurrentPopup();
            }
        } else if (run.phase == EDungeonPhase::Camp) {
            const auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
            if (!checkpoint.error.empty()) ImGui::TextColored({.9f, .31f, .31f, 1}, "%s", checkpoint.error.c_str());
            ImGui::Text("Checkpoint: dungeon %d cleared  |  materials %d", run.cleared, run.materials);
            ImGui::TextWrapped("Healed up to %d HP. Fit collected runes for free before descending.",
                              DungeonBalance(registry, "camp_heal"));
            for (std::size_t memberIndex = 0; memberIndex < run.party.size(); ++memberIndex) {
                const auto& member = run.party[memberIndex];
                ImGui::PushID(static_cast<int>(memberIndex));
                if (ImGui::TreeNode(member.id.c_str())) {
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
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            const auto reinforceLabel = "Reinforce (+" + std::to_string(DungeonBalance(registry, "reward_attack")) +
                " attack/+" + std::to_string(DungeonBalance(registry, "reward_magic")) + " magic, " +
                std::to_string(DungeonBalance(registry, "camp_cost")) + " materials) & descend";
            ImGui::BeginDisabled(run.materials < DungeonBalance(registry, "camp_cost"));
            if (ui::Button(reinforceLabel.c_str(), {-1, 42})) {
                LeaveDungeonCamp(registry, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (ui::Button("Descend without reinforcement", {-1, 36})) {
                LeaveDungeonCamp(registry, false);
                ImGui::CloseCurrentPopup();
            }
            if (ui::Button("End expedition at checkpoint", {-1, 30})) {
                run.abandonRequested = true;
                ImGui::CloseCurrentPopup();
            }
        } else {
            if (ui::Button("New expedition from checkpoint", {-1, 44})) {
                retry = true;
                ImGui::CloseCurrentPopup();
            }
            if (ui::Button("Return to camp", {-1, 32})) {
                run.abandonRequested = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (retry) {
        StartDungeonRun(registry, static_cast<std::uint32_t>(SDL_GetTicks()));
        registry.ctx().get<FDungeonRun>().advanceRequested = true;
    }
}

} // namespace

void InitializeDungeonUi(entt::registry& registry) {
    registry.ctx().emplace<FDungeonUiState>();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigNavCaptureKeyboard = false;
    ui::ApplyTheme();
}

void RenderDungeonOverlay(entt::registry& registry, const Time& time, const SystemManager& systems) {
    auto& ui = registry.ctx().get<FDungeonUiState>();
    if (const auto* input = registry.ctx().find<FInputState>();
        input && input->isActionPressed(EInputAction::DebugToggle)) ui.debugVisible = !ui.debugVisible;
    if (registry.ctx().contains<FDungeonRun>()) RunChoice(registry);
    else Menu(registry);
    if (ui.debugVisible) RenderDebugOverlay(registry, time, systems);
}

} // namespace game
