#pragma once
#include <string>

namespace game::ecs {
    constexpr float kZzMaxSpeed = 3.5f;

    struct FZzBad {
        virtual void update(float dt);      // V2: virtual logic inside a component
        virtual ~FZzBad() = default;
        std::string* cachedLabel{nullptr};  // V2: component owns raw pointer
        bool isDead{false};                 // V11: bool state soup
        bool isAttacking{false};            // V11
        float t{0.f};
    };
}
