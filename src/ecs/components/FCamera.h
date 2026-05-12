#pragma once
#include <glm/vec2.hpp>

namespace game {

/// Orthographic camera component — offset + zoom.
/// Default camera sits at origin, 1:1 pixel scale.
struct FCamera {
    glm::vec2 position{0.f, 0.f};
    float     zoom{1.f};
};

} // namespace game
