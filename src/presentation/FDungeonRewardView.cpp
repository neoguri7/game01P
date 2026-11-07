#include "presentation/FDungeonViews.h"

#include <algorithm>

#include <imgui.h>

#include "core/EngineState.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FPassiveTreePanel.h"
#include "presentation/FUiTheme.h"

namespace game {

const std::string& MemberName(const FRunMember& member) {
    return member.displayName.empty() ? member.id : member.displayName;
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

/// The room-secured screen. The body is the original RunChoice reward branch, moved verbatim; the only
/// change is the transition button: it posts the intent, the host applies it.
void RenderDungeonRewardView(entt::registry& registry) {
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
        DrawDungeonIcon(registry, EDungeonSprite::Chest);
        ImGui::SameLine();
        ui::HeadingText("ROOM SECURED", 26.f);
        ImGui::TextWrapped("%s", run.message.c_str());
        ImGui::Separator();
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
            PostRunIntent(registry, ERunIntent::ContinueReward);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace game
