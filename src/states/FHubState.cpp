#include "FHubState.h"

void FHubState::OnEnter()
{
    // Initialize this app-flow state.
}

void FHubState::OnExit()
{
    // Cleanup this app-flow state.
}

EStateTransition FHubState::Update(float DeltaTime)
{
    // TODO: Handle input to transition to the next app-flow state.
    return EStateTransition::None;
}
