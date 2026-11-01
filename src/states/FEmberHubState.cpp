#include "FEmberHubState.h"
#include "FEmberMenuRequest.h"
#include "core/InputState.h"

void FEmberHubState::OnEnter(entt::registry& Registry)
{
    (void)Registry;
}

void FEmberHubState::OnExit(entt::registry& Registry)
{
    (void)Registry;
}

EStateTransition FEmberHubState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (const auto next = ResolveEmberMenuRequest(Registry)) return *next;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (!input) return EStateTransition::None;
    if (input->isActionPressed(game::EInputAction::Confirm)) return EStateTransition::ToBattle;
    if (input->isActionPressed(game::EInputAction::Cancel)) return EStateTransition::ToTitle;
    return EStateTransition::None;
}
