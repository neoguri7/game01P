#pragma once

#include <string>

namespace game {

struct FTacticalD20CommandFeedback {
    std::string message;
    float lifetimeSeconds{0.f};
    bool accepted{false};
};

} // namespace game
