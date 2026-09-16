#pragma once

#include "ecs/systems/ISystem.h"

// V35 [R1, gameplay coverage]: an ISystem in gameplay/systems whose type name does not match its file name.
// The R1 loop used to walk only src/ecs/systems, so a gameplay system could be registered under any name.
namespace game::gameplay {

struct ZzOtherSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float dt) override {
        (void)registry;
        (void)dt;
    }

    [[nodiscard]] const char* name() const override { return "ZzOther"; }
};

} // namespace game::gameplay
