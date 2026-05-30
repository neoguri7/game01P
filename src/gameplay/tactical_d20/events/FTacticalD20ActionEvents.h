#pragma once

#include <entt/entt.hpp>
#include <string>

namespace game {

struct FTacticalD20ActionResolvedEvent {
    entt::entity unit{entt::null};
    std::string actionId{"wait"};
    int remainingMovementTiles{0};
    bool turnComplete{false};
};

struct FTacticalD20AttackResolvedEvent {
    entt::entity attacker{entt::null};
    entt::entity target{entt::null};
    std::string weaponId;
    int d20{0};
    int total{0};
    int effectiveArmorClass{0};
    bool hit{false};
    bool criticalHit{false};
    bool coverApplied{false};
    bool disadvantageApplied{false};
};

struct FTacticalD20AttackRollResolvedEvent {
    entt::entity attacker{entt::null};
    entt::entity target{entt::null};
    std::string weaponId;
    int naturalRoll{0};
    int total{0};
    int targetNumber{0};
    bool hit{false};
    bool criticalHit{false};
    std::string breakdown;
};

} // namespace game
