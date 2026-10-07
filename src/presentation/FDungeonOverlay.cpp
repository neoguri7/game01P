#include "presentation/FDungeonOverlay.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include "core/EngineState.h"
#include "core/InputState.h"
#include "debug/DebugOverlay.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "presentation/FDungeonArt.h"

namespace game {
namespace {

void Menu(entt::registry& registry) {
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const float w = static_cast<float>(window.width);
    const float h = static_cast<float>(window.height);
    const float tile = 64.f;
    const float left = w * .1f;
    const float top = h * .5f - 128;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 5; ++x) {
            DrawDungeonSprite(registry, (y == 0 || x == 0 || x == 4) ? EDungeonSprite::Wall : EDungeonSprite::Floor,
                              {left + x * tile, top + y * tile, tile, tile});
        }
    }
    DrawDungeonSprite(registry, EDungeonSprite::Warrior, {left + 64, top + 128, 64, 64});
    DrawDungeonSprite(registry, EDungeonSprite::Archer, {left + 128, top + 128, 64, 64});
    DrawDungeonSprite(registry, EDungeonSprite::Mage, {left + 192, top + 128, 64, 64});
    DrawDungeonSprite(registry, EDungeonSprite::Healer, {left + 128, top + 192, 64, 64});
    DrawDungeonSprite(registry, EDungeonSprite::Imp, {left + 128, top + 64, 64, 64});

    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos({w * .48f, h * .23f});
    ImGui::SetNextWindowSize({w * .46f, 0});
    ImGui::SetNextWindowScroll({0, 0});
    ImGui::Begin("Ember dungeon", nullptr, flags | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextDisabled("GRID TACTICS  /  ROGUELIKE PROTOTYPE");
    ImGui::Spacing();
    ImGui::SetWindowFontScale(2.f);
    ImGui::TextColored({1.f, .86f, .24f, 1.f}, "EMBER DUNGEON");
    ImGui::SetWindowFontScale(1.f);
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
    if (ImGui::Button(begin.c_str(), {-1, 46})) request.value = EMenuRequest::Start;
    if (checkpoint.cleared > 0 && ImGui::Button("Fresh expedition from dungeon 1", {-1, 32})) {
        request.value = EMenuRequest::Start;
        request.fresh = true;
    }
    if (ImGui::Button("Quit", {-1, 30})) request.value = EMenuRequest::Quit;
    if (!checkpoint.error.empty()) ImGui::TextColored({.9f, .31f, .31f, 1}, "%s", checkpoint.error.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Mouse: move / target    WASD: one-cell move");
    ImGui::TextDisabled("1-4: skill    Space: end turn    Esc: abandon");
    ImGui::TextDisabled("` : developer overlay    No real-time pressure.");
    ImGui::End();
}

void RunChoice(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    auto& ui = registry.ctx().get<FDungeonUiState>();
    if (const auto* input = registry.ctx().find<FInputState>();
        input && input->isActionPressed(EInputAction::Cancel)) ui.confirmAbandon = true;
    if (ui.confirmAbandon) {
        ImGui::OpenPopup("Abandon expedition?");
        if (ImGui::BeginPopupModal("Abandon expedition?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Run growth and runes will be lost. Cleared checkpoints remain.");
            if (ImGui::Button("Keep exploring", {220, 36})) {
                ui.confirmAbandon = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Abandon", {160, 36})) {
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
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        const char* heading = run.phase == EDungeonPhase::Reward ? "ROOM SECURED" :
            run.phase == EDungeonPhase::Event ? "A QUIET SHRINE" :
            run.phase == EDungeonPhase::Camp ? "DUNGEON CLEARED / CAMP" : "EXPEDITION LOST";
        DrawDungeonIcon(registry, run.phase == EDungeonPhase::Reward ? EDungeonSprite::Chest :
                        run.phase == EDungeonPhase::Event ? EDungeonSprite::Shrine :
                        run.phase == EDungeonPhase::Camp ? EDungeonSprite::Stairs : EDungeonSprite::Guardian);
        ImGui::SameLine();
        ImGui::TextColored({1, .86f, .24f, 1}, "%s", heading);
        ImGui::TextWrapped("%s", run.message.c_str());
        ImGui::Separator();
        if (run.phase == EDungeonPhase::Reward) {
            ImGui::TextDisabled("Run level %d  |  EXP %d  |  materials %d", run.level, run.experience, run.materials);
            const std::string labels[]{
                "VIGOR  /  +" + std::to_string(DungeonBalance(registry, "reward_health")) + " max HP and heal",
                "MIGHT  /  +" + std::to_string(DungeonBalance(registry, "reward_attack")) + " physical attack",
                "FOCUS  /  +" + std::to_string(DungeonBalance(registry, "reward_magic")) + " magic attack"};
            for (int choice = 0; choice < 3; ++choice) {
                if (ImGui::Button(labels[choice].c_str(), {-1, 42})) {
                    ChooseDungeonReward(registry, choice);
                    ImGui::CloseCurrentPopup();
                }
            }
        } else if (run.phase == EDungeonPhase::Event) {
            const auto restLabel = "Rest at the shrine  /  heal " +
                std::to_string(DungeonBalance(registry, "event_heal")) + " HP";
            const auto cacheLabel = "Open the cache  /  gain " +
                std::to_string(DungeonBalance(registry, "event_materials")) + " materials";
            if (ImGui::Button(restLabel.c_str(), {-1, 42})) {
                ChooseDungeonEvent(registry, true);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button(cacheLabel.c_str(), {-1, 42})) {
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
            if (ImGui::Button(reinforceLabel.c_str(), {-1, 42})) {
                LeaveDungeonCamp(registry, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Descend without reinforcement", {-1, 36})) {
                LeaveDungeonCamp(registry, false);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("End expedition at checkpoint", {-1, 30})) {
                run.abandonRequested = true;
                ImGui::CloseCurrentPopup();
            }
        } else {
            if (ImGui::Button("New expedition from checkpoint", {-1, 44})) {
                retry = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Return to camp", {-1, 32})) {
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
    io.FontGlobalScale = 1.2f;
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 4;
    style.FrameRounding = 3;
    style.WindowPadding = {18, 14};
    style.ItemSpacing = {8, 7};
    style.Colors[ImGuiCol_WindowBg] = {.075f, .08f, .11f, .98f};
    style.Colors[ImGuiCol_PopupBg] = {.085f, .09f, .12f, 1.f};
    style.Colors[ImGuiCol_ModalWindowDimBg] = {.015f, .02f, .035f, .65f};
    style.Colors[ImGuiCol_Button] = {.17f, .22f, .31f, 1.f};
    style.Colors[ImGuiCol_ButtonHovered] = {.25f, .36f, .5f, 1.f};
    style.Colors[ImGuiCol_ButtonActive] = {.31f, .55f, 1.f, 1.f};
    style.Colors[ImGuiCol_Header] = {.19f, .26f, .37f, 1.f};
    style.Colors[ImGuiCol_HeaderHovered] = {.25f, .36f, .5f, 1.f};
    style.Colors[ImGuiCol_Text] = {.9f, .9f, .86f, 1.f};
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
