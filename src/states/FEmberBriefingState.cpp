#include "states/FEmberBriefingState.h"

#include <string>

#include "core/InputState.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "presentation/FBriefingView.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberTitleState.h"

FStateTransition FEmberBriefingState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (!Registry.ctx().contains<game::FDungeonRun>()) {
        LOG_ERROR("The briefing has no run; returning to the title.");
        Registry.ctx().erase<game::FBriefingCommand>();
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    }
    if (!briefing.has_value()) {
        LOG_WARN("The briefing could not be built (no run sequence or content); entering the battle.");
        Registry.ctx().erase<game::FBriefingCommand>();
        return {EStateChangeKind::Root, std::string(kEmberBattleId)};
    }

    // One command per frame, taken and erased: the button's posted command, else the Confirm key.
    game::EBriefingCommand command = game::EBriefingCommand::None;
    if (const auto* posted = Registry.ctx().find<game::FBriefingCommand>()) {
        command = posted->value;
        Registry.ctx().erase<game::FBriefingCommand>();
    } else if (const auto* input = Registry.ctx().find<game::FInputState>();
               input != nullptr && input->isActionPressed(game::EInputAction::Confirm)) {
        command = game::EBriefingCommand::Enter;
    }
    if (command == game::EBriefingCommand::Enter) {
        return {EStateChangeKind::Root, std::string(kEmberBattleId)};
    }
    return {};
}

void FEmberBriefingState::Render(entt::registry& Registry)
{
    if (briefing.has_value()) game::DrawBriefingScreen(Registry, *briefing);
}

void FEmberBriefingState::OnEnter(entt::registry& Registry)
{
    briefing = game::BuildRunBriefing(Registry);
    Registry.ctx().erase<game::FBriefingCommand>();
}
