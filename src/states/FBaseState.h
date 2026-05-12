#pragma once

/// @deprecated This is legacy high-level state. Prefer stateful systems (e.g. FGameFlowSystem) in new code.
/// Transition enum: states communicate intent, the state manager executes the switch.
enum class EStateTransition
{
    None,
    ToTitle,
    ToHub,
    ToDungeon,
    ToBossFloor,
    ToCombat,
    ToGameOver,
    Quit
};

/// Base state interface for polymorphic game states.
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
