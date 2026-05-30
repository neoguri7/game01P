#pragma once

#include "core/events/FEventBus.h"

#include <entt/entt.hpp>
#include <functional>
#include <utility>

namespace game {

template<typename T>
bool PublishEvent(entt::registry& registry, const T& event) {
    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return false;

    bus->publish<T>(event);
    return true;
}

template<typename T>
bool QueueFrameEvent(entt::registry& registry, const T& event) {
    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return false;

    bus->queueFrame<T>(event);
    return true;
}

template<typename T>
bool PublishAndQueueFrameEvent(entt::registry& registry, const T& event) {
    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return false;

    bus->publish<T>(event);
    bus->queueFrame<T>(event);
    return true;
}

template<typename T>
bool SubscribeEvent(entt::registry& registry, std::function<void(const T&)> callback) {
    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return false;

    bus->subscribe<T>(std::move(callback));
    return true;
}

} // namespace game
