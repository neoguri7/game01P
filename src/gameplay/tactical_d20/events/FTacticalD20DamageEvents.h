#pragma once

#include <entt/entt.hpp>
#include <string>

namespace game {

struct FTacticalD20DamageAppliedEvent {
    entt::entity source{entt::null};
    entt::entity target{entt::null};
    std::string damageType{"weapon"};
    int damage{0};
    int hpBefore{0};
    int hpAfter{0};
    bool defeated{false};
};

} // namespace game
