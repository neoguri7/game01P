#pragma once

#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FDungeonRun.h"
#include "states/FBaseState.h"

namespace game {

/// One title-menu row. The base three are the new expedition / continue / quit; a later WP appends
/// settings and compendium through the anchors in FTitleMenuView.cpp. A request item sets FMenuRequest;
/// a transition item leaves its transition in the pending slot for the title/hub Update to consume.
struct FTitleMenuItem {
    std::string id;
    std::string label;
    FMenuRequest request;
    FStateTransition transition;
    bool visible{true};
};

struct FTitleMenuRegistry {
    std::vector<FTitleMenuItem> items;
};

/// A transition-only row's intent. The title/hub Update takes it once; the menu never starts a run.
struct FTitleMenuPendingTransition {
    FStateTransition transition;
};

/// Builds the title rows. Called once from InitializeDungeonUi.
void RegisterTitleMenuItems(entt::registry& registry);

/// Draws the title screen (painting, logo, rows, the "start a new expedition?" confirm). The title state
/// and the hub state both call this; the drawing is the only owner of the menu.
void DrawTitleMenu(entt::registry& registry);

/// Takes a pending transition-only row, if one was clicked this frame.
[[nodiscard]] std::optional<FStateTransition> TakeTitleMenuTransition(entt::registry& registry);

} // namespace game
