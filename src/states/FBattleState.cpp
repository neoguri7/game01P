#include "FBattleState.h"
#include "core/InputState.h"

EStateTransition FBattleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (input && input->isActionPressed(game::EInputAction::Cancel)) {
        return EStateTransition::ToHub;
    }
    return EStateTransition::None;
}
