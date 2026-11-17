#pragma once

#include "ecs/systems/ISystem.h"
#include <entt/entt.hpp>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <concepts>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

namespace game {

/**
 * Owns a list of systems. Systems are stored as unique_ptr<ISystem> wrappers.
 * Registration is type-safe via templated AddSystem().
 * Update order == registration order.
 *
 * Update and render are separate flags (WP-01 §2): a state that stays visible under a modal keeps its
 * render systems drawing while their update stops ("paused" stops processing, not drawing).
 * setEnabled() keeps its original meaning and sets both flags.
 */
class SystemManager {
public:
    SystemManager() = default;
    ~SystemManager() = default;

    template<ecs::SystemConcept T>
    void addSystem() {
        auto sys = std::make_unique<ecs::SystemWrapper<T>>();
        spdlog::info("[SystemManager] Registered system: {}", sys->name());
        systems.push_back({ std::move(sys), false, false });
    }

    void addSystem(std::unique_ptr<ecs::ISystem> sys) {
        if (sys) {
            spdlog::info("[SystemManager] Registered system: {}", sys->name());
            systems.push_back({ std::move(sys), false, false });
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
            if (entry.update) entry.system->update(reg, deltaTime);
        }
    }

    void renderEnabled(entt::registry& reg) {
        ZoneScopedN("SystemManager::renderEnabled");
        for (const auto& entry : systems) {
            if (entry.render) entry.system->render(reg);
        }
    }

    /// Enable or disable update and render together (the original single-switch meaning).
    bool setEnabled(std::string_view name, bool enabled) {
        const auto entry = std::ranges::find_if(systems, [name](const FSystemEntry& candidate) {
            return candidate.system->name() == name;
        });
        if (entry == systems.end()) return false;
        entry->update = enabled;
        entry->render = enabled;
        return true;
    }

    /// Enable rendering only: update stays off. Used for a state that keeps its screen under a modal.
    bool setRenderOnly(std::string_view name) {
        const auto entry = std::ranges::find_if(systems, [name](const FSystemEntry& candidate) {
            return candidate.system->name() == name;
        });
        if (entry == systems.end()) return false;
        entry->update = false;
        entry->render = true;
        return true;
    }

    void disableAll() noexcept {
        for (auto& entry : systems) {
            entry.update = false;
            entry.render = false;
        }
    }

    /// True when the system's update is enabled (render-only state is reported as disabled here).
    [[nodiscard]] bool isEnabled(std::string_view name) const {
        const auto entry = std::ranges::find_if(systems, [name](const FSystemEntry& candidate) {
            return candidate.system->name() == name;
        });
        return entry != systems.end() && entry->update;
    }

    [[nodiscard]] bool isRenderEnabled(std::string_view name) const {
        const auto entry = std::ranges::find_if(systems, [name](const FSystemEntry& candidate) {
            return candidate.system->name() == name;
        });
        return entry != systems.end() && entry->render;
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

    /// Systems whose update is enabled (render-only systems are not listed; see isRenderEnabled).
    [[nodiscard]] std::vector<std::string> getEnabledSystemNames() const {
        std::vector<std::string> names;
        for (const auto& entry : systems) {
            if (entry.update) names.push_back(entry.system->name());
        }
        return names;
    }

    void clear() {
        systems.clear();
    }

private:
    struct FSystemEntry {
        std::unique_ptr<ecs::ISystem> system;
        bool update{false};
        bool render{false};
    };

    std::vector<FSystemEntry> systems;
};

} // namespace game
