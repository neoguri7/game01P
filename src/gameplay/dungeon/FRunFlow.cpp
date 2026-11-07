#include "gameplay/dungeon/FRunFlow.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "gameplay/dungeon/FRunEventLog.h"

namespace game {
namespace {

// ---------------------------------------------------------------------------------------------
// Run event log (WP-02a): the log lives in FRunEventLog.h; the append/register/shutdown functions
// live here because the ownership table gives FRunEventLog.h no translation unit of its own.
// ---------------------------------------------------------------------------------------------

bool SaveCheckpoint(entt::registry& registry) {
    auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
    if (checkpoint.path.empty()) return false;
    std::ofstream file(checkpoint.path, std::ios::trunc);
    file << nlohmann::json{{"schema_version", 1}, {"cleared", checkpoint.cleared}}.dump(2);
    file.flush();
    if (!file) {
        checkpoint.error = "Could not save dungeon checkpoint.";
        LOG_ERROR("{}", checkpoint.error);
        return false;
    }
    checkpoint.error.clear();
    return true;
}

void RestoreParty(entt::registry& registry, int amount) {
    auto& run = registry.ctx().get<FDungeonRun>();
    for (auto& member : run.party) {
        member.health += amount;
    }
}

// Callers save once their own state change is complete, so Advance does not.
void Advance(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    run.phase = EDungeonPhase::Battle;
    run.advanceRequested = true;
}

std::unexpected<std::string> NoRun() {
    return std::unexpected(std::string("There is no run."));
}

std::unexpected<std::string> WrongPhase(EDungeonPhase phase) {
    return std::unexpected(std::string("The run cannot apply that intent in its ") + PhaseId(phase) + " phase.");
}

/// ① run change → ② AppendRunEvent → ③ SaveDungeonRun. A save failure is returned, the phase stays changed.
std::expected<FRunResult, std::string> SaveAfterTransition(entt::registry& registry, FRunResult result) {
    if (!SaveDungeonRun(registry)) {
        const auto* save = registry.ctx().find<FDungeonSave>();
        const std::string reason =
            save != nullptr && !save->error.empty() ? save->error : "Could not save the expedition.";
        return std::unexpected(reason);
    }
    return result;
}

std::expected<FRunResult, std::string> ContinueReward(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Reward) return WrongPhase(run->phase);
    if (run->room == 0) {
        run->phase = EDungeonPhase::Event;
        run->message = "A quiet shrine offers shelter, or a sealed cache of reinforcement materials.";
        return SaveAfterTransition(registry, FRunResult{ERunExit::ShowModal, RunModalIdForPhase(EDungeonPhase::Event)});
    }
    if (run->room == 2) {
        run->phase = EDungeonPhase::Camp;
        run->cleared = run->depth;
        auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
        checkpoint.cleared = std::max(checkpoint.cleared, std::min(run->cleared, 999));
        (void)SaveCheckpoint(registry);
        RestoreParty(registry, DungeonBalance(registry, "camp_heal"));
        run->message = "Dungeon cleared. Checkpoint reached. Rest, fit runes, reinforce stats, then descend.";
        // The checkpoint/cleared update is complete; the event is appended before the save.
        AppendRunEvent(registry, ERunEventKind::DungeonCleared);
        return SaveAfterTransition(registry, FRunResult{ERunExit::ShowModal, RunModalIdForPhase(EDungeonPhase::Camp)});
    }
    ++run->room;
    Advance(registry);
    return SaveAfterTransition(registry, FRunResult{ERunExit::NextRoom, {}});
}

std::expected<FRunResult, std::string> ChooseEvent(entt::registry& registry, bool rest) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Event) return WrongPhase(run->phase);
    if (rest) RestoreParty(registry, DungeonBalance(registry, "event_heal"));
    else run->materials += DungeonBalance(registry, "event_materials");
    ++run->room;
    Advance(registry);
    return SaveAfterTransition(registry, FRunResult{ERunExit::NextRoom, {}});
}

std::expected<FRunResult, std::string> LeaveCamp(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Camp) return WrongPhase(run->phase);
    run->depth = std::min(run->depth + 1, 1000);
    run->room = 0;
    Advance(registry);
    return SaveAfterTransition(registry, FRunResult{ERunExit::NextRoom, {}});
}

std::expected<FRunResult, std::string> AbandonRun(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Battle && run->phase != EDungeonPhase::Camp) {
        return WrongPhase(run->phase);
    }
    AppendRunEvent(registry, ERunEventKind::RunEnded);
    DeleteDungeonSave(registry);
    registry.ctx().erase<FDungeonRun>();
    return FRunResult{ERunExit::ToTitle, {}};
}

std::expected<FRunResult, std::string> RetryAfterDefeat(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Defeat) return WrongPhase(run->phase);
    // The lost run is dropped; the prepare step (WP-02b) starts a new one from the checkpoint.
    registry.ctx().erase<FDungeonRun>();
    return FRunResult{ERunExit::ToPrepare, {}};
}

std::expected<FRunResult, std::string> LeaveRun(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase == EDungeonPhase::Defeat) {
        // A lost run's save is already gone; leaving only drops the run object.
        registry.ctx().erase<FDungeonRun>();
        return FRunResult{ERunExit::ToTitle, {}};
    }
    if (run->phase != EDungeonPhase::Battle) return WrongPhase(run->phase);
    if (!SaveDungeonRun(registry)) {
        const auto* save = registry.ctx().find<FDungeonSave>();
        const std::string reason =
            save != nullptr && !save->error.empty() ? save->error : "Could not save the expedition.";
        return std::unexpected(reason);  // the run stays in the registry and untouched
    }
    registry.ctx().erase<FDungeonRun>();
    return FRunResult{ERunExit::ToTitle, {}};
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Run event log
// ---------------------------------------------------------------------------------------------

void AppendRunEvent(entt::registry& registry, ERunEventKind kind, std::string id, int amount) {
    auto* listeners = registry.ctx().find<FRunEventListeners>();
    if (listeners != nullptr && listeners->delivering) {
        // Re-entrant append during delivery: a listener that appends would mutate the event stream the
        // listeners are currently reading. Drop it loudly rather than corrupting the order.
        LOG_ERROR("A run event was appended while a listener was delivering; the event is dropped.");
        return;
    }
    auto* log = registry.ctx().find<FRunEventLog>();
    if (log == nullptr) log = &registry.ctx().emplace<FRunEventLog>();
    FRunEvent event;
    event.kind = kind;
    event.seq = log->nextSeq++;
    event.id = std::move(id);
    event.amount = amount;
    if (const auto* run = registry.ctx().find<FDungeonRun>()) {
        event.depth = run->depth;
        event.room = run->room;
    }
    log->tail.push_back(event);
    if (log->tail.size() > kRunEventTail) log->tail.erase(log->tail.begin());
    if (listeners == nullptr || listeners->listeners.empty()) return;
    listeners->delivering = true;
    // Copy the list: a listener may register another listener; the delivery set stays stable.
    const auto targets = listeners->listeners;
    for (const auto& listener : targets) {
        try {
            listener(event);
        } catch (const std::exception& error) {
            LOG_ERROR("Run event listener threw: {}", error.what());
        } catch (...) {
            LOG_ERROR("Run event listener threw an unknown exception.");
        }
    }
    if (auto* current = registry.ctx().find<FRunEventListeners>()) current->delivering = false;
}

void RegisterRunEventListener(entt::registry& registry, std::function<void(const FRunEvent&)> listener) {
    auto* listeners = registry.ctx().find<FRunEventListeners>();
    if (listeners == nullptr) listeners = &registry.ctx().emplace<FRunEventListeners>();
    listeners->listeners.push_back(std::move(listener));
}

void ShutdownRunEventLog(entt::registry& registry) {
    registry.ctx().erase<FRunEventLog>();
    registry.ctx().erase<FRunEventListeners>();
}

// ---------------------------------------------------------------------------------------------
// Run flow
// ---------------------------------------------------------------------------------------------

void PostRunIntent(entt::registry& registry, ERunIntent intent) {
    if (auto* request = registry.ctx().find<FRunIntentRequest>()) {
        LOG_WARN("A run intent was already posted this frame; replacing it with the newest request.");
        request->intent = intent;
        return;
    }
    registry.ctx().emplace<FRunIntentRequest>(FRunIntentRequest{intent});
}

void StartRun(entt::registry& registry, std::uint32_t seed, bool fresh) {
    InstallDungeonRun(registry, seed, fresh);
    if (!SaveDungeonRun(registry)) {
        LOG_ERROR("A new expedition could not be saved.");
    }
}

std::expected<FRunResult, std::string> ResolveRoomVictory(entt::registry& registry, bool victory) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Battle) return WrongPhase(run->phase);
    CaptureRunParty(registry);
    if (!victory) {
        run->phase = EDungeonPhase::Defeat;
        run->message = "The expedition is lost. Run growth and runes reset on retry.";
        AppendRunEvent(registry, ERunEventKind::RunEnded);
        DeleteDungeonSave(registry);  // a lost run cannot be continued
        return FRunResult{ERunExit::ShowModal, RunModalIdForPhase(EDungeonPhase::Defeat)};
    }
    RestoreParty(registry, DungeonBalance(registry, "room_heal"));
    run->materials += DungeonBalance(registry, "room_materials");
    const auto& drops = DungeonDefinition(registry).at("rune_drops");
    run->runes.push_back(drops.at((run->seed + run->depth * 3u + run->room) % drops.size()).get<std::string>());
    // 정본 §6: 런 EXP는 캐릭터별로 쌓이고 레벨업마다 노드 포인트 1을 준다(런마다 0부터).
    const int experienceReward = DungeonBalance(registry, "room_experience");
    int levelsGained = 0;
    if (const auto* growth = registry.ctx().find<FDungeonGrowth>()) {
        for (auto& member : run->party) {
            member.experience += experienceReward;
            while (member.experience >= growth->ExperienceForNextLevel(member.level)) {
                member.experience -= growth->ExperienceForNextLevel(member.level);
                ++member.level;
                ++member.passivePoints;
                ++levelsGained;
            }
        }
    }
    run->phase = EDungeonPhase::Reward;
    run->message = "Room secured. Each member gains " + std::to_string(experienceReward) + " EXP.";
    if (levelsGained > 0) run->message += " Level up: allocate a passive node.";
    LOG_INFO("Dungeon {} room {} cleared. {} EXP per member.", run->depth, run->room + 1, experienceReward);
    AppendRunEvent(registry, ERunEventKind::RoomCleared);  // before the save: a listener's change is saved too
    return SaveAfterTransition(registry, FRunResult{ERunExit::ShowModal, RunModalIdForPhase(EDungeonPhase::Reward)});
}

std::expected<FRunResult, std::string> ApplyRunIntent(entt::registry& registry, ERunIntent intent) {
    switch (intent) {
    case ERunIntent::ContinueReward: return ContinueReward(registry);
    case ERunIntent::ChooseEventRest: return ChooseEvent(registry, true);
    case ERunIntent::ChooseEventLoot: return ChooseEvent(registry, false);
    case ERunIntent::LeaveCamp: return LeaveCamp(registry);
    case ERunIntent::AbandonRun: return AbandonRun(registry);
    case ERunIntent::RetryAfterDefeat: return RetryAfterDefeat(registry);
    case ERunIntent::LeaveRun: return LeaveRun(registry);
    }
    return std::unexpected("Unknown run intent.");
}

std::string RunModalIdForPhase(EDungeonPhase phase) {
    switch (phase) {
    case EDungeonPhase::Reward: return std::string(kRunModalReward);
    case EDungeonPhase::Event: return std::string(kRunModalEvent);
    case EDungeonPhase::Camp: return std::string(kRunModalCamp);
    case EDungeonPhase::Defeat: return std::string(kRunModalRunEnd);
    case EDungeonPhase::Battle: return {};
    }
    return {};
}

const char* PhaseId(EDungeonPhase phase) {
    switch (phase) {
    case EDungeonPhase::Battle: return "battle";
    case EDungeonPhase::Reward: return "reward";
    case EDungeonPhase::Event: return "event";
    case EDungeonPhase::Camp: return "camp";
    case EDungeonPhase::Defeat: return "defeat";
    }
    return "battle";
}

std::optional<EDungeonPhase> PhaseFromId(std::string_view id) {
    for (const auto phase :
         {EDungeonPhase::Battle, EDungeonPhase::Reward, EDungeonPhase::Event, EDungeonPhase::Camp, EDungeonPhase::Defeat}) {
        if (id == PhaseId(phase)) return phase;
    }
    return std::nullopt;
}

bool IsKnownPhaseId(std::string_view id) {
    return PhaseFromId(id).has_value();
}

} // namespace game
