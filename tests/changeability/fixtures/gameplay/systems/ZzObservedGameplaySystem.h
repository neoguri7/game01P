#pragma once

#include "ecs/systems/ISystem.h"

#include <tracy/Tracy.hpp>

// V38 [R32 negative control, gameplay coverage]: the same shape as ZzQuietGameplaySystem.h but with a Tracy span,
// so the gameplay R32 assertion cannot pass by flagging every gameplay system.
namespace game::gameplay {

struct ZzObservedGameplaySystem final : public ecs::ISystem {
    void update(entt::registry& registry, float dt) override {
        ZoneScopedN("ZzObservedGameplay");
        (void)registry;
        (void)dt;
    }

    [[nodiscard]] const char* name() const override { return "ZzObservedGameplay"; }
};

} // namespace game::gameplay
