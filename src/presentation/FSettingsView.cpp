#include "presentation/FSettingsView.h"

#include <imgui.h>

#include "core/EngineState.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FSettings.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

/// Disabled-colour text that wraps inside the modal instead of clipping at its right edge.
void MutedWrappedText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

/// Two rows, both saved the moment they change: the speed button toggles 1x/2x, the checkbox flips the
/// skip. Everything else on the screen is read-only text.
void RenderSettingsView(entt::registry& registry) {
    const auto* window = registry.ctx().find<FEngineWindowState>();
    if (window == nullptr) return;
    const FSettings& settings = CurrentSettings(registry);
    ImGui::SetNextWindowPos({window->width * .5f, window->height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({460, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({460, 0}, {460, static_cast<float>(window->height - 80)});
    ImGui::OpenPopup("Settings");
    if (ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        ui::HeadingText("SETTINGS", 26.f);
        ImGui::Separator();

        ImGui::TextUnformatted("Battle speed");
        ImGui::SameLine();
        const bool fast = settings.animSpeed == kAnimSpeedFast;
        if (ui::Button(fast ? "2x  (fast)" : "1x  (normal)", {150, 32})) ToggleAnimSpeed(registry);
        MutedWrappedText("(presentation only - turns and damage never change)");

        bool skip = settings.skipAnimations;
        if (ImGui::Checkbox("Skip battle animations", &skip)) SetSkipAnimations(registry, skip);
        MutedWrappedText("Drains each action's show in one frame; the fight resolves identically.");

        ImGui::Separator();
        if (ui::Button("Close", {-1, 40})) {
            RequestModalViewClose(registry);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace game
