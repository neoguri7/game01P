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

/// Authored layout of the full-screen HUD frame (assets/data/ui_layout.json). Every number is in design
/// pixels: the frame art is stretched to the design size, so the art can be redrawn at any resolution
/// without touching the layout.
struct FUiLayoutSpec {
    float designWidth{1280.f};
    float designHeight{800.f};
    /// Narrowest width (design px) the zones are laid out for; narrower windows shrink the whole HUD.
    float minWidth{1140.f};
    /// How far the frame reaches in from each screen edge; what remains is the playfield viewport.
    FInsets bands;
    /// Where the frame art is cut into nine pieces. Corner blocks keep their size, the strips between
    /// them stretch, so the horizontal/vertical middle of the art should tolerate stretching.
    FInsets slice;
    /// Asset id of the optional frame image; without it the bands are drawn with the stone panel skin.
    std::string texture;
    FHudZones<FZoneSpec> zones;
};

/// The frame resolved for one window size, all in screen pixels.
struct FUiLayout {
    /// Screen pixels per design pixel (uniform, so portraits and slots never distort).
    float scale{1.f};
    FRect screen;
    FRect topBar;
    FRect dock;
    FRect leftRail;
    FRect rightRail;
    /// The transparent window the world shows through; the isometric arena is fitted into it.
    FRect viewport;
    FHudZones<FRect> zones;
};

/// Reads and validates a ui_layout.json document; the error names the first problem found.
[[nodiscard]] std::expected<FUiLayoutSpec, std::string> ParseUiLayoutSpec(const FAssetDigest& digest);

/// Resolves the spec for a window. The scale follows the window height (and the narrowest supported
/// width), so a wider window gives the extra width to the middle of the bars and the viewport, and a
/// taller window gives the extra height to the viewport only.
[[nodiscard]] FUiLayout ComputeUiLayout(const FUiLayoutSpec& spec, float screenWidth, float screenHeight);

} // namespace game::ui
