#pragma once

#include "utils/cute_c2.h"
#include <entt/entt.hpp>

namespace game {

/// Frame-bound collision event emitted by CollisionSystem through FEventBus.
struct FCollisionEvent {
    entt::entity a;
    entt::entity b;
    c2Manifold manifold;
};

} // namespace game
