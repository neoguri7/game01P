#pragma once

#include <entt/entt.hpp>

#include <memory>

namespace game {

class IAssetManager {
public:
    virtual ~IAssetManager() = default;
};

class FNoOpAssetManager final : public IAssetManager {};

struct FAssetManager {
    std::shared_ptr<IAssetManager> implementation{std::make_shared<FNoOpAssetManager>()};

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
