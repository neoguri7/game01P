#include "FEmberTitleState.h"
#include "FEmberHubState.h"
#include "FEmberMenuRequest.h"
#include "core/InputState.h"
#include "presentation/FTitleMenuView.h"

FStateTransition FEmberTitleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    // A transition-only row (settings/compendium) is the title's own hand-off; the title never starts a run.
    if (auto pending = game::TakeTitleMenuTransition(Registry)) return *pending;
    if (const auto next = ResolveEmberMenuRequest(Registry)) return *next;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (input && input->isActionPressed(game::EInputAction::Confirm)) {
        return {EStateChangeKind::ReplaceTop, std::string(kEmberHubId)};
    }
    return {};
}

void FEmberTitleState::Render(entt::registry& Registry)
{
    game::DrawTitleMenu(Registry);
}
