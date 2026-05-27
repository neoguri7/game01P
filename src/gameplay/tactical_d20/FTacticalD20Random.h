#pragma once

#include <random>

namespace game {

struct FTacticalD20Random {
    std::mt19937 rng{std::random_device{}()};
};

} // namespace game