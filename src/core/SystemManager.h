#pragma once

#include "ecs/systems/ISystem.h"
#include <entt/entt.hpp>
#include <memory>
#include <vector>
#include <string>
#include <concepts>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

namespace game {

/**
 * Owns a list of systems. Systems are stored as unique_ptr<ISystem> wrappers.
 * Registration is type-safe via templated AddSystem().
 * Update order == registration order.
 */
class SystemManager {
public:
    SystemManager() = default;
    ~SystemManager() = default;

    template<ecs::SystemConcept T>
    void addSystem() {
        auto sys = std::make_unique<ecs::SystemWrapper<T>>();
        spdlog::info("[SystemManager] Registered system: {}", sys->name());
        systems.push_back(std::move(sys));
    }

    void addSystem(std::unique_ptr<ecs::ISystem> sys) {
        if (sys) {
            spdlog::info("[SystemManager] Registered system: {}", sys->name());
            systems.push_back(std::move(sys));
        }
    }

    void onAllSystemsRegistered(entt::registry& reg) {
        for (auto& sys : systems) {
            sys->onRegister(reg);
        }
    }

    void updateAll(entt::registry& reg, float deltaTime) {
        ZoneScopedN("SystemManager::updateAll");
        for (const auto& sys : systems) {
            sys->update(reg, deltaTime);
        }
    }

    [[nodiscard]] size_t getRegisteredCount() const noexcept {
        return systems.size();
    }

    void clear() {
        systems.clear();
    }

private:
    std::vector<std::unique_ptr<ecs::ISystem>> systems;
};

} // namespace game
