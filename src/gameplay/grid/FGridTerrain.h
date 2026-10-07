#pragma once

#include <array>

#include "gameplay/grid/FGridTypes.h"

namespace game {

enum class ETile { Floor, Wall, Trap };

struct FGridTerrain {
    std::array<ETile, kGridWidth * kGridHeight> cells{};
    int moveRange{3};
    int trapDamage{8};

    [[nodiscard]] ETile at(int x, int y) const noexcept {
        if (x < 0 || y < 0 || x >= kGridWidth || y >= kGridHeight) return ETile::Wall;
        return cells[static_cast<std::size_t>(y * kGridWidth + x)];
    }
};

} // namespace game
