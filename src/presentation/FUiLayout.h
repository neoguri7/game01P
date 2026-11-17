#pragma once

#include <expected>
#include <string>

#include "core/FAssetDigest.h"

namespace game::ui {

/// Axis-aligned rectangle in screen (or design) pixels.
struct FRect {
    float x{0.f};
    float y{0.f};
    float w{0.f};
    float h{0.f};

    [[nodiscard]] float right() const noexcept { return x + w; }
    [[nodiscard]] float bottom() const noexcept { return y + h; }
    [[nodiscard]] bool intersects(const FRect& other) const noexcept {
        return x < other.right() && other.x < right() && y < other.bottom() && other.y < bottom();
    }
    [[nodiscard]] bool contains(const FRect& other) const noexcept {
        return other.x >= x && other.y >= y && other.right() <= right() && other.bottom() <= bottom();
    }
};

enum class EHorizontalAnchor { Left, Center, Right };
enum class EVerticalAnchor { Top, Bottom };

/// A HUD area as authored against the nominal design size. The anchor says which screen edge the area
/// keeps its distance from when the window is larger or smaller than the design.
struct FZoneSpec {
    EHorizontalAnchor horizontal{EHorizontalAnchor::Left};
    EVerticalAnchor vertical{EVerticalAnchor::Top};
    FRect rect;
};

/// The named areas the combat HUD fills. The member names are the keys of "zones" in ui_layout.json.
template <class T>
struct FHudZones {
    T title;      // top bar: expedition name and room
    T turnOrder;  // top bar: round counter and initiative portraits
    T status;     // top bar: depth, materials, checkpoint
    T party;      // dock: party portraits with life bars
    T unit;       // dock: the acting unit's name, life and action points
    T skills;     // dock: skill slots (or the enemy-turn banner)
    T endTurn;    // dock: end turn button
    T journal;    // dock: battle journal
    T hints;      // dock: control hints
    T targets;    // floating card over the playfield: selected skill and its legal targets
};

struct FInsets {
    float left{0.f};
    float right{0.f};
    float top{0.f};
    float bottom{0.f};
};

/// WP-12 density rule: above this many declared enemy intents the board keeps only the numbers and drops
/// the glyph/badge decoration. Authored in ui_layout.json (`combat.intent_compact_threshold`).
inline constexpr int kDefaultIntentCompactThreshold = 12;

/// WP-24b onboarding card placement (design px). It is not a HUD zone: the card is drawn straight into
/// ImGui's foreground draw list (no window, so it cannot take input by construction) above every other
/// window, and its height comes from the wrapped text at draw time — no authored height can overflow 1.25.
struct FOnboardingSpec {
    float stripWidth{560.f};   // battle hint: centered card under the top band
    float sideWidth{340.f};    // reward/camp hint: card in the right margin beside the centered modal
    float topMargin{6.f};      // distance below the arena's top edge
    float sideMargin{16.f};    // gap to the modal column and to the screen edge
    float padding{12.f};       // inner padding on every side
    /// Width of the centered run-phase modals (FDungeonRewardView / FDungeonCampView / FDungeonEventView),
    /// in SCREEN pixels: those views do not scale with the layout, so the side card must clear this band.
    float modalClearanceWidth{580.f};
};

/// Narrowest usable side card (screen px at scale 1): the layout refuses a document that leaves less.
inline constexpr float kOnboardingSideMinWidth = 160.f;

/// Authored layout of the floating combat HUD (assets/data/ui_layout.json). Every number is in design pixels.
struct FUiLayoutSpec {
    float designWidth{1280.f};
    float designHeight{800.f};
    /// Narrowest width (design px) the zones are laid out for; narrower windows shrink the whole HUD.
    float minWidth{1140.f};
    /// Strips kept clear of the arena for the HUD clusters; what remains is the playfield viewport. Nothing
    /// paints them: the world reaches every window edge.
    FInsets bands;
    int intentCompactThreshold{kDefaultIntentCompactThreshold};
    FOnboardingSpec onboarding;
    FHudZones<FZoneSpec> zones;
};

/// The layout resolved for one window size, all in screen pixels.
struct FUiLayout {
    /// Screen pixels per design pixel (uniform, so portraits and slots never distort).
    float scale{1.f};
    FRect screen;
    FRect topBar;
    FRect dock;
    FRect leftRail;
    FRect rightRail;
    /// The window minus the bands.
    FRect viewport;
    /// The part of the viewport clear of the top and bottom HUD clusters; the isometric arena is fitted into
    /// it, so the clusters never cover a cell even when the bands reserve nothing.
    FRect arena;
    /// The onboarding hint anchors (screen px). `strip` is the battle hint (top-centre under the top band);
    /// `side` is the reward/camp hint in the right margin beside the centered run-phase modal column
    /// (`modalColumn`). The card is drawn at the anchor's top-left with the anchor's width and the wrapped
    /// text's height, so it always stays inside the anchor. All three lie inside `arena`.
    struct FOnboardingPlacement {
        FRect strip;
        FRect side;
        FRect modalColumn;
        float padding{0.f};
    } onboarding;
    /// The authored density threshold (design data, never a magic number in the renderer).
    int intentCompactThreshold{kDefaultIntentCompactThreshold};
    FHudZones<FRect> zones;
};

/// Reads and validates a ui_layout.json document; the error names the first problem found.
[[nodiscard]] std::expected<FUiLayoutSpec, std::string> ParseUiLayoutSpec(const FAssetDigest& digest);

/// Resolves the spec for a window. The scale follows the window height (and the narrowest supported
/// width), so a wider window gives the extra width to the gaps between clusters and the viewport, and a
/// taller window gives the extra height to the viewport only.
[[nodiscard]] FUiLayout ComputeUiLayout(const FUiLayoutSpec& spec, float screenWidth, float screenHeight);

} // namespace game::ui
