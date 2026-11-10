#include "states/FEmberModalHostState.h"

#include <utility>

#include "core/InputState.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FModalViewRegistry.h"
#include "states/FEmberTitleState.h"
#include "states/FRunFlowStateMap.h"

namespace {

/// The intent a run phase's view is allowed to post. A mismatch is a wiring bug, not a player action:
/// log it and drop it so the host stays in its own phase.
bool AcceptsIntent(std::string_view id, game::ERunIntent intent) {
    if (id == game::kRunModalReward) {
        return intent == game::ERunIntent::ContinueReward || intent == game::ERunIntent::TakeFork;
    }
    if (id == game::kRunModalEvent) {
        return intent == game::ERunIntent::ChooseEventRest || intent == game::ERunIntent::ChooseEventLoot ||
               intent == game::ERunIntent::ChooseEventOption || intent == game::ERunIntent::ContinueEvent ||
               intent == game::ERunIntent::TakeFork;
    }
    if (id == game::kRunModalCamp) {
        return intent == game::ERunIntent::LeaveCamp || intent == game::ERunIntent::AbandonRun;
    }
    return false;
}

} // namespace

FEmberModalHostState::FEmberModalHostState(std::string id, EHostMode mode)
    : id(std::move(id)), mode(mode), name("Modal " + this->id) {}

FStateTransition FEmberModalHostState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (mode == EHostMode::Menu) {
        if (game::TakeModalViewCloseRequest(Registry)) return {EStateChangeKind::Pop, {}};
        if (const auto* input = Registry.ctx().find<game::FInputState>();
            input && input->isActionPressed(game::EInputAction::Cancel)) {
            return {EStateChangeKind::Pop, {}};
        }
        if (game::FindModalView(Registry, id) == nullptr) {
            // settings/compendium have no view until WP-24a/24b. Never trap the player in a blank modal.
            LOG_WARN("Modal '{}' has no registered view; closing it.", id);
            return {EStateChangeKind::Pop, {}};
        }
        return {};
    }

    if (!Registry.ctx().contains<game::FDungeonRun>()) {
        if (auto* stale = Registry.ctx().find<game::FRunIntentRequest>()) Registry.ctx().erase<game::FRunIntentRequest>();
        LOG_ERROR("Run modal '{}' has no run in the context; returning to the title.", id);
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    }
    auto* request = Registry.ctx().find<game::FRunIntentRequest>();
    if (request == nullptr) return {};
    const game::ERunIntent intent = request->intent;
    const int choice = request->choice;
    Registry.ctx().erase<game::FRunIntentRequest>();
    if (!AcceptsIntent(id, intent)) {
        LOG_ERROR("Run modal '{}' dropped an intent it does not own.", id);
        return {};
    }
    const auto result = game::ApplyRunIntent(Registry, intent, choice);
    if (!result) {
        LOG_ERROR("Run modal '{}' could not apply its intent: {}", id, result.error());
        return {};
    }
    return game::ToStateTransition(*result, id);
}

void FEmberModalHostState::Render(entt::registry& Registry)
{
    const auto* view = game::FindModalView(Registry, id);
    if (view == nullptr) {
        LOG_WARN("Modal '{}' has no view to render.", id);
        return;
    }
    view->render(Registry);
}
