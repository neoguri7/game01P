#pragma once

#include <optional>

#include <entt/entt.hpp>

#include "gameplay/grid/FGridTypes.h"
#include "presentation/FIsoView.h"
#include "presentation/FUiLayout.h"
#include "presentation/FUiTheme.h"

namespace game::ui {

/// The full-screen combat HUD frame: a decorated border that fills the whole window around a transparent
/// viewport, with the HUD zones laid out inside it (see assets/data/ui_layout.json). Lives in the EnTT
/// context for the Ember mode.
struct FUiFrame {
    FUiLayoutSpec spec;
    /// Optional frame image (spec.texture); invalid means the bands are drawn with the stone panel skin.
    FSkinPiece art;
    /// Layout for `layoutSize`, recomputed when the window changes size.
    FUiLayout layout;
    float layoutWidth{-1.f};
    float layoutHeight{-1.f};
};

/// Loads ui_layout.json and, when present, the frame image. False when the layout document is unusable
/// (the reason is logged); a missing frame image is not an error.
bool InitializeUiFrame(entt::registry& registry);

/// The frame resolved for the current window; null before InitializeUiFrame or without window state.
[[nodiscard]] const FUiLayout* FindUiLayout(entt::registry& registry);

/// Isometric view of the arena fitted into the frame's viewport, the one view that drawing and mouse
/// picking share. Null when no frame layout exists.
[[nodiscard]] std::optional<FIsoView> FindBattleIsoView(entt::registry& registry, const FGridMetrics& metrics);

/// Draws the frame behind every ImGui window: the frame image stretched to the window, or the stone bands.
void DrawUiFrame(entt::registry& registry);

} // namespace game::ui
