#pragma once

#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include <entt/entt.hpp>
#include <string_view>

namespace game {

struct FTacticalD20ConfigLoader {
    static constexpr std::string_view DefaultPath = "assets/data/tactical_d20_combat_demo.json";

    static FTacticalD20Config load(std::string_view path = DefaultPath);
    static void Initialize(entt::registry& registry, std::string_view path = DefaultPath);
};

} // namespace game