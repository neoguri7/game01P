#pragma once

#include <entt/entt.hpp>
#include <string>

namespace game {

struct FTacticalD20CommandTokenFactory {
    static entt::entity create(entt::registry& registry, const std::string& id, const std::string& displayName, int trayIndex);
};

} // namespace game