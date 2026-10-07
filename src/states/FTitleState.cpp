#include "FTitleState.h"
#include "core/InputState.h"
#include <array>
#include <imgui.h>

std::span<const std::string_view> FTitleState::RequiredSystems() const noexcept
{
    static constexpr std::array<std::string_view, 2> systems{"AnimationSystem", "SpriteRenderSystem"};
    return systems;
}

EStateTransition FTitleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (requested != EStateTransition::None) return requested;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (input && input->isActionPressed(game::EInputAction::Confirm)) {
        return EStateTransition::ToHub;
    }
    return EStateTransition::None;
}

void FTitleState::Render(entt::registry& Registry)
{
    (void)Registry;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440, 240), ImGuiCond_Always);
    ImGui::Begin("Game01P", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::TextWrapped("Turn-based grid prototype. Select actions and targets with the mouse. Attack visuals do not determine damage.");
    if (ImGui::Button("Prepare party", ImVec2(-1, 42))) requested = EStateTransition::ToHub;
    if (ImGui::Button("Quit", ImVec2(-1, 32))) requested = EStateTransition::Quit;
    ImGui::TextDisabled("Enter: prepare | grave: diagnostics");
    ImGui::End();
}
