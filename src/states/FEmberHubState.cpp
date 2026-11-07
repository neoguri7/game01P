#include "FEmberHubState.h"
#include "FEmberBattleState.h"
#include "FEmberMenuRequest.h"
#include "FEmberTitleState.h"
#include "core/InputState.h"

void FEmberHubState::OnEnter(entt::registry& Registry)
{
    (void)Registry;
}

void FEmberHubState::OnExit(entt::registry& Registry)
{
    (void)Registry;
}

FStateTransition FEmberHubState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (const auto next = ResolveEmberMenuRequest(Registry)) return *next;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (!input) return {};
    if (input->isActionPressed(game::EInputAction::Confirm)) {
        return {EStateChangeKind::ReplaceTop, std::string(kEmberBattleId)};
    }
    if (input->isActionPressed(game::EInputAction::Cancel)) {
        return {EStateChangeKind::ReplaceTop, std::string(kEmberTitleId)};
    }
    return {};
}
