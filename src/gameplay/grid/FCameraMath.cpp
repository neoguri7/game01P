#include "gameplay/grid/FCameraMath.h"

namespace game {

glm::vec2 WorldToScreen(glm::vec2 world, const FCamera& camera, const FViewport& viewport) noexcept {
    const glm::vec2 viewportCenter{viewport.width * 0.5f, viewport.height * 0.5f};
    const float zoom = camera.zoom > 0.f ? camera.zoom : 1.f;
    return (world - camera.position) * zoom + viewportCenter;
}

glm::vec2 ScreenToWorld(glm::vec2 screen, const FCamera& camera, const FViewport& viewport) noexcept {
    const glm::vec2 viewportCenter{viewport.width * 0.5f, viewport.height * 0.5f};
    const float zoom = camera.zoom > 0.f ? camera.zoom : 1.f;
    return (screen - viewportCenter) / zoom + camera.position;
}

} // namespace game
