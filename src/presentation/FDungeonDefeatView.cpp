#include "presentation/FDungeonViews.h"

#include <imgui.h>

#include "core/EngineState.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FUiTheme.h"

namespace game {

/// The run-end screen. The original RunChoice "EXPEDITION LOST" branch; the buttons post RetryAfterDefeat
/// or LeaveRun. The run-end host owns the transition, so the view never starts a run or changes a phase.
void RenderDungeonDefeatView(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    const auto& window = registry.ctx().get<FEngineWindowState>();
    ImGui::SetNextWindowPos({window.width * .5f, window.height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({580, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({580, 0}, {580, static_cast<float>(window.height - 80)});
    ImGui::OpenPopup("Expedition choice");
    if (ImGui::BeginPopupModal("Expedition choice", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        DrawDungeonIcon(registry, EDungeonSprite::Guardian);
        ImGui::SameLine();
        ui::HeadingText("EXPEDITION LOST", 26.f);
        ImGui::TextWrapped("%s", run.message.c_str());
        ImGui::Separator();
        if (ui::Button("New expedition from checkpoint", {-1, 44})) {
            PostRunIntent(registry, ERunIntent::RetryAfterDefeat);
            ImGui::CloseCurrentPopup();
        }
        if (ui::Button("Return to camp", {-1, 32})) {
            PostRunIntent(registry, ERunIntent::LeaveRun);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace game
