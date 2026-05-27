#pragma once

#include <entt/entt.hpp>

namespace game {

struct FTacticalD20BoardInteraction {
    bool hasHoveredTile{false};
    int hoveredTileX{0};
    int hoveredTileY{0};
    bool hasSelectedTile{false};
    int selectedTileX{0};
    int selectedTileY{0};
    entt::entity hoveredEntity{entt::null};
    entt::entity selectedEntity{entt::null};
};

} // namespace game
