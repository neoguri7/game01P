#pragma once

#include <optional>

#include <entt/entt.hpp>

#include "gameplay/grid/FGridTypes.h"
#include "presentation/FIsoView.h"
#include "presentation/FUiLayout.h"

namespace game::ui {

/// The floating combat HUD's layout for the current window (see assets/data/ui_layout.json). Lives in the
/// EnTT context for the Ember mode.
struct FUiFrame {
    FUiLayoutSpec spec;
    /// Layout for `layoutSize`, recomputed when the window changes size.
    FUiLayout layout;
    float layoutWidth{-1.f};
    float layoutHeight{-1.f};
};

/// Loads ui_layout.json. False when the layout document is unusable (the reason is logged).
bool InitializeUiFrame(entt::registry& registry);

/// The layout resolved for the current window; null before InitializeUiFrame or without window state.
[[nodiscard]] const FUiLayout* FindUiLayout(entt::registry& registry);

/// Isometric view of the arena fitted clear of the HUD clusters, the one view that drawing and mouse
/// picking share. Null when no layout exists.
[[nodiscard]] std::optional<FIsoView> FindBattleIsoView(entt::registry& registry, const FGridMetrics& metrics);

} // namespace game::ui
