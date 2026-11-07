#include "states/FEmberRunEndState.h"

#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FModalViewRegistry.h"
#include "states/FEmberTitleState.h"
#include "states/FRunFlowStateMap.h"

FStateTransition FEmberRunEndState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (!Registry.ctx().contains<game::FDungeonRun>()) {
        if (auto* stale = Registry.ctx().find<game::FRunIntentRequest>()) Registry.ctx().erase<game::FRunIntentRequest>();
        LOG_ERROR("The run-end screen has no run in the context; returning to the title.");
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    }
    auto* request = Registry.ctx().find<game::FRunIntentRequest>();
    if (request == nullptr) return {};
    const game::ERunIntent intent = request->intent;
    Registry.ctx().erase<game::FRunIntentRequest>();
    if (intent != game::ERunIntent::RetryAfterDefeat && intent != game::ERunIntent::LeaveRun) {
        LOG_ERROR("The run-end screen dropped an intent it does not own.");
        return {};
    }
    const auto result = game::ApplyRunIntent(Registry, intent);
    if (!result) {
        LOG_ERROR("The run-end screen could not apply its intent: {}", result.error());
        return {};
    }
    if (intent == game::ERunIntent::RetryAfterDefeat) {
        // A retry starts again from the checkpoint, not from depth 1.
        Registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{false});
    }
    return game::ToStateTransition(*result, kEmberRunEndId);
}

void FEmberRunEndState::Render(entt::registry& Registry)
{
    const auto* view = game::FindModalView(Registry, game::kRunModalRunEnd);
    if (view == nullptr) {
        LOG_WARN("The run-end view is not registered.");
        return;
    }
    view->render(Registry);
}
