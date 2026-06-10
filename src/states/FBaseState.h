#pragma once

/// App-flow transition intents only.
/// Gameplay state must not be represented here; use ECS tag components plus
/// FEventBus-driven transition systems with documented transition tables.
enum class EStateTransition
{
    None,
    ToTitle,
    ToHub,
    ToDungeon,
    ToBossFloor,
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
    virtual EStateTransition Update(float DeltaTime) = 0;

    /// Called when this state becomes active.
    virtual void OnEnter() {}

    /// Called when this state is being replaced.
    virtual void OnExit() {}
};
