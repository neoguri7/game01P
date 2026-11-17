#pragma once

#include <glm/vec2.hpp>

#include "ecs/components/FCamera.h"

namespace game {

struct FViewport {
    float width{1280.f};
    float height{720.f};
};

[[nodiscard]] glm::vec2 WorldToScreen(glm::vec2 world, const FCamera& camera, const FViewport& viewport) noexcept;
[[nodiscard]] glm::vec2 ScreenToWorld(glm::vec2 screen, const FCamera& camera, const FViewport& viewport) noexcept;

} // namespace game
