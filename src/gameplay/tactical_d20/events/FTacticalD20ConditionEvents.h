#pragma once

#include <entt/entt.hpp>
#include <string>

namespace game {

struct FTacticalD20ConditionChangedEvent {
    entt::entity unit{entt::null};
    std::string conditionId;
    std::string change;
    int remainingRounds{0};
};

struct FTacticalD20ConditionAppliedEvent {
    entt::entity unit{entt::null};
    std::string conditionId;
    int remainingRounds{0};
};

struct FTacticalD20ConditionTickedEvent {
    entt::entity unit{entt::null};
    std::string conditionId;
    int remainingRounds{0};
};

struct FTacticalD20ConditionExpiredEvent {
    entt::entity unit{entt::null};
    std::string conditionId;
};

} // namespace game
