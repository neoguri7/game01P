#pragma once

#include <glm/vec2.hpp>

#include "gameplay/grid/FGridTypes.h"

namespace game {

/// Isometric projection of the battle grid. Logic stays on the orthogonal grid; only the Ember presentation
/// and mouse picking go through this view. One cell is a 64x32 diamond at scale 1.
inline constexpr float kIsoHalfWidth = 32.f;
inline constexpr float kIsoHalfHeight = 16.f;
/// Screen rows kept free for the HUD above and below the playfield.
inline constexpr float kIsoTopReserve = 84.f;
inline constexpr float kIsoBottomReserve = 168.f;
/// The unit card sits on the left, so the arena is nudged right to keep it clear of the party.
inline constexpr float kIsoCenterShift = 48.f;

struct FIsoView {
    glm::vec2 origin{};  // screen position of grid corner (0, 0): the top vertex of cell (0, 0)
    float scale{1.f};

    /// Screen position of a continuous grid coordinate (cell corners are whole numbers).
    [[nodiscard]] glm::vec2 corner(float gridX, float gridY) const noexcept {
        return {origin.x + (gridX - gridY) * kIsoHalfWidth * scale, origin.y + (gridX + gridY) * kIsoHalfHeight * scale};
    }
    [[nodiscard]] glm::vec2 cellCenter(int x, int y) const noexcept {
        return corner(static_cast<float>(x) + .5f, static_cast<float>(y) + .5f);
    }
};

/// Fits the grid between the HUD reserves, centered, at a 0.5-step scale so pixel art stays even.
[[nodiscard]] FIsoView MakeIsoView(const FGridMetrics& metrics, float viewportWidth, float viewportHeight) noexcept;

/// Cell under a screen point; false when the point lies outside the grid.
[[nodiscard]] bool IsoScreenToCell(const FIsoView& view, const FGridMetrics& metrics, glm::vec2 screen,
                                   int& outX, int& outY) noexcept;

} // namespace game
