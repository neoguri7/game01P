#pragma once

#include <entt/entt.hpp>
#include <concepts>
#include <memory>
#include <string>
#include <vector>

namespace game::ecs {

// Every concrete system must satisfy this minimal contract
// Usage: struct MySystem { void update(entt::registry&, float); std::string name() const { return "My"; } };
template<typename T>
concept SystemConcept = requires(T t, entt::registry& reg, float dt) {
    { t.update(reg, dt) } -> std::same_as<void>;
    { t.name() } -> std::convertible_to<std::string>;
};

// Simple owning wrapper for heterogeneous systems registered in SystemManager
struct ISystem {
    virtual ~ISystem() = default;
    virtual void update(entt::registry& reg, float dt) = 0;
    virtual std::string name() const = 0;

    // Optional: called once after registration
    virtual void onRegister(entt::registry& /*reg*/) {}
};

template<SystemConcept ConcreteSystem>
struct SystemWrapper final : public ISystem {
    SystemWrapper() = default;

    void update(entt::registry& reg, float dt) override {
        impl.update(reg, dt);
    }

    std::string name() const override {
        return impl.name();
    }

private:
    ConcreteSystem impl;
};

} // namespace game::ecs
