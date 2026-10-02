#include "FHubState.h"
#include "core/InputState.h"

void FHubState::OnEnter(entt::registry& Registry)
{
    (void)Registry;
}

void FHubState::OnExit(entt::registry& Registry)
{
    (void)Registry;
}

EStateTransition FHubState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (!input) return EStateTransition::None;
    if (input->isActionPressed(game::EInputAction::Confirm)) return EStateTransition::ToBattle;
    if (input->isActionPressed(game::EInputAction::Cancel)) return EStateTransition::ToTitle;
    return EStateTransition::None;
}
