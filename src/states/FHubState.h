#pragma once

/// @deprecated Legacy state used only for reference.
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
