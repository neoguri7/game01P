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
        systems.push_back({ std::move(sys), false });
    }

    void addSystem(std::unique_ptr<ecs::ISystem> sys) {
        if (sys) {
            spdlog::info("[SystemManager] Registered system: {}", sys->name());
            systems.push_back({ std::move(sys), false });
        }
    }

    void onAllSystemsRegistered(entt::registry& reg) {
        for (auto& entry : systems) {
            entry.system->onRegister(reg);
        }
    }

    void updateEnabled(entt::registry& reg, float deltaTime) {
        ZoneScopedN("SystemManager::updateEnabled");
        for (const auto& entry : systems) {
            if (entry.enabled) entry.system->update(reg, deltaTime);
        }
    }

    void renderEnabled(entt::registry& reg) {
        ZoneScopedN("SystemManager::renderEnabled");
        for (const auto& entry : systems) {
            if (entry.enabled) entry.system->render(reg);
        }
    }

    bool setEnabled(std::string_view name, bool enabled) {
        const auto entry = std::ranges::find_if(systems, [name](const FSystemEntry& candidate) {
            return candidate.system->name() == name;
        });
        if (entry == systems.end()) return false;
        entry->enabled = enabled;
        return true;
    }

    void disableAll() noexcept {
        for (auto& entry : systems) entry.enabled = false;
    }

    [[nodiscard]] bool isEnabled(std::string_view name) const {
        const auto entry = std::ranges::find_if(systems, [name](const FSystemEntry& candidate) {
            return candidate.system->name() == name;
        });
        return entry != systems.end() && entry->enabled;
    }

    [[nodiscard]] size_t getRegisteredCount() const noexcept {
        return systems.size();
    }

    [[nodiscard]] std::vector<std::string> getSystemNames() const {
        std::vector<std::string> names;
        names.reserve(systems.size());
        for (const auto& entry : systems) names.push_back(entry.system->name());
        return names;
    }

    [[nodiscard]] std::vector<std::string> getEnabledSystemNames() const {
        std::vector<std::string> names;
        for (const auto& entry : systems) {
            if (entry.enabled) names.push_back(entry.system->name());
        }
        return names;
    }

    void clear() {
        systems.clear();
    }

private:
    struct FSystemEntry {
        std::unique_ptr<ecs::ISystem> system;
        bool enabled{false};
    };

    std::vector<FSystemEntry> systems;
};

} // namespace game
