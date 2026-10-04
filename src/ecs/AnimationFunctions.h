#pragma once

#include "core/AssetHandles.h"

#include <entt/entt.hpp>

namespace game::ecs {

/// Starts a clip on an entity with an FSprite. Passing restart=false leaves the same running clip intact.
[[nodiscard]] bool PlayAnimation(entt::registry& registry, entt::entity entity, FAnimClipId clip, bool restart = false);

} // namespace game::ecs
