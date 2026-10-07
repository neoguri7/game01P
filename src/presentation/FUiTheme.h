#pragma once

#include <imgui.h>

namespace game::ui {

// Dark-fantasy palette shared by every in-game window. Warm near-black stone, bronze and gold trim,
// blood-red life. Faction cues (party blue / enemy red) stay with the world-space markers.
namespace palette {
inline constexpr ImU32 kPanelTop = IM_COL32(30, 22, 22, 250);
inline constexpr ImU32 kPanelBottom = IM_COL32(12, 9, 10, 252);
inline constexpr ImU32 kOutline = IM_COL32(4, 2, 3, 255);
inline constexpr ImU32 kBronze = IM_COL32(112, 82, 40, 255);
inline constexpr ImU32 kGold = IM_COL32(201, 160, 82, 255);
inline constexpr ImU32 kGoldBright = IM_COL32(240, 206, 128, 255);
inline constexpr ImU32 kText = IM_COL32(222, 208, 176, 255);
inline constexpr ImU32 kMuted = IM_COL32(140, 128, 108, 255);
inline constexpr ImU32 kBloodTop = IM_COL32(204, 44, 40, 255);
inline constexpr ImU32 kBloodBottom = IM_COL32(98, 14, 17, 255);
inline constexpr ImU32 kParty = IM_COL32(80, 140, 255, 255);
inline constexpr ImU32 kEnemy = IM_COL32(230, 80, 80, 255);
inline constexpr ImU32 kMove = IM_COL32(222, 160, 60, 255);
inline constexpr ImU32 kSkill = IM_COL32(70, 130, 230, 255);

inline constexpr ImVec4 kTextV{0.871f, 0.816f, 0.69f, 1.f};
inline constexpr ImVec4 kMutedV{0.55f, 0.50f, 0.42f, 1.f};
inline constexpr ImVec4 kGoldV{0.89f, 0.74f, 0.40f, 1.f};
inline constexpr ImVec4 kGoldBrightV{0.94f, 0.81f, 0.50f, 1.f};
inline constexpr ImVec4 kPartyV{0.31f, 0.55f, 1.f, 1.f};
inline constexpr ImVec4 kEnemyV{0.90f, 0.31f, 0.31f, 1.f};
} // namespace palette

/// Loads the bundled fonts (when present) and installs the dark-fantasy ImGui style. Call once per
/// ImGui context; without font files the built-in font is used and everything keeps working.
void ApplyTheme();

/// Heading font (Cinzel) at an arbitrary pixel size; null when the font file was not found.
[[nodiscard]] ImFont* HeadingFont();

/// Ornate stone panel with bronze/gold trim. `fill` false draws only the trim (for popups).
void DrawPanel(ImDrawList* draw, ImVec2 min, ImVec2 max, bool fill = true);

/// Crimson life bar with a dark inset frame. `fraction` is clamped to [0, 1].
void DrawLifeBar(ImDrawList* draw, ImVec2 min, ImVec2 max, float fraction);

/// Small faceted gem used for action points.
void DrawGem(ImDrawList* draw, ImVec2 center, float radius, ImU32 color, bool filled);

/// Framed square used for portraits and skill slots.
void DrawSlotFrame(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 trim, bool glow);

/// Scales a color's alpha by the current ImGui style alpha so disabled widgets dim our custom trim too.
[[nodiscard]] ImU32 Fade(ImU32 color);

/// Borderless fixed window with the stone panel behind it. Pair with EndPanel().
bool BeginPanel(const char* name, ImVec2 pos, ImVec2 size, ImGuiWindowFlags extraFlags = 0);
void EndPanel();

/// Draws trim over the current (popup) window; call right after Begin/BeginPopupModal.
void DecorateCurrentWindow();

/// Gold heading with ornamental rules on both sides, e.g. "YOUR PARTY".
void SectionTitle(const char* text);

/// Text in the heading font at `size` pixels.
void HeadingText(const char* text, float size, ImVec4 color = palette::kGoldV);

/// Standard ImGui button (so labels still reach ImGui's text log) with bronze/gold trim.
bool Button(const char* label, ImVec2 size = {0, 0});

} // namespace game::ui
