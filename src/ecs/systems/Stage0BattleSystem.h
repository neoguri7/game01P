#pragma once

#include <cstdint>
#include <entt/entt.hpp>

namespace game::ecs {

struct FStage0BattleRuntime {
    std::uint64_t updateCount{0};
};

class Stage0BattleSystem {
public:
    void update(entt::registry& registry, float deltaTime) {
        (void)deltaTime;
        auto* runtime = registry.ctx().find<FStage0BattleRuntime>();
        if (!runtime) runtime = &registry.ctx().emplace<FStage0BattleRuntime>();
        ++runtime->updateCount;
    }

    [[nodiscard]] std::string name() const { return "Stage0BattleSystem"; }
};

} // namespace game::ecs
