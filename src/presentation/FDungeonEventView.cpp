#include "presentation/FDungeonViews.h"

#include <string>

#include <imgui.h>

#include "core/EngineState.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FUiTheme.h"

namespace game {

/// The shrine event. The body is the original RunChoice event branch; the two buttons post the choice.
void RenderDungeonEventView(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    const auto& window = registry.ctx().get<FEngineWindowState>();
    ImGui::SetNextWindowPos({window.width * .5f, window.height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({580, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({580, 0}, {580, static_cast<float>(window.height - 80)});
    ImGui::OpenPopup("Expedition choice");
    if (ImGui::BeginPopupModal("Expedition choice", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        DrawDungeonIcon(registry, EDungeonSprite::Shrine);
        ImGui::SameLine();
        ui::HeadingText("A QUIET SHRINE", 26.f);
        ImGui::TextWrapped("%s", run.message.c_str());
        ImGui::Separator();
        const auto restLabel = "Rest at the shrine  /  heal " +
            std::to_string(DungeonBalance(registry, "event_heal")) + " HP";
        const auto cacheLabel = "Open the cache  /  gain " +
            std::to_string(DungeonBalance(registry, "event_materials")) + " materials";
        if (ui::Button(restLabel.c_str(), {-1, 42})) {
            PostRunIntent(registry, ERunIntent::ChooseEventRest);
            ImGui::CloseCurrentPopup();
        }
        if (ui::Button(cacheLabel.c_str(), {-1, 42})) {
            PostRunIntent(registry, ERunIntent::ChooseEventLoot);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace game
