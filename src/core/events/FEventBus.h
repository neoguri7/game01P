#pragma once
#include <entt/entt.hpp>
#include <functional>
#include <unordered_map>
#include <typeindex>
#include <any>
#include <vector>
#include <memory>

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
/// Usage:
///   auto* bus = reg.ctx().find<FEventBus>();
///   bus->subscribe<DamagedEvent>([](const DamagedEvent& e) { ... });
///   bus->publish<DamagedEvent>({ .entity = e, .amount = 10 });
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

    void clear() { callbacks_.clear(); }

private:
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<IEventCallback>>> callbacks_;
};

inline void InitializeEventBus(entt::registry& reg) {
    if (!reg.ctx().contains<FEventBus>()) {
        reg.ctx().emplace<FEventBus>();
    }
}

} // namespace game

// Convenience macros
#define SUBSCRIBE(type, reg, lambda) \
    if (auto* __bus = (reg).ctx().find<::game::FEventBus>()) \
        __bus->subscribe<type>(lambda)

#define PUBLISH(type, reg, eventInstance) \
    if (auto* __bus = (reg).ctx().find<::game::FEventBus>()) \
        __bus->publish<type>(eventInstance)
