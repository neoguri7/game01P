#pragma once

#include <entt/entt.hpp>

#include "gameplay/dungeon/FRunBriefing.h"
#include "presentation/FUiLayout.h"

namespace game {

enum class EBriefingCommand { None, Enter };
struct FBriefingCommand { EBriefingCommand value{EBriefingCommand::None}; };

/// Screen rectangles of the briefing (screen px). u = min(h/800, w/1160) as on the preparation screen;
/// k = max(1, floor(u)) scales pictures by whole numbers; t = u * fontScale scales text bands.
struct FBriefingLayout {
    float u{1.f};
    int k{1};
    float t{1.f};
    ui::FRect panel;
    ui::FRect banner;
    ui::FRect headerText;
    ui::FRect expedition;
    ui::FRect objective;
    ui::FRect route;
    ui::FRect threats;
    ui::FRect party;
    ui::FRect reward;
    ui::FRect button;
};

[[nodiscard]] FBriefingLayout ComputeBriefingLayout(float windowWidth, float windowHeight, float fontScale);

/// Draws the briefing; the button posts FBriefingCommand{Enter}. Needs FEngineWindowState (no-op without).
void DrawBriefingScreen(entt::registry& registry, const FRunBriefing& briefing);

} // namespace game
