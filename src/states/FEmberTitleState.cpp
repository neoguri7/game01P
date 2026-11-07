#include "FEmberTitleState.h"
#include "FEmberHubState.h"
#include "FEmberMenuRequest.h"
#include "core/InputState.h"

FStateTransition FEmberTitleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (const auto next = ResolveEmberMenuRequest(Registry)) return *next;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (input && input->isActionPressed(game::EInputAction::Confirm)) {
        return {EStateChangeKind::ReplaceTop, std::string(kEmberHubId)};
    }
    return {};
}
