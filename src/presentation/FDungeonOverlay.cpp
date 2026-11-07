#include "presentation/FDungeonOverlay.h"

#include <imgui.h>

#include "core/InputState.h"
#include "debug/DebugOverlay.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FTitleMenuView.h"
#include "presentation/FUiTheme.h"

namespace game {

void InitializeDungeonUi(entt::registry& registry) {
    registry.ctx().emplace<FDungeonUiState>();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigNavCaptureKeyboard = false;
    ui::ApplyTheme();
    // The composition root registers the views and the title rows once. States and views only look them up.
    RegisterModalViews(registry);
    RegisterTitleMenuItems(registry);
}

/// The overlay is the debug layer only (WP-02b). Every title, hub and run modal is drawn by the state that
/// owns it, so the overlay never draws a menu or a run choice.
void RenderDungeonOverlay(entt::registry& registry, const Time& time, const SystemManager& systems) {
    auto& ui = registry.ctx().get<FDungeonUiState>();
    if (const auto* input = registry.ctx().find<FInputState>();
        input && input->isActionPressed(EInputAction::DebugToggle)) ui.debugVisible = !ui.debugVisible;
    if (ui.debugVisible) RenderDebugOverlay(registry, time, systems);
}

} // namespace game
