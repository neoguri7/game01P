#include "debug/TacticalD20VisualFeedbackPanel.h"

#include "core/InputState.h"
#include "core/factories/FTacticalD20BoardPlacement.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FCombatStateDefeat.h"
#include "ecs/components/FCombatStateVictory.h"
#include "ecs/components/FCommandToken.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FText.h"
#include "ecs/components/FTacticalBoardTile.h"
#include "ecs/components/FTacticalD20CommandDragState.h"
#include "ecs/components/FTacticalD20CommandFeedback.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FUnitStateDefeated.h"

#include <imgui.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

ImU32 Rgba(int r, int g, int b, int a) {
    return IM_COL32(r, g, b, a);
}

bool CombatEnded(entt::registry& registry) {
    return !registry.view<FCombatStateVictory>().empty() || !registry.view<FCombatStateDefeat>().empty();
}

bool CommandInputEnabled(entt::registry& registry) {
    const auto* input = registry.ctx().find<FInputState>();
    return !CombatEnded(registry) && !registry.view<FCombatStateAwaitingCommand>().empty() && (!input || !input->uiCapturesMouse);
}

ImVec2 TileMin(const FPosition& position) {
    return ImVec2(position.x, position.y);
}

ImVec2 TileMax(const FPosition& position) {
    return ImVec2(position.x + TacticalD20BoardTileSizePixels, position.y + TacticalD20BoardTileSizePixels);
}

ImVec2 CenterRectMin(const FPosition& position, const FCollider& collider) {
    return ImVec2(position.x - collider.halfWidth, position.y - collider.halfHeight);
}

ImVec2 CenterRectMax(const FPosition& position, const FCollider& collider) {
    return ImVec2(position.x + collider.halfWidth, position.y + collider.halfHeight);
}

void RenderTiles(entt::registry& registry, ImDrawList& drawList) {
    auto view = registry.view<FTacticalBoardTile, FPosition>();
    for (auto entity : view) {
        const auto& tile = view.get<FTacticalBoardTile>(entity);
        const auto& position = view.get<FPosition>(entity);
        const auto fill = tile.isWall ? Rgba(70, 70, 76, 130) : tile.isCover ? Rgba(55, 105, 70, 95) : Rgba(40, 48, 58, 45);
        drawList.AddRectFilled(TileMin(position), TileMax(position), fill);
        drawList.AddRect(TileMin(position), TileMax(position), Rgba(130, 138, 150, 105));
        if (tile.isCover) drawList.AddText(ImVec2(position.x + 6.f, position.y + 5.f), Rgba(190, 235, 180, 230), "cover");
    }
}

void RenderUnits(entt::registry& registry, ImDrawList& drawList) {
    auto view = registry.view<FTacticalUnit, FPosition, FText>();
    for (auto entity : view) {
        const auto& unit = view.get<FTacticalUnit>(entity);
        const auto& position = view.get<FPosition>(entity);
        const auto& text = view.get<FText>(entity);
        const ImVec2 center(position.x + 32.f, position.y + 32.f);
        const bool defeated = registry.all_of<FUnitStateDefeated>(entity);
        const auto fill = defeated ? Rgba(92, 92, 92, 190) : unit.team == "player" ? Rgba(80, 150, 230, 210) : Rgba(220, 90, 80, 210);
        drawList.AddCircleFilled(center, 18.f, fill);
        drawList.AddCircle(center, 18.f, Rgba(245, 245, 245, 230), 0, 2.f);
        drawList.AddText(ImVec2(position.x, position.y - 28.f), Rgba(text.colorR, text.colorG, text.colorB, text.colorA), text.content.c_str());
    }
}

void RenderCommandTokens(entt::registry& registry, ImDrawList& drawList) {
    const bool enabled = CommandInputEnabled(registry);
    drawList.AddRectFilled(ImVec2(610.f, 58.f), ImVec2(1120.f, 132.f), Rgba(20, 22, 28, 180));
    drawList.AddText(ImVec2(620.f, 66.f), Rgba(225, 225, 225, 230), enabled ? "Command Tray" : "Command Tray disabled");

    auto view = registry.view<FCommandToken, FPosition, FCollider>();
    for (auto entity : view) {
        const auto& token = view.get<FCommandToken>(entity);
        const auto& position = view.get<FPosition>(entity);
        const auto& collider = view.get<FCollider>(entity);
        const bool dragging = registry.all_of<FTacticalD20CommandDragState>(entity);
        const auto fill = !enabled ? Rgba(78, 78, 84, 150) : dragging ? Rgba(80, 145, 235, 215) : Rgba(56, 92, 145, 210);
        drawList.AddRectFilled(CenterRectMin(position, collider), CenterRectMax(position, collider), fill, 5.f);
        drawList.AddRect(CenterRectMin(position, collider), CenterRectMax(position, collider), Rgba(235, 240, 250, 210), 5.f);
        drawList.AddText(ImVec2(position.x - collider.halfWidth + 8.f, position.y - 8.f), Rgba(255, 255, 255, enabled ? 255 : 145), token.displayName.c_str());

        if (const auto* feedback = registry.try_get<FTacticalD20CommandFeedback>(entity)) {
            const auto color = feedback->accepted ? Rgba(100, 240, 135, 255) : Rgba(255, 95, 86, 255);
            drawList.AddRect(CenterRectMin(position, collider), CenterRectMax(position, collider), color, 5.f, 0, 3.f);
            drawList.AddText(ImVec2(position.x - collider.halfWidth, position.y + collider.halfHeight + 7.f), color, feedback->message.c_str());
        }
    }
}

} // namespace

void RenderTacticalD20VisualFeedbackPanel(entt::registry& registry) {
    ZoneScopedN("TacticalD20VisualFeedbackPanel");
    auto* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) return;
    RenderTiles(registry, *drawList);
    RenderUnits(registry, *drawList);
    RenderCommandTokens(registry, *drawList);
}

} // namespace game
