#include "debug/TacticalCombatTelemetryPanel.h"

#include "core/SystemManager.h"
#include "core/Time.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHitPoints.h"
#include "ecs/components/FPlayerControlledTacticalUnit.h"
#include "ecs/components/FTacticalAttack.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "gameplay/tactical_d20/FTacticalCommandInputBridge.h"
#include "gameplay/tactical_d20/FTacticalCombatConfig.h"
#include "gameplay/tactical_d20/FTacticalCombatLog.h"
#include "gameplay/tactical_d20/FTacticalCombatQueries.h"
#include "gameplay/tactical_d20/FTacticalCombatTelemetry.h"
#include "gameplay/tactical_d20/events/FTacticalCombatEvents.h"
#include "gameplay/tactical_d20/logging/FTacticalCombatLogUtils.h"

#include <imgui.h>

#include <cstdlib>

namespace game {
namespace {

void EnqueueUiCommand(entt::registry& registry, const FTacticalCommandRequestedEvent& request) {
    if (!registry.ctx().contains<FTacticalCommandInputBridge>()) {
        registry.ctx().emplace<FTacticalCommandInputBridge>();
    }
    registry.ctx().get<FTacticalCommandInputBridge>().requests.push_back(request);
}

entt::entity NearestEnemy(entt::registry& registry, entt::entity unit) {
    auto enemies = TacticalLivingEnemies(registry, unit);
    if (enemies.empty()) return entt::null;
    entt::entity best = enemies.front();
    int bestDistance = TacticalManhattanDistanceFeet(registry, unit, best);
    for (auto enemy : enemies) {
        const int distance = TacticalManhattanDistanceFeet(registry, unit, enemy);
        if (distance < bestDistance) {
            best = enemy;
            bestDistance = distance;
        }
    }
    return best;
}

bool BestStepToward(entt::registry& registry, entt::entity unit, entt::entity target, int& outX, int& outY) {
    const auto& from = registry.get<FGridPosition>(unit);
    const auto& to = registry.get<FGridPosition>(target);
    const int options[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const auto* config = registry.ctx().find<FTacticalCombatConfig>();
    const int tileFeet = config ? config->tileFeet : 5;
    int bestDistance = TacticalManhattanDistanceFeet(registry, unit, target);
    bool found = false;
    for (const auto& option : options) {
        const int x = from.x + option[0];
        const int y = from.y + option[1];
        if (!TacticalTileInBounds(registry, x, y)) continue;
        if (TacticalTileBlocksMovement(registry, x, y)) continue;
        if (TacticalLivingUnitAt(registry, x, y) != entt::null) continue;
        const int distance = (abs(x - to.x) + abs(y - to.y)) * tileFeet;
        if (distance < bestDistance) {
            outX = x;
            outY = y;
            bestDistance = distance;
            found = true;
        }
    }
    return found;
}

void RenderCommandButtons(entt::registry& registry) {
    const auto controller = TacticalCombatController(registry);
    const auto active = TacticalActiveUnit(registry);
    if (controller == entt::null || !registry.valid(controller) || !registry.all_of<FCombatStateAwaitingCommand>(controller)) return;
    if (active == entt::null || !registry.valid(active) || TacticalIsCombatEnded(registry)) return;
    if (!registry.all_of<FPlayerControlledTacticalUnit>(active)) return;

    const auto target = NearestEnemy(registry, active);
    const bool hasTarget = target != entt::null;
    if (ImGui::Button("Attack") && hasTarget) {
        const auto& targetPosition = registry.get<FGridPosition>(target);
        EnqueueUiCommand(registry, FTacticalCommandRequestedEvent{active, ETacticalCommandAction::Attack, targetPosition.x, targetPosition.y, true, target, true});
    }
    ImGui::SameLine();
    if (ImGui::Button("Move") && hasTarget) {
        int x = registry.get<FGridPosition>(active).x;
        int y = registry.get<FGridPosition>(active).y;
        if (BestStepToward(registry, active, target, x, y)) {
            EnqueueUiCommand(registry, FTacticalCommandRequestedEvent{active, ETacticalCommandAction::Move, x, y, true, entt::null, false});
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Dash")) {
        EnqueueUiCommand(registry, FTacticalCommandRequestedEvent{active, ETacticalCommandAction::Dash, 0, 0, false, entt::null, false});
    }
    ImGui::SameLine();
    if (ImGui::Button("Dodge")) {
        EnqueueUiCommand(registry, FTacticalCommandRequestedEvent{active, ETacticalCommandAction::Dodge, 0, 0, false, entt::null, true});
    }
    ImGui::SameLine();
    if (ImGui::Button("End Turn")) {
        EnqueueUiCommand(registry, FTacticalCommandRequestedEvent{active, ETacticalCommandAction::EndTurn, 0, 0, false, entt::null, true});
    }
}

void RenderUnits(entt::registry& registry) {
    auto view = registry.view<FTacticalUnit, FHitPoints, FGridPosition>();
    for (auto unit : view) {
        const auto& info = view.get<FTacticalUnit>(unit);
        const auto& hp = view.get<FHitPoints>(unit);
        const auto& pos = view.get<FGridPosition>(unit);
        const bool active = registry.all_of<FActiveTacticalUnit>(unit);
        ImGui::Text("%s%s [%s] HP %d/%d at (%d,%d)",
            active ? "> " : "  ",
            info.displayName.c_str(),
            info.team.c_str(),
            hp.current,
            hp.max,
            pos.x,
            pos.y);
    }
}

} // namespace

void RenderTacticalCombatTelemetryPanel(entt::registry& registry, const Time& frameTime, const SystemManager& systemManager) {
    const auto* telemetry = registry.ctx().find<FTacticalCombatTelemetry>();
    ImGui::SetNextWindowPos(ImVec2(940, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tactical Combat");
    ImGui::Text("FPS: %d", frameTime.getFps());
    ImGui::Text("Systems: %zu", systemManager.getRegisteredCount());
    if (telemetry) {
        ImGui::Text("State: %s", telemetry->combatState.c_str());
        ImGui::Text("Round: %d", telemetry->round);
        ImGui::Text("Active: %s", telemetry->activeUnitName.c_str());
        if (!telemetry->lastRollBreakdown.empty()) ImGui::TextWrapped("Last roll: %s", telemetry->lastRollBreakdown.c_str());
        if (!telemetry->winner.empty()) ImGui::Text("Winner: %s", telemetry->winner.c_str());
    }
    ImGui::Separator();
    RenderCommandButtons(registry);
    ImGui::Separator();
    RenderUnits(registry);
    ImGui::Separator();
    if (const auto* log = registry.ctx().find<FTacticalCombatLog>()) {
        for (const auto& line : log->lines) {
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
    ImGui::End();
}

} // namespace game
