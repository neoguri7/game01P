#pragma once

#include "ecs/systems/ISystem.h"

// V37 [R32, gameplay coverage]: a gameplay system with no observation point at all — no ZoneScopedN span and no
// LOG_* call, so nothing can answer "is it running, and how long does it take?".
namespace game::gameplay {

struct ZzQuietGameplaySystem final : public ecs::ISystem {
    void update(entt::registry& registry, float dt) override {
        (void)registry;
        (void)dt;
    }

    [[nodiscard]] const char* name() const override { return "ZzQuietGameplay"; }
};

} // namespace game::gameplay
