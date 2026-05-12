#pragma once

/// @deprecated Legacy state. Use GameStateMachine only if high-level flow is simple.
/// Prefer implementing logic as pure ECS systems (FTitleSystem) that read/write registry components.
#include "FBaseState.h"

class FTitleState : public FBaseState
{
public:
    FTitleState() = default;
    ~FTitleState() override = default;

    EStateTransition Update(float DeltaTime) override;
};
