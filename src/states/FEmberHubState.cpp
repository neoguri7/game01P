#include "FEmberHubState.h"
#include "FEmberMenuRequest.h"
#include "FEmberTitleState.h"
#include "core/InputState.h"
#include "presentation/FTitleMenuView.h"

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
    if (auto pending = game::TakeTitleMenuTransition(Registry)) return *pending;
    if (const auto next = ResolveEmberMenuRequest(Registry)) return *next;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (!input) return {};
    // WP-21 §5: no Confirm shortcut into a battle — the menu row is the only start input, so the
    // preparation screen (and its overwrite confirmation) can never be bypassed.
    if (input->isActionPressed(game::EInputAction::Cancel)) {
        return {EStateChangeKind::ReplaceTop, std::string(kEmberTitleId)};
    }
    return {};
}

void FEmberHubState::Render(entt::registry& Registry)
{
    game::DrawTitleMenu(Registry);
}
