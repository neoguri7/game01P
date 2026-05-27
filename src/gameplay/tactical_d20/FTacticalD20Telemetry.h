#pragma once

#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace game {

struct FTacticalD20EventCount {
    std::string name;
    int count{0};
};

struct FTacticalD20Telemetry {
    float deltaTimeSeconds{0.f};
    int entityCount{0};
    std::string combatState{"None"};
    entt::entity activeUnit{entt::null};
    std::string activeUnitId{"None"};
    int round{0};
    std::vector<FTacticalD20EventCount> eventCountsThisFrame;
    std::string lastCommandDropResult{"None"};
    std::string lastD20RollBreakdown{"None"};
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
