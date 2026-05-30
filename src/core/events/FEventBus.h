#pragma once
#include <entt/entt.hpp>
#include <any>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace game {

/// Type-erased event callback stored in the event bus.
struct IEventCallback {
    virtual ~IEventCallback() = default;
};

template<typename T>
struct TypedEventCallback : IEventCallback {
    std::function<void(const T&)> fn;
    explicit TypedEventCallback(std::function<void(const T&)> f) : fn(std::move(f)) {}
};

/// Event system — lives in registry.ctx().
///
/// Dispatch rules:
/// - publish<T>() dispatches immediately to subscribers in subscription order.
/// - queueFrame<T>() stores frame-bound events for systems later in the same
///   frame; consumers read frameEvents<T>().
/// - beginFrame() clears frame queues once at frame start. Queued events remain
///   available through update/render debug for that frame and are never
///   dispatched implicitly.
/// - clear() is shutdown cleanup and removes subscriptions and queued events.
///
/// Usage:
///   auto* bus = reg.ctx().find<FEventBus>();
///   bus->subscribe<DamagedEvent>([](const DamagedEvent& e) { ... });
///   bus->publish<DamagedEvent>({ .entity = e, .amount = 10 });
///   bus->queueFrame<CollisionEvent>({ .a = a, .b = b });
struct FEventBus {
    template<typename T>
    void subscribe(std::function<void(const T&)> callback) {
        auto cb = std::make_shared<TypedEventCallback<T>>(std::move(callback));
        callbacks_[std::type_index(typeid(T))].push_back(cb);
    }

    template<typename T>
    void publish(const T& event) {
        auto it = callbacks_.find(std::type_index(typeid(T)));
        if (it == callbacks_.end()) return;

        for (const auto& rawCb : it->second) {
            auto* cb = dynamic_cast<TypedEventCallback<T>*>(rawCb.get());
            if (cb) cb->fn(event);
        }
    }

    template<typename T>
    void queueFrame(const T& event) {
        auto& bucket = frameEvents_[std::type_index(typeid(T))];
        if (!bucket.has_value()) {
            bucket = std::vector<T>{};
        }

        std::any_cast<std::vector<T>&>(bucket).push_back(event);
    }

    template<typename T>
    [[nodiscard]] const std::vector<T>& frameEvents() const {
        static const std::vector<T> empty;

        auto it = frameEvents_.find(std::type_index(typeid(T)));
        if (it == frameEvents_.end()) return empty;

        return std::any_cast<const std::vector<T>&>(it->second);
    }

    void beginFrame() { frameEvents_.clear(); }

    void clear() {
        callbacks_.clear();
        frameEvents_.clear();
    }

private:
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<IEventCallback>>> callbacks_;
    std::unordered_map<std::type_index, std::any> frameEvents_;
};

inline void InitializeEventBus(entt::registry& reg) {
    if (!reg.ctx().contains<FEventBus>()) {
        reg.ctx().emplace<FEventBus>();
    }
}

} // namespace game

// Legacy convenience macros. Prefer the typed helpers in FEventPublishing.h for
// new code.
#define SUBSCRIBE(type, reg, lambda) \
    if (auto* __bus = (reg).ctx().find<::game::FEventBus>()) \
        __bus->subscribe<type>(lambda)

#define PUBLISH(type, reg, eventInstance) \
    if (auto* __bus = (reg).ctx().find<::game::FEventBus>()) \
        __bus->publish<type>(eventInstance)

#define QUEUE_FRAME_EVENT(type, reg, eventInstance) \
    if (auto* __bus = (reg).ctx().find<::game::FEventBus>()) \
        __bus->queueFrame<type>(eventInstance)
