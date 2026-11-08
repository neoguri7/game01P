#pragma once

#include <array>
#include <span>
#include <string_view>

#include <imgui.h>

#include "presentation/FUiLayout.h"

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

// ---------------------------------------------------------------------------------------------
// WP-24b non-colour information cues
// ---------------------------------------------------------------------------------------------

/// One row of the WP-24b "no information by colour alone" audit. `cue` names the redundant channel
/// (shape, glyph, number, text) or, for FollowUp, the gap and where it is recorded.
enum class EInfoCueStatus { Covered, FollowUp };

struct FInfoCueRow {
    std::string_view id;
    std::string_view what;
    std::string_view cue;
    EInfoCueStatus status;
};

/// The 6-row checklist the acceptance criterion refers to (the data lives in FUiTheme.cpp). A renderer or
/// test reads this instead of restating the audit; the residual gaps stay visible as FollowUp rows.
[[nodiscard]] std::span<const FInfoCueRow> InfoCueChecklist();

/// The faction marker shapes. The world draws the filled shape above the unit's head (party = square, enemy
/// = triangle), so faction never depends on the ring colour.
enum class EFactionMarker { Square, Triangle };

struct FMarkerPoint {
    float x{0.f};
    float y{0.f};
};

/// Up to four screen-axis outline points. `count` is 4 for Square and 3 for Triangle; the last point is NOT
/// repeated (the renderer closes the loop) and the points run clockwise on screen (y down).
struct FMarkerOutline {
    std::array<FMarkerPoint, 4> points{};
    int count{0};
};

/// Party = square, enemy = upward triangle.
[[nodiscard]] EFactionMarker FactionMarkerFor(bool party) noexcept;

/// Filled-badge outline inside (rx, ry) around (cx, cy): axis-aligned, not iso-projected. Pure geometry: no
/// ImGui types, so the SDL world renderer can consume it.
[[nodiscard]] FMarkerOutline FactionMarkerOutline(EFactionMarker marker, float cx, float cy, float rx,
                                                  float ry) noexcept;

/// Applies the user's text scale through ImGui's one global font factor (style.FontScaleMain). This is the
/// only place the game writes FontScaleMain: explicit PushFont(font, size) sites scale automatically.
void ApplyTextScale(float scale);

// ---------------------------------------------------------------------------------------------
// WP-12c HUD legibility: the Free/Zone text model, the board-plate cues and the non-colour channels
// ---------------------------------------------------------------------------------------------

/// WP-12c text scopes. Free = board-attached text (intent / preview numbers, damage pops, board hover
/// tooltip): it follows the user's text scale. Zone = the fixed HUD clusters: the authored size is kept,
/// and ImGui-path Zone text (unit name, END TURN) keeps ImGui's own scaling without a wrapper.
enum class EHudText { Free, Zone };

/// The one text-size rule: `base * u` in a fixed zone, `base * u * FontScaleMain` on the free board.
/// `u` is the layout scale, which FUiTheme does not know — the caller passes it.
[[nodiscard]] float HudTextPx(float base, float u, EHudText scope);

namespace hud {

// WP-12c size table (design px at u = 1): the single source of truth for every HUD text site this WP
// touched. The view reads these constants; HudTextSizeTable() carries the scope/class audit so a test
// can report the sizes without restating them. See docs/expansion/plans/WP-12c-hud-legibility.md §2.
inline constexpr float kIntentNumberPx = 18.f;  // intent damage number (Class A, Free)
inline constexpr float kIntentPlusPx = 12.f;    // same-cell "+N" (Free, hierarchy tier)
inline constexpr float kPreviewNumberPx = 18.f; // selected-skill preview number (Class A, Free)
inline constexpr float kDamagePopPx = 18.f;     // floating damage pop (Class A, Free)
inline constexpr float kHoverTextPx = 15.f;     // board hover tooltip (Free)
inline constexpr float kRibbonRoundPx = 15.f;   // turn ribbon "ROUND N" (Zone)
inline constexpr float kRibbonMarkerPx = 15.f;  // turn ribbon next-round marker (Zone)
inline constexpr float kUnitNamePx = 15.f;      // unit readout name (Cinzel, ImGui path)
inline constexpr float kUnitRolePx = 15.f;      // unit readout role label (Zone)
inline constexpr float kHpNumberPx = 18.f;      // HP "cur / max" (Class A, Zone)
inline constexpr float kMoveSkillPx = 15.f;     // "M" / "S" action-point labels (Zone)
inline constexpr float kPlanningPx = 15.f;      // "Planning..." enemy-turn label (Zone)
inline constexpr float kStatusSummaryPx = 15.f; // status summary / dot plate / SKIP (Zone)
inline constexpr float kThreatNumberPx = 18.f;  // card threat total (Class A, Zone)
inline constexpr float kSkillKeyPx = 18.f;      // skill slot hotkey (Class A, Zone)
inline constexpr float kSkillCostPx = 18.f;     // skill slot AP cost (Class A, Zone)
inline constexpr float kJournalPx = 15.f;       // journal lines (Zone)
inline constexpr float kTopBarTitlePx = 15.f;   // "EMBER DUNGEON" title (Cinzel, ImGui path)

} // namespace hud

/// One row of the WP-12c HUD text-size audit. `classA` is the XAG-critical gameplay number (must stay at
/// or above 18 design px); `residualGap` names a row in the WP's explicit 84% report (held at 15 on purpose).
struct FHudTextSite {
    std::string_view id;
    float basePx;
    EHudText scope;
    bool classA;
    bool residualGap;
};

/// The size table the tests report against (data lives in FUiTheme.cpp), built from the hud:: constants.
[[nodiscard]] std::span<const FHudTextSite> HudTextSizeTable();

/// Status gem shape by status kind (`kStatusKind*` from gameplay/data/FStatusContent.h): a second channel
/// next to the kind colour. Unknown kinds get the hollow ring.
enum class EStatusGemShape { Circle, Triangle, Square, Diamond, Ring };

[[nodiscard]] EStatusGemShape StatusGemShapeFor(std::string_view kind);

/// One status gem at `center`: the kind's shape in `fill` with the existing dark 1px outline.
void DrawStatusGem(ImDrawList* draw, ImVec2 center, float radius, EStatusGemShape shape, ImU32 fill);

/// 1px bright outline over a board plate's dark backing (WP-12c), so the plate reads on any terrain.
void DrawPlateOutline(ImDrawList* draw, ImVec2 min, ImVec2 max, float radius, ImU32 color);

/// Board-plate caret: a filled trapezoid from the plate's bottom centre (4u wide) to `anchor` (2u wide),
/// so a stacked plate still names the cell it belongs to. Colour = the plate's meaning colour.
void DrawPlateCaret(ImDrawList* draw, ImVec2 plateBottomCenter, ImVec2 anchor, float u, ImU32 color);

/// Same-cell stack lift: the 3rd and later plates stay at the 2nd step and let the caret length carry the
/// affiliation instead of growing an unbounded column.
[[nodiscard]] int PlateLiftSteps(int stackIndex) noexcept;

/// Which selection cues on the board are drawn as an outline, and at which diamond insets. Affected (the
/// hit forecast) is the double-outline cue; hovered is the pointer-cursor affordance (see the renderer).
enum class ESelectionCue { Reachable, Affected, Hovered };

[[nodiscard]] std::span<const float> SelectionOutlineInsets(ESelectionCue cue);

/// Layout width of `text` in the heading font at the EFFECTIVE size (`size * FontScaleMain`) — what the
/// ImGui text path really lays out, which is the width the unit readout's role label must clear. Zero
/// without the bundled heading font.
[[nodiscard]] float HeadingTextWidth(const char* text, float size);

/// The rect of one skill slot inside `zone` (`layout.zones.skills`): 56u slot, 18u pad, three gaps. The
/// view's hit rect and the click test read the same geometry.
[[nodiscard]] FRect SkillSlotRect(const FRect& zone, float u, int index);

/// One piece of the carved-metal UI skin. An empty piece (size 0) means "draw the procedural fallback".
struct FSkinPiece {
    ImTextureRef texture{};
    ImVec2 size{0.f, 0.f};
    [[nodiscard]] bool valid() const noexcept { return size.x > 0.f && size.y > 0.f; }
};

/// Parchment panel with ornate corners, bronze plaque frame (hollow center) and square item slot.
struct FSkin {
    FSkinPiece panel;
    FSkinPiece plaque;
    FSkinPiece slot;
};

/// Installs the skin textures; panels, slots and buttons then draw them instead of the procedural trim.
void InstallSkin(const FSkin& skin);

/// Stretches `piece` over [min, max]: corners keep their pixel size, edges and center stretch.
void DrawNineSlice(ImDrawList* draw, const FSkinPiece& piece, ImVec2 min, ImVec2 max, float corner,
                   ImU32 tint = IM_COL32_WHITE);

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

/// Borderless, transparent window pinned to a screen rectangle with no padding. Pair with EndZone().
bool BeginZone(const char* name, ImVec2 pos, ImVec2 size);
void EndZone();

/// Dark inset field so text and readouts stay legible over any frame art.
void DrawRecess(ImDrawList* draw, ImVec2 min, ImVec2 max);

/// Scopes the body font to `size` pixels; does nothing when the bundled fonts could not be loaded.
class FBodyFont {
public:
    explicit FBodyFont(float size);
    ~FBodyFont();
    FBodyFont(const FBodyFont&) = delete;
    FBodyFont& operator=(const FBodyFont&) = delete;

private:
    bool pushed_;
};

/// Draws trim over the current (popup) window; call right after Begin/BeginPopupModal.
void DecorateCurrentWindow();

/// Gold heading with ornamental rules on both sides, e.g. "YOUR PARTY".
void SectionTitle(const char* text, float size = 16.f);

/// Text in the heading font at `size` pixels.
void HeadingText(const char* text, float size, ImVec4 color = palette::kGoldV);

/// Standard ImGui button (so labels still reach ImGui's text log) with bronze/gold trim.
bool Button(const char* label, ImVec2 size = {0, 0});

} // namespace game::ui
