#pragma once

/// High-level app-flow state for the title screen only.
/// Do not add entity gameplay state here; gameplay transitions belong in ECS
/// tag components and FEventBus-driven systems.
#include "FBaseState.h"

class FTitleState : public FBaseState
{
public:
    FTitleState() = default;
    ~FTitleState() override = default;

    EStateTransition Update(float DeltaTime) override;
};
