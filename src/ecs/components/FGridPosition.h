#pragma once

namespace game {

struct FGridPosition {
    int x{0};
    int y{0};
};

inline bool operator==(const FGridPosition& a, const FGridPosition& b) noexcept {
    return a.x == b.x && a.y == b.y;
}

} // namespace game
