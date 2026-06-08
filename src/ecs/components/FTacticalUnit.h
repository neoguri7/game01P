#pragma once

#include <string>

namespace game {

struct FTacticalUnit {
    std::string id;
    std::string team;
    std::string displayName;
    int spawnOrder{0};
};

} // namespace game
