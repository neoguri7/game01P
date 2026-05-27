#include "debug/TacticalD20TelemetryPanel.h"

#include <cstdint>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>
#include <tracy/Tracy.hpp>

#include "core/SystemManager.h"
#include "core/Time.h"
#include "gameplay/tactical_d20/FTacticalD20CombatLog.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20EventLog.h"
#include "gameplay/tactical_d20/FTacticalD20StateLog.h"
#include "gameplay/tactical_d20/FTacticalD20Telemetry.h"

namespace game {
namespace {

const char* EntityText(entt::entity entity) {
    static std::string text;
    text = entity == entt::null ? "None" : std::to_string(static_cast<uint32_t>(entity));
    return text.c_str();
}

void RenderLogLines(const char* title, const std::vector<std::string>& lines) {
    if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) return;
    for (const auto& line : lines) ImGui::TextUnformatted(line.c_str());
}

void RenderSystemOrder(const SystemManager& systemManager) {
    if (!ImGui::CollapsingHeader("System Order")) return;
    const auto names = systemManager.getSystemNames();
    for (std::size_t i = 0; i < names.size(); ++i) ImGui::Text("%zu. %s", i + 1, names[i].c_str());
}

void RenderTelemetryRows(entt::registry& registry, const Time& frameTime) {
    const auto* telemetry = registry.ctx().find<FTacticalD20Telemetry>();
    ImGui::Text("Frame: %.2f ms  FPS: %d", frameTime.getDeltaTime() * 1000.f, frameTime.getFps());
    ImGui::Text("Raw dt: %.2f ms  Clamp: %s", frameTime.getRawDeltaTime() * 1000.f, frameTime.wasDeltaClamped() ? "yes" : "no");
    ImGui::Text("Entities: %d", telemetry ? telemetry->entityCount : static_cast<int>(registry.storage<entt::entity>().size()));
    if (!telemetry) return;

    ImGui::Text("Combat: %s", telemetry->combatState.c_str());
    ImGui::Text("Active unit: %s (%s)", telemetry->activeUnitId.c_str(), EntityText(telemetry->activeUnit));
    ImGui::Text("Round: %d", telemetry->round);
    ImGui::Text("Last command: %s", telemetry->lastCommandDropResult.c_str());
    ImGui::TextWrapped("Last d20: %s", telemetry->lastD20RollBreakdown.c_str());
    ImGui::Text("Hovered tile: %s", telemetry->hasHoveredTile ? fmt::format("{}, {}", telemetry->hoveredTileX, telemetry->hoveredTileY).c_str() : "None");
    ImGui::Text("Selected tile: %s", telemetry->hasSelectedTile ? fmt::format("{}, {}", telemetry->selectedTileX, telemetry->selectedTileY).c_str() : "None");
    ImGui::Text("Hovered entity: %s", EntityText(telemetry->hoveredEntity));
    ImGui::Text("Selected entity: %s", EntityText(telemetry->selectedEntity));
    if (ImGui::CollapsingHeader("Events This Frame", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& count : telemetry->eventCountsThisFrame) ImGui::Text("%s: %d", count.name.c_str(), count.count);
    }
}

void RenderLogs(entt::registry& registry, const FTacticalD20Config* config) {
    if (config && !config->logging.imguiLogEnabled) return;
    if (const auto* log = registry.ctx().find<FTacticalD20CombatLog>(); log && (!config || config->showCombatLog)) RenderLogLines("Combat Log", log->lines);
    if (const auto* log = registry.ctx().find<FTacticalD20EventLog>(); log && (!config || config->showEventLog)) RenderLogLines("Event Log", log->lines);
    if (const auto* log = registry.ctx().find<FTacticalD20StateLog>(); log && (!config || config->showStateLog)) RenderLogLines("State Log", log->lines);
}

} // namespace

void RenderTacticalD20TelemetryPanel(entt::registry& registry, const Time& frameTime, const SystemManager& systemManager) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    if (config && !config->showEngineTelemetry) return;

    ZoneScopedN("TacticalD20TelemetryPanel");
    ImGui::SetNextWindowPos(ImVec2(980, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tactical D20 Telemetry");
    RenderTelemetryRows(registry, frameTime);
    RenderSystemOrder(systemManager);
    RenderLogs(registry, config);
    ImGui::End();
}

} // namespace game
