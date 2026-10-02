#pragma once

#include <entt/entt.hpp>
#include <string_view>

/// App-flow transition intents only.
/// Gameplay state must not be represented here; use ECS tag components plus
/// FEventBus-driven transition systems with documented transition tables.
enum class EStateTransition
{
    None,
    ToTitle,
    ToHub,
    ToBattle,
    ToPrototype,
    ToGameOver,
    Quit
};

/// Base interface for high-level app-flow states only (title, menus, scene flow).
/// Gameplay state such as alive/dead/attacking/stunned belongs in ECS tags and
/// event-driven systems, not in this polymorphic state stack.
class FBaseState
{
public:
    virtual ~FBaseState() = default;

    /// Per-frame logic. Returns transition intent.
    virtual EStateTransition Update(entt::registry& Registry, float DeltaTime) = 0;

    /// Human-readable app-flow state name for diagnostics.
    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;

    /// Called when this state becomes active.
    virtual void OnEnter(entt::registry& Registry) {}

    /// Called when this state is being replaced.
    virtual void OnExit(entt::registry& Registry) {}
};
