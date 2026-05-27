#pragma once

namespace game {

struct FTacticalBoardTile {
    int tileX{0};
    int tileY{0};
    int tileFeet{5};
    bool isWall{false};
    bool isCover{false};
};

} // namespace game