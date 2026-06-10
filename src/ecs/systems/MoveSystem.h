#pragma once

#include "ecs/systems/ISystem.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FVelocity.h"  // you will create this next if needed

#include <entt/entt.hpp>
#include <string>
#include <tracy/Tracy.hpp>

namespace game::ecs {

struct MoveSystem : public ISystem {
    void update(entt::registry& reg, float dt) override {
        ZoneScopedN("MoveSystem");

        auto view = reg.view<FPosition, FVelocity>();
        for (auto entity : view) {
            auto& pos = view.get<FPosition>(entity);
            const auto& vel = view.get<FVelocity>(entity);

            pos.x += vel.vx * dt;
            pos.y += vel.vy * dt;
        }
    }

    std::string name() const override { return "MoveSystem"; }
};

} // namespace game::ecs
