#include "gameplay/ai/FAiBrain.h"

#include "core/Logger.h"
#include "gameplay/ai/FAiWorld.h"
#include "gameplay/combat/FCombatQuery.h"

namespace game::ai {
namespace {

constexpr int kMaxPassesPerDecision = 3;

} // namespace

FAiBrain MakeBrain(const FAiProfile& profile) {
    return FAiBrain{profile.tree, profile.tuning, profile.tree->makeMemory(), FAiBlackboard{}, profile.id};
}

std::optional<FBattleAction> RunBrain(
    entt::registry& registry,
    FTurnToken token,
    bool hostilePartySide,
    const gameplay::FContentRegistry& content) {
    const entt::entity actor = token.actor;
    if (!IsAlive(registry, actor)) {
        return std::nullopt;
    }
    FAiBrain* brain = registry.try_get<FAiBrain>(actor);
    if (brain == nullptr) {
        const auto* library = registry.ctx().find<FAiLibrary>();
        if (library == nullptr) {
            // A missing service is a wiring bug, not something to paper over with a default (R19): say so, once,
            // because the symptom is a battle in which nobody acts.
            static bool reported = false;
            if (!reported) {
                reported = true;
                LOG_ERROR("ai: {} has no behavior tree and the registry has no FAiLibrary; the unit will not act. "
                          "Create gameplay services (FGameplayServices::Initialize) before fighting.",
                          UnitDisplayName(registry, actor));
            }
            return std::nullopt;
        }
        brain = &registry.emplace<FAiBrain>(actor, MakeBrain(library->fallback()));
    }

    const FAiWorld world = BuildAiWorld(registry, actor, content);
    FAiBlackboard& blackboard = brain->blackboard;
    FBtContext ctx{registry, content, brain->tuning, world, blackboard, brain->memory, actor, token, hostilePartySide};

    if (!blackboard.started || blackboard.lastTurnSerial != token.turnSerial) {
        // A new turn of this actor: whatever the tree was in the middle of belongs to the previous turn.
        if (blackboard.started) {
            brain->tree->abort(ctx);
        }
        brain->memory.reset();
        blackboard.started = true;
        blackboard.lastTurnSerial = token.turnSerial;
        ++blackboard.turnsTaken;
        blackboard.pending = FAiPendingAction{};
        blackboard.rejected.clear();
    }
    blackboard.decision.reset();
    blackboard.reason.clear();

    // A tree that finishes a plan without issuing anything (the last action just took effect) gets another
    // pass from the root, so leftover AP is spent in the same decision instead of being thrown away.
    EBtStatus status = EBtStatus::Invalid;
    for (int pass = 0; pass < kMaxPassesPerDecision; ++pass) {
        status = brain->tree->tick(ctx);
        if (blackboard.decision || status != EBtStatus::Success) {
            break;
        }
    }
    if (blackboard.decision) {
        LOG_DEBUG("[ai] {} ({}) turn {}: {}", UnitDisplayName(registry, actor), brain->profileId,
                  blackboard.turnsTaken, blackboard.reason);
    } else {
        LOG_DEBUG("[ai] {} ({}) turn {}: nothing to do ({})", UnitDisplayName(registry, actor), brain->profileId,
                  blackboard.turnsTaken, BtStatusName(status));
    }
    return blackboard.decision;
}

} // namespace game::ai
