#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gameplay/grid/FGridTypes.h"

namespace game::gameplay {

struct FEnemyChoice {
    std::string unit;
    std::string displayName;
    int weight{1};
};

struct FIntRange {
    int min{0};
    int max{0};
};

struct FRoomGenConfig {
    std::vector<std::string> names;
    std::vector<FEnemyChoice> enemies;
    std::string bossUnit;
    std::string bossName;
    FIntRange enemyCount;
    FIntRange obstacleCount;
    FIntRange trapCount;
};

struct FRoomRequest {
    std::uint32_t seed{1};
    int depth{1};
    int room{0};
    bool boss{false};
    // Party start cells; they and every enemy are guaranteed to stay mutually reachable.
    std::span<const std::pair<int, int>> partyCells;
};

struct FGeneratedEnemy {
    std::string unit;
    std::string displayName;
    int x{0};
    int y{0};
};

struct FGeneratedRoom {
    static_assert(kGridWidth == kGridHeight, "generated rooms are square");
    static constexpr int kSize = kGridWidth;
    std::string name;
    std::array<std::string, kSize> terrain;
    std::vector<FGeneratedEnemy> enemies;
};

/// Deterministic for a given config and request. Enemies spawn in the top rows (away from the party),
/// obstacles and traps are scattered in the middle, and walls never disconnect any unit.
/// The room is an open map: nothing is placed on the outermost ring of cells, so the grid edge is the only boundary.
[[nodiscard]] FGeneratedRoom GenerateRoom(const FRoomGenConfig& config, const FRoomRequest& request);

} // namespace game::gameplay
