#pragma once

#include <entt/entt.hpp>

namespace game {

/// Placeholder ctx service that marks the future asset-management boundary.
/// Intentionally owns no loading API, manifests, caches, or runtime resources.
struct FAssetManager {
    static void Initialize(entt::registry& registry) {
        if (!registry.ctx().contains<FAssetManager>()) {
            registry.ctx().emplace<FAssetManager>();
        }
    }

    static void Shutdown(entt::registry& registry) {
        registry.ctx().erase<FAssetManager>();
    }
};

} // namespace game
