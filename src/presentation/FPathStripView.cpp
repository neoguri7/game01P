#include "presentation/FPathStripView.h"

#include <string>

#include <imgui.h>

#include "gameplay/combat/FEliteRules.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunPath.h"
#include "presentation/FUiTheme.h"

namespace game {

void DrawPathStrip(entt::registry& registry, float rightX, float topY, float scale) {
    const FPathStrip strip = BuildPathStrip(registry);
    if (strip.nodes.empty()) return;
    ImGui::SetNextWindowPos({rightX, topY}, ImGuiCond_Always, {1.f, 0.f});
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##path-strip", nullptr, flags)) {
        ui::FBodyFont body(12.f * scale);
        for (int index = 0; index < static_cast<int>(strip.nodes.size()); ++index) {
            const FPathNode& node = strip.nodes[static_cast<std::size_t>(index)];
            if (index > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled(">");
                ImGui::SameLine();
            }
            std::string text = node.label;
            if (index == strip.forkNode && strip.forkAlt) text += " / " + strip.forkAlt->label;
            if (node.current) text = "[" + text + "]";
            const ImVec4 color = node.current ? ui::palette::kGoldBrightV
                                              : (node.done ? ui::palette::kMutedV : ui::palette::kTextV);
            ImGui::TextColored(color, "%s", text.c_str());
        }
        // WP-44: the elite rule of this battle, wrapped so the right-anchored window cannot grow across the HUD.
        const std::string rule = EliteRuleHudLine(registry);
        if (!rule.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.f * scale);
            ImGui::TextColored(ui::palette::kGoldBrightV, "%s", rule.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::End();
}

int DrawForkPanel(entt::registry& registry, bool disabled) {
    if (!IsForkPending(registry)) return -1;
    const auto options = ForkOptions(registry);
    if (options.size() != 2u) return -1;
    const auto& run = registry.ctx().get<FDungeonRun>();
    int hp = 0;
    int hpMax = 0;
    for (const auto& member : run.party) {
        hp += DungeonEffectiveHealth(registry, member);
        hpMax += DungeonMemberMaxHealth(registry, member);
    }
    int pick = -1;
    ImGui::SeparatorText("CHOOSE A PATH");
    ImGui::Text("Party HP %d/%d | Materials %d", hp, hpMax, run.materials);
    ImGui::BeginDisabled(disabled);
    for (int index = 0; index < 2; ++index) {
        const auto& option = options[static_cast<std::size_t>(index)];
        if (ui::Button(option.title.c_str(), {-1, 38})) pick = index;
        ImGui::TextWrapped("%s", option.detail.c_str());
    }
    ImGui::EndDisabled();
    return pick;
}

} // namespace game
