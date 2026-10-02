#include "FTitleState.h"
#include "core/InputState.h"

EStateTransition FTitleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (input && input->isActionPressed(game::EInputAction::Confirm)) {
        return EStateTransition::ToHub;
    }
    return EStateTransition::None;
}
