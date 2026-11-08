#include "states/FRunFlowStateMap.h"

#include <string>

#include "core/Logger.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberPreparationState.h"
#include "states/FEmberTitleState.h"

namespace game {

FStateTransition ToStateTransition(const FRunResult& result, std::string_view topId) {
    switch (result.exit) {
    case ERunExit::None:
        return {};
    case ERunExit::NextRoom:
        // A run step ends in the next battle: the battle is the root, so the whole modal stack unwinds.
        return {EStateChangeKind::Root, std::string(kEmberBattleId)};
    case ERunExit::LeaveRun:
    case ERunExit::ToTitle:
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    case ERunExit::ToPrepare:
        return {EStateChangeKind::Root, std::string(kEmberPrepareId)};
    case ERunExit::ShowModal:
        if (result.modalId.empty()) {
            LOG_ERROR("Run result asked for a modal without a modal id (top '{}').", topId);
            return {};
        }
        if (topId == kEmberBattleId) {
            // The battle stays under the modal: its screen stays visible, its simulation stops.
            return {EStateChangeKind::Push, result.modalId};
        }
        if (topId == kRunModalReward || topId == kRunModalEvent || topId == kRunModalCamp) {
            // Modal to modal: the stack depth stays at 2.
            return {EStateChangeKind::ReplaceTop, result.modalId};
        }
        LOG_ERROR("Run result asked for a modal from top id '{}', which is not a run host.", topId);
        return {};
    }
    LOG_ERROR("Run result has an unknown exit value.");
    return {};
}

} // namespace game
