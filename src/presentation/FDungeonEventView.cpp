#include "presentation/FDungeonViews.h"

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/EngineState.h"
#include "core/InputState.h"
#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FEventEffects.h"
#include "gameplay/dungeon/FEventSection.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

/// The per-member loss preview in the pre-WP-09 wording ("Alden / Warrior 0, Rowan / Archer 3"). The
/// numbers come from FEventEffects (displayed loss == applied loss); the names are the run members'
/// presentation names (MemberName) at the same index — the string the old view and the tests pin.
std::string LossLine(const FDungeonRun& run, const std::vector<FEventMemberLoss>& loss) {
    std::string line;
    for (std::size_t index = 0; index < loss.size(); ++index) {
        if (!line.empty()) line += ", ";
        const std::string& name = index < run.party.size() ? MemberName(run.party[index]) : loss[index].memberName;
        line += name + " " + std::to_string(loss[index].loss);
    }
    return line;
}

} // namespace

/// The event room (WP-09 §5), a 2-stage screen: stage 1 lists the choices (a closed gate's reason and a
/// damaging choice's per-member HP loss under the button — both computed by FEventEffects, never restated
/// here), stage 2 shows the resolved text and Continue. No undo/leave: a choice is final.
void RenderDungeonEventView(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const auto instance = CurrentEventInstance(registry);
    const auto def = instance ? FindEventDef(registry, instance->eventId) : std::nullopt;
    ImGui::SetNextWindowPos({window.width * .5f, window.height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({580, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({580, 0}, {580, static_cast<float>(window.height - 80)});
    ImGui::OpenPopup("Expedition choice");
    if (ImGui::BeginPopupModal("Expedition choice", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        if (def && instance) {
            std::string title = def->title;
            for (char& c : title) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            DrawDungeonIcon(registry, EDungeonSprite::Shrine);
            ImGui::SameLine();
            ui::HeadingText(title.c_str(), 26.f);
            if (!instance->resolved) {
                ImGui::TextWrapped("%s", def->body.c_str());
                ImGui::Separator();
                for (std::size_t index = 0; index < def->choices.size(); ++index) {
                    const FEventChoice& choice = def->choices[index];
                    const FEventGate gate = EvaluateEventGate(registry, choice);
                    ImGui::BeginDisabled(!gate.open);
                    // The choice resolves inside the same popup (the stage switches); never CloseCurrentPopup here.
                    if (ui::Button(choice.text.c_str(), {-1, 42})) {
                        PostRunIntent(registry, ERunIntent::ChooseEventOption, static_cast<int>(index));
                    }
                    ImGui::EndDisabled();
                    if (!gate.reason.empty()) ImGui::TextDisabled("%s", gate.reason.c_str());
                    const auto loss = PreviewEventChoiceLoss(registry, choice);
                    if (!loss.empty()) {
                        const std::string line = LossLine(run, loss);
                        ImGui::TextWrapped("HP cost: %s", line.c_str());
                    }
                }
            } else {
                // The flow wrote the result text (saved, so it survives Continue); the choice's own
                // resultText is the fallback for a restored result screen whose message came back empty.
                std::string result = run.message;
                if (result.empty() && instance->choice >= 0 &&
                    static_cast<std::size_t>(instance->choice) < def->choices.size()) {
                    result = def->choices[static_cast<std::size_t>(instance->choice)].resultText;
                }
                ImGui::TextWrapped("%s", result.c_str());
                bool confirmed = false;
                if (const auto* input = registry.ctx().find<FInputState>()) {
                    confirmed = input->isActionPressed(EInputAction::Confirm);
                }
                if (ui::Button("Continue", {-1, 42}) || confirmed) {
                    PostRunIntent(registry, ERunIntent::ContinueEvent);
                    ImGui::CloseCurrentPopup();
                }
            }
        } else {
            // No resolvable event (a save whose content vanished): show what the flow wrote, offer nothing.
            ImGui::TextWrapped("%s", run.message.c_str());
        }
        ImGui::EndPopup();
    }
}

} // namespace game
