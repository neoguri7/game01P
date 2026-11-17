#include "gameplay/combat/FBattleCommand.h"

#include <utility>

#include "gameplay/combat/FBattleFlow.h"

namespace game {

std::uint64_t CommandSessionId(const FBattleAction& action) {
    return std::visit([](const auto& typed) -> std::uint64_t { return typed.token.sessionId; }, action);
}

void ResetCommandResultsForSession(FCommandResults& results, std::uint64_t sessionId) {
    if (results.sessionId == sessionId) {
        return;
    }
    results.sessionId = sessionId;
    results.nextSeq = 1;
    results.recent.clear();
}

void RecordCommandResult(FCommandResults& results, std::uint64_t sessionId, FCommandResult result) {
    ResetCommandResultsForSession(results, sessionId);
    results.recent.push_back(std::move(result));
    if (results.recent.size() > kMaxRecentCommandResults) {
        const std::size_t excess = results.recent.size() - kMaxRecentCommandResults;
        results.recent.erase(results.recent.begin(), results.recent.begin() + static_cast<std::ptrdiff_t>(excess));
    }
}

bool SubmitBattleCommand(entt::registry& registry, FBattleCommand command) {
    auto* queue = registry.ctx().find<FActionQueue>();
    if (queue == nullptr) {
        queue = &registry.ctx().emplace<FActionQueue>();
    }
    auto* results = registry.ctx().find<FCommandResults>();
    if (results == nullptr) {
        results = &registry.ctx().emplace<FCommandResults>();
    }
    auto* outbox = registry.ctx().find<FCommandOutbox>();
    if (outbox == nullptr) {
        outbox = &registry.ctx().emplace<FCommandOutbox>();
    }

    const std::uint64_t sessionId = CommandSessionId(command.action);
    if (results->sessionId != sessionId) {
        // New battle: the queue itself is re-emplaced per battle by the battle state, but this outbox lives
        // outside it — its meta describes a queue that no longer exists, so it dies with the old record.
        outbox->meta.clear();
    }
    ResetCommandResultsForSession(*results, sessionId);

    if (const auto* outcome = registry.ctx().find<FBattleOutcome>(); outcome != nullptr && outcome->resolved) {
        RecordCommandResult(*results, sessionId,
                            FCommandResult{command, ECommandReject::OutcomeResolved, kOutcomeResolvedReason});
        return false;
    }
    if (!IsPresentationSettled(registry)) {
        if (auto* log = registry.ctx().find<FCombatLog>(); log != nullptr) {
            log->push(kPresentationBusyReason);
        }
        RecordCommandResult(*results, sessionId,
                            FCommandResult{command, ECommandReject::PresentationBusy, kPresentationBusyReason});
        return false;
    }

    command.seq = results->nextSeq++;
    queue->pending.push_back(std::move(command.action));
    outbox->meta.push_back(FCommandMeta{command.source, command.seq});
    return true;
}

} // namespace game
