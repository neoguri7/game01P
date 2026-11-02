#include "presentation/FDungeonOverlay.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include "core/EngineState.h"
#include "core/InputState.h"
#include "debug/DebugOverlay.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FPassiveTreePanel.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

const std::string& MemberName(const FRunMember& member) {
    return member.displayName.empty() ? member.id : member.displayName;
}

std::string DescribeSave(const FDungeonSave& save) {
    const char* where = save.phase == EDungeonPhase::Battle ? "battle"
        : save.phase == EDungeonPhase::Reward ? "room cleared"
        : save.phase == EDungeonPhase::Event ? "shrine" : "camp";
    std::string text = "Dungeon " + std::to_string(save.depth) + ", room " + std::to_string(save.room + 1) +
        " (" + where + ")";
    if (save.savedAt.size() >= 10) text += "  |  saved " + save.savedAt.substr(0, 10);
    return text;
}

/// Starting over while a save exists asks first: that save is the only copy of the run.
void NewExpeditionConfirm(entt::registry& registry) {
    auto& state = registry.ctx().get<FDungeonUiState>();
    if (!state.confirmNewExpedition) return;
    const auto& save = registry.ctx().get<FDungeonSave>();
    auto& request = registry.ctx().get<FMenuRequest>();
    ImGui::OpenPopup("Start a new expedition?");
    if (ImGui::BeginPopupModal("Start a new expedition?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        ui::HeadingText("NEW EXPEDITION?", 22.f);
        ImGui::Text("Your saved expedition will be overwritten:");
        ImGui::TextDisabled("%s", DescribeSave(save).c_str());
        if (ui::Button("Keep my save", {200, 36})) {
            state.confirmNewExpedition = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::Button("Start new", {160, 36})) {
            request.value = EMenuRequest::Start;
            request.fresh = state.newExpeditionFresh;
            state.confirmNewExpedition = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// A title-menu button: the HUD button picture under an ImGui button with a clear face, so the label still
// reaches ImGui's text log.
bool MenuButton(entt::registry& registry, const char* label, ImVec2 size, float textSize) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max{min.x + size.x, min.y + size.y};
    const bool hot = ImGui::IsMouseHoveringRect(min, max);
    ui::DrawNineSlice(ImGui::GetWindowDrawList(), DungeonSkinPiece(registry, EDungeonSprite::HudButton), min, max, 16.f,
                      hot ? IM_COL32(255, 236, 190, 255) : IM_COL32_WHITE);
    constexpr ImVec4 clear{0.f, 0.f, 0.f, 0.f};
    ImGui::PushStyleColor(ImGuiCol_Button, clear);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, clear);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, clear);
    ImGui::PushStyleColor(ImGuiCol_Text, hot ? ui::palette::kGoldBrightV : ui::palette::kGoldV);
    ImFont* heading = ui::HeadingFont();
    if (heading) ImGui::PushFont(heading, textSize);
    const bool pressed = ImGui::Button(label, size);
    if (heading) ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return pressed;
}

// The title screen: the painting fills the window, the logo and a short menu float over it.
void Menu(entt::registry& registry) {
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const float w = static_cast<float>(window.width);
    const float h = static_cast<float>(window.height);
    const float u = std::min(h / 800.f, w / 1160.f);

    auto* back = ImGui::GetBackgroundDrawList();
    const auto painting = DungeonSpriteSize(registry, EDungeonSprite::TitleBg);
    if (painting.x > 0.f && painting.y > 0.f) {
        // Cover the window and crop the overflow evenly, whatever its shape.
        const float cover = std::max(w / painting.x, h / painting.y);
        const ImVec2 size{painting.x * cover, painting.y * cover};
        const ImVec2 min{(w - size.x) * .5f, (h - size.y) * .5f};
        DrawDungeonImage(registry, EDungeonSprite::TitleBg, back, min, {min.x + size.x, min.y + size.y});
    }
    // Sink the lower half so the menu reads over any painting.
    back->AddRectFilledMultiColor({0.f, h * .3f}, {w, h}, IM_COL32(3, 4, 8, 0), IM_COL32(3, 4, 8, 0),
                                  IM_COL32(3, 4, 8, 205), IM_COL32(3, 4, 8, 205));

    const auto logo = DungeonSpriteSize(registry, EDungeonSprite::TitleLogo) * u;
    const ImVec2 logoMin{std::floor((w - logo.x) * .5f), std::floor(h * .1f)};
    DrawDungeonImage(registry, EDungeonSprite::TitleLogo, back, logoMin, {logoMin.x + logo.x, logoMin.y + logo.y});

    const float column = 340.f * u;
    const float menuTop = logoMin.y + logo.y + 44.f * u;
    ui::BeginZone("Ember dungeon", {0.f, 0.f}, {w, h});
    const auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
    const auto& save = registry.ctx().get<FDungeonSave>();
    auto& request = registry.ctx().get<FMenuRequest>();
    auto& state = registry.ctx().get<FDungeonUiState>();
    const auto startNew = [&](bool fresh) {
        if (save.exists) {
            state.confirmNewExpedition = true;
            state.newExpeditionFresh = fresh;
            return;
        }
        request.value = EMenuRequest::Start;
        request.fresh = fresh;
    };

    {
        // The font scope must close before the window does; ImGui asserts on an unbalanced font stack in End().
        ui::FBodyFont body(17.f * u);
        const float x = (w - column) * .5f;
        const ImVec2 tall{column, 52.f * u};
        const ImVec2 short_{column, 44.f * u};
        const float gap = 10.f * u;
        const auto centered = [&](float y, const std::string& text, ImVec4 color) {
            ImGui::SetCursorScreenPos({(w - ImGui::CalcTextSize(text.c_str()).x) * .5f, y});
            ImGui::TextColored(color, "%s", text.c_str());
        };
        centered(menuTop - 30.f * u, "Four adventurers. One more descent.", ui::palette::kTextV);

        float y = menuTop;
        if (save.exists) {
            ImGui::SetCursorScreenPos({x, y});
            if (MenuButton(registry, "CONTINUE", tall, 20.f * u)) request.value = EMenuRequest::Continue;
            y += tall.y + 2.f * u;
            centered(y, DescribeSave(save), ui::palette::kTextV);
            y += 24.f * u;
        }
        const std::string begin = checkpoint.cleared > 0
            ? "BEGIN AT DUNGEON " + std::to_string(checkpoint.cleared + 1) : "BEGIN EXPEDITION";
        ImGui::SetCursorScreenPos({x, y});
        if (MenuButton(registry, begin.c_str(), save.exists ? short_ : tall, 20.f * u)) startNew(false);
        y += (save.exists ? short_.y : tall.y) + gap;
        if (checkpoint.cleared > 0) {
            ImGui::SetCursorScreenPos({x, y});
            if (MenuButton(registry, "FRESH EXPEDITION", short_, 17.f * u)) startNew(true);
            y += short_.y + gap;
        }
        ImGui::SetCursorScreenPos({x, y});
        if (MenuButton(registry, "QUIT", short_, 17.f * u)) request.value = EMenuRequest::Quit;
        y += short_.y + gap;
        if (!checkpoint.error.empty()) {
            centered(y, checkpoint.error, {.9f, .31f, .31f, 1});
            y += 22.f * u;
        }
        if (!save.error.empty()) centered(y, save.error, {.9f, .31f, .31f, 1});

        ui::FBodyFont hints(14.f * u);
        centered(h - 26.f * u, "Mouse: move / target    WASD: one-cell move    1-4: skill    Space: end turn    Esc: abandon",
                 ui::palette::kMutedV);
    }
    ui::EndZone();
    NewExpeditionConfirm(registry);
}

void DrawGrowthPanel(
    entt::registry& registry, const FDungeonGrowth& growth, FDungeonRun& run, std::size_t memberIndex) {
    if (memberIndex >= run.party.size()) return;
    FRunMember& member = run.party[memberIndex];
    ImGui::PushID(static_cast<int>(memberIndex));
    ImGui::Text("Level %d   EXP %d / %d   Node points %d", member.level, member.experience,
                growth.ExperienceForNextLevel(member.level), member.passivePoints);
    const FPassiveStatBonuses bonuses = growth.passiveTree.Bonuses(member.allocatedNodes);
    ImGui::TextDisabled("Tree bonuses: +%d HP, +%d ATK, +%d MATK, +%d DEF, +%d MDEF, +%d speed",
                        bonuses.maxHealth, bonuses.attack, bonuses.magicAttack, bonuses.defense,
                        bonuses.magicDefense, bonuses.speed);
    ui::DrawPassiveTreePanel(ui::FPassiveTreePanel{
        .nodes = &growth.passiveTree.nodes,
        .rootNodeId = growth.passiveTree.rootNodeId,
        .allocated = &member.allocatedNodes,
        .points = member.passivePoints,
        .canAllocate = [&](std::string_view nodeId) { return CanAllocateDungeonNode(registry, memberIndex, nodeId); },
        .allocate = [&](std::string_view nodeId) { (void)AllocateDungeonNode(registry, memberIndex, nodeId); },
    });
    ImGui::PopID();
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
            ui::HeadingText("LEAVE EXPEDITION?", 22.f);
            ImGui::Text("The expedition is saved at every room. Leaving mid-battle restarts this room");
            ImGui::Text("when you continue. Abandoning deletes the save: run growth and runes are lost,");
            ImGui::Text("cleared checkpoints remain.");
            if (ui::Button("Keep exploring", {180, 36})) {
                ui.confirmAbandon = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::Button("Save and leave", {180, 36})) {
                run.leaveRequested = true;
                ui.confirmAbandon = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::Button("Abandon", {140, 36})) {
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
            const auto* growth = registry.ctx().find<FDungeonGrowth>();
            ImGui::Text("Materials %d  |  collected runes %zu", run.materials, run.runes.size());
            if (growth == nullptr) {
                ImGui::TextColored({.9f, .31f, .31f, 1}, "Run growth content is missing.");
            } else if (!run.party.empty()) {
                ui.selectedGrowthMember = std::clamp(ui.selectedGrowthMember, 0, static_cast<int>(run.party.size()) - 1);
                if (ImGui::BeginCombo("Character", MemberName(run.party[ui.selectedGrowthMember]).c_str())) {
                    for (std::size_t index = 0; index < run.party.size(); ++index) {
                        const bool selected = static_cast<int>(index) == ui.selectedGrowthMember;
                        if (ImGui::Selectable(MemberName(run.party[index]).c_str(), selected)) {
                            ui.selectedGrowthMember = static_cast<int>(index);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                DrawGrowthPanel(registry, *growth, run, static_cast<std::size_t>(ui.selectedGrowthMember));
            }
            if (ui::Button("Continue", {-1, 42})) {
                ContinueDungeonReward(registry);
                ImGui::CloseCurrentPopup();
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
                LeaveDungeonCamp(registry);
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
