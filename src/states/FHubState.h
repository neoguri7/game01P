#pragma once

/// High-level app-flow state for hub scene flow only.
/// Do not add entity gameplay state here; gameplay transitions belong in ECS
/// tag components and FEventBus-driven systems.
#include "FBaseState.h"

class FHubState : public FBaseState
{
public:
    FHubState() = default;
    ~FHubState() override = default;

    EStateTransition Update(float DeltaTime) override;
    void OnEnter() override;
    void OnExit() override;
};
