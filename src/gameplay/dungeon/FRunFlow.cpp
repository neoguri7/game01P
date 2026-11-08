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
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FEventEffects.h"
#include "gameplay/dungeon/FEventSection.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRoomTypeRegistry.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunSequence.h"

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
    // One restore rule: the sentinel skip and the clamp formula live in FEventEffects (WP-09h #6), shared
    // with the event restore_party_hp applier, so camp/room heals and shrine heals cannot drift apart.
    (void)RestorePartyHealth(registry, amount);
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
    // The offer is a decision, not decoration: the reward step does not move on until it is picked or skipped.
    EnsureRewardsForRun(registry);
    if (const auto* rewards = registry.ctx().find<FRewards>();
        rewards != nullptr && rewards->offer.active) {
        LOG_WARN("The rune offer is still open; pick or skip it before continuing.");
        return std::unexpected(std::string("Resolve the rune offer before continuing."));
    }
    // WP-08: the step is sequence data now — `room` is the battle ordinal, the gap event after it is the
    // event slot (if any), and the camp opens after the dungeon's last battle (never a room index).
    const auto* sequence = FindRunSequence(registry);
    if (sequence == nullptr) {
        LOG_ERROR("The dungeon sequence is missing; the reward step cannot advance (see InitializeDungeon).");
        return std::unexpected(std::string("The dungeon sequence is missing."));
    }
    const auto position = sequence->PositionOf(run->depth - 1);
    if (const auto* slot = sequence->GapEvent(position, run->room)) {
        // WP-09: the gap slot's type dispatches through the ctx room-type table; `event` is the only
        // registered type today. An unregistered type is a data/wiring error — never a crash: the phase
        // modal is the fallback.
        run->phase = EDungeonPhase::Event;
        const auto* roomType = FindRoomType(registry, slot->type);
        if (roomType == nullptr) {
            LOG_ERROR("The event slot type '{}' is not a registered room type; using the phase modal.", slot->type);
        }
        // The instance is derived from the pick (same seed/depth/room/seen = same event, so a resume
        // reopens the same screen) and stored before the save, so the flow and the view agree.
        auto instance = CurrentEventInstance(registry);
        if (instance) {
            if (const auto def = FindEventDef(registry, instance->eventId)) {
                run->message = def->body;
            } else {
                LOG_ERROR("The picked event '{}' is not in the catalog; the screen has no body text.",
                          instance->eventId);
            }
            registry.ctx().insert_or_assign(std::move(*instance));
            const std::string modalId =
                roomType != nullptr ? roomType->modalId : RunModalIdForPhase(EDungeonPhase::Event);
            return SaveAfterTransition(registry, FRunResult{ERunExit::ShowModal, modalId});
        }
        // An empty pick (empty pool) has nothing to click. Boot validation should have caught it
        // (ValidateEventContent), so this is a loud fallback: advance exactly like a gap without an event —
        // never strand the player on an empty modal.
        LOG_ERROR("The event room at depth {} room {} has no event to open; advancing past it.",
                  run->depth, run->room);
    }
    if (run->room + 1 < sequence->BattleCountAt(position)) {
        ++run->room;
        Advance(registry);
        return SaveAfterTransition(registry, FRunResult{ERunExit::NextRoom, {}});
    }
    run->phase = EDungeonPhase::Camp;
    run->cleared = run->depth;
    auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
    checkpoint.cleared = std::max(checkpoint.cleared, std::min(run->cleared, 999));
    (void)SaveCheckpoint(registry);
    RestoreParty(registry, DungeonRecovery(registry).campAutoHeal);
    run->message = "Dungeon cleared. Checkpoint reached. Rest, fit runes, reinforce stats, then descend.";
    // WP-10: the entry report (hoard_ratio = materials / cheapest-two) — a tuning metric, not a gate.
    LogCampEntryReport(registry);
    // The checkpoint/cleared update is complete; the event is appended before the save.
    AppendRunEvent(registry, ERunEventKind::DungeonCleared);
    return SaveAfterTransition(registry, FRunResult{ERunExit::ShowModal, RunModalIdForPhase(EDungeonPhase::Camp)});
}

std::expected<FRunResult, std::string> ChooseEventOption(entt::registry& registry, int choice) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Event) return WrongPhase(run->phase);
    const int depth = run->depth;
    const int room = run->room;
    // The ctx instance when present (ContinueReward emplaces the derived pick; the event section restores a
    // resolved one), otherwise the derivation for this frame. Stored right away so the continue step (and
    // the next frame's view) sees the instance the resolve writes to.
    auto* instance = registry.ctx().find<FEventInstance>();
    if (instance == nullptr) {
        auto derived = CurrentEventInstance(registry);
        if (!derived) {
            LOG_ERROR("The event room at depth {} room {} has no event to resolve.", depth, room);
            return std::unexpected(std::string("There is no event to resolve."));
        }
        instance = &registry.ctx().emplace<FEventInstance>(std::move(*derived));
    }
    if (instance->resolved) {
        // The double-payment guard: a resolved event never applies a second time across Continue/resume.
        return std::unexpected(std::string("The event is already resolved."));
    }
    const auto def = FindEventDef(registry, instance->eventId);
    if (!def) {
        LOG_ERROR("The event '{}' (depth {}, room {}) is not in the catalog.", instance->eventId, depth, room);
        return std::unexpected(std::string("The event is not in the catalog."));
    }
    if (choice < 0 || static_cast<std::size_t>(choice) >= def->choices.size()) {
        return std::unexpected(std::string("The event choice is out of range."));
    }
    const FEventChoice& option = def->choices[static_cast<std::size_t>(choice)];
    const auto gate = EvaluateEventGate(registry, option);
    if (!gate.open) {
        return std::unexpected(gate.reason.empty() ? std::string("The choice is not available.") : gate.reason);
    }
    const auto outcome = ApplyEventChoice(registry, option);
    if (!outcome) {
        LOG_ERROR("The event '{}' choice {} applied nothing.", def->id, choice);
        return std::unexpected(std::string("The event choice could not be applied."));
    }
    // WP-19a: the choice is recorded before the room advances, so the line names the event room.
    AppendRunEvent(registry, ERunEventKind::EventChosen, def->id, choice);
    MarkEventSeen(registry, def->id);
    // The resolve steps above may insert new ctx types (the context is a dense map): re-fetch instead of
    // reusing pointers taken before the insert.
    auto* instanceNow = registry.ctx().find<FEventInstance>();
    auto* runNow = registry.ctx().find<FDungeonRun>();
    if (instanceNow == nullptr || runNow == nullptr) return NoRun();
    instanceNow->resolved = true;
    instanceNow->choice = choice;
    const std::string summary = DescribeEventOutcome(*outcome);
    runNow->message = option.resultText;
    if (!summary.empty()) runNow->message += " (" + summary + ")";
    LOG_INFO("[event] {} choice {}: {}", def->id, choice, summary);
    // The phase stays Event (a core phase — save validation untouched): the view switches to the result
    // stage inside the same modal and ContinueEvent advances the room.
    return SaveAfterTransition(registry, FRunResult{ERunExit::None, {}});
}

std::expected<FRunResult, std::string> ContinueEvent(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Event) return WrongPhase(run->phase);
    // The result screen is only reachable through a resolve: without a resolved ctx instance there is
    // nothing to continue past (Change nothing).
    const auto* instance = registry.ctx().find<FEventInstance>();
    if (instance == nullptr || !instance->resolved) {
        return std::unexpected(std::string("Choose an option first."));
    }
    ++run->room;
    Advance(registry);
    registry.ctx().erase<FEventInstance>();  // seen survives (FEventRunState is kept apart by design)
    return SaveAfterTransition(registry, FRunResult{ERunExit::NextRoom, {}});
}

/// The legacy aliases (ChooseEventRest/ChooseEventLoot): option 0/1 resolved and continued in one step —
/// the pre-WP-09 behaviour ChooseDungeonEvent, the autopilot and the unedited tests expect.
std::expected<FRunResult, std::string> ChooseEventLegacy(entt::registry& registry, int choice) {
    const auto resolved = ChooseEventOption(registry, choice);
    if (!resolved) return resolved;
    return ContinueEvent(registry);
}

std::expected<FRunResult, std::string> LeaveCamp(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return NoRun();
    if (run->phase != EDungeonPhase::Camp) return WrongPhase(run->phase);
    // WP-10: the camp's rune-offer purchase opens an offer the same way a room reward does — descending
    // with it unresolved is refused, exactly like ContinueReward (the pick/skip owns the decision).
    EnsureRewardsForRun(registry);
    if (const auto* rewards = registry.ctx().find<FRewards>();
        rewards != nullptr && rewards->offer.active) {
        LOG_WARN("The rune offer is still open; pick or skip it before leaving camp.");
        return std::unexpected(std::string("Resolve the rune offer before leaving camp."));
    }
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

void AppendRunEvent(entt::registry& registry, ERunEventKind kind, std::string id, int amount, int member) {
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
    event.member = member;
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

void PostRunIntent(entt::registry& registry, ERunIntent intent, int choice) {
    if (auto* request = registry.ctx().find<FRunIntentRequest>()) {
        LOG_WARN("A run intent was already posted this frame; replacing it with the newest request.");
        request->intent = intent;
        request->choice = choice;
        return;
    }
    registry.ctx().emplace<FRunIntentRequest>(FRunIntentRequest{intent, choice});
}

void StartRun(entt::registry& registry, std::uint32_t seed, bool fresh) {
    // D-21: fresh = dungeon 1; otherwise the checkpoint's cleared index (the checkpoint never goes down).
    StartRunAt(registry, seed, fresh ? 0 : registry.ctx().get<FDungeonCheckpoint>().cleared);
}

void StartRunAt(entt::registry& registry, std::uint32_t seed, int clearedIndex) {
    const auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
    const int index = std::clamp(clearedIndex, 0, std::max(0, checkpoint.cleared));
    InstallDungeonRunAt(registry, seed, index);
    // A new expedition always starts with a clean offer slate — even when it reuses the previous seed, which
    // is why the reset is unconditional here and not left to the seed-mismatch check.
    ResetRewardsForRun(registry);
    // WP-09: same for the run-scope event memory — a new run has no seen ids and no half-resolved event.
    ResetEventsForRun(registry);
    // WP-08/D-7: the start-support rune offer is forced by seeding the pity streak right after the reset;
    // it survives the save through the existing `offers` section (no new save field). The materials half of
    // the support is installed with the run (InstallDungeonRunAt).
    if (index > 0) {
        const auto* sequence = FindRunSequence(registry);
        auto* rewards = registry.ctx().find<FRewards>();
        if (sequence != nullptr && rewards != nullptr && sequence->startSupport.runeOffers > 0) {
            if (rewards->tables.pityThreshold <= 0) {
                LOG_WARN("The start rune offer cannot be forced: rewards.pity_threshold is 0 (off).");
            } else {
                rewards->pity = rewards->tables.pityThreshold;  // the next monster-room offer is forced
            }
        }
    }
    if (!SaveDungeonRun(registry)) {
        LOG_ERROR("A new expedition could not be saved.");
    }
}

void NormalizeResumedRunPhase(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr || run->phase != EDungeonPhase::Event) return;
    const auto* sequence = FindRunSequence(registry);
    // A missing sequence is a wiring error (logged by the callers that need it); the resume then keeps its
    // event modal instead of inventing an advance rule.
    if (sequence == nullptr) return;
    const auto position = sequence->PositionOf(run->depth - 1);
    if (sequence->GapEvent(position, run->room) != nullptr) {
        // The event step still exists. A resolved instance restored from the event section already knows its
        // event — leave it so the result screen reopens (no second payment). An unresolved resume re-derives
        // the pick (same seed/depth/room/seen = same event) and stores it, so the choices screen and the
        // flow agree without waiting for a render.
        if (const auto* restored = registry.ctx().find<FEventInstance>()) {
            if (!restored->resolved || FindEventDef(registry, restored->eventId)) return;
            // Content changed under a result-screen save: the resolved event left the catalog, so the view has
            // no event to draw (and no Continue). The choice was already paid — never re-open choices; continue
            // past the room exactly as the player's Continue would.
            LOG_WARN("Saved event '{}' (depth {}, room {}) is no longer in the catalog; continuing past it.",
                     restored->eventId, run->depth, run->room);
            if (const auto result = ApplyRunIntent(registry, ERunIntent::ContinueEvent); !result) {
                LOG_WARN("The saved event room could not be continued past: {}", result.error());
                return;
            }
            // Same as the vanished-slot path below: the continue path enters ember.battle itself.
            if (auto* current = registry.ctx().find<FDungeonRun>()) current->advanceRequested = false;
            return;
        }
        auto instance = CurrentEventInstance(registry);
        if (instance) {
            registry.ctx().emplace<FEventInstance>(std::move(*instance));
            return;
        }
        LOG_ERROR("The saved event room at depth {} room {} has no event to reopen (empty pool).",
                  run->depth, run->room);
    } else {
        // Content changed under this save: the gap after this battle no longer carries an event. Never
        // replay the cleared battle — advance exactly like a reward step with no event (the offer was
        // already resolved before the event step was saved). A restored instance goes with the slot.
        LOG_WARN("Saved event room no longer exists in the dungeon sequence (depth {}, room {}); advancing past it.",
                 run->depth, run->room);
        registry.ctx().erase<FEventInstance>();
        // The erase can move the ctx map's storage (dense_map): re-fetch the run before writing to it.
        run = registry.ctx().find<FDungeonRun>();
        if (run == nullptr) return;
    }
    run->phase = EDungeonPhase::Reward;
    if (const auto result = ApplyRunIntent(registry, ERunIntent::ContinueReward); !result) {
        LOG_WARN("The saved event room could not be advanced past: {}", result.error());
        return;
    }
    // The continue path enters ember.battle itself (FEmberMenuRequest → kEmberBattleId): a pending Advance
    // would make the battle state root itself a second time, building the room twice and re-logging
    // dungeon_enter/room_start. The flag is transient (never written to the save), so clearing it in
    // memory is all that is needed for the resumed state to match the file.
    if (auto* current = registry.ctx().find<FDungeonRun>()) current->advanceRequested = false;
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
    // WP-17: the rune is no longer granted here — the room only decides whether to open an offer (the reward
    // modal's pick/skip owns the grant). WP-08: a boss room is a sequence slot (`boss` mid/final), never a
    // room index. Before the save, so a continued run comes back with the same offer.
    bool bossRoom = false;
    if (const auto* sequence = FindRunSequence(registry)) {
        bossRoom = sequence->IsBossBattle(sequence->PositionOf(run->depth - 1), run->room);
    } else {
        LOG_ERROR("The dungeon sequence is missing; the room cannot tell whether it is a boss room.");
    }
    RollRoomOffer(registry, bossRoom);
    LOG_INFO("Dungeon {} room {} cleared. {} EXP per member.", run->depth, run->room + 1, experienceReward);
    AppendRunEvent(registry, ERunEventKind::RoomCleared);  // before the save: a listener's change is saved too
    return SaveAfterTransition(registry, FRunResult{ERunExit::ShowModal, RunModalIdForPhase(EDungeonPhase::Reward)});
}

std::expected<FRunResult, std::string> ApplyRunIntent(entt::registry& registry, ERunIntent intent, int choice) {
    switch (intent) {
    case ERunIntent::ContinueReward: return ContinueReward(registry);
    case ERunIntent::ChooseEventRest: return ChooseEventLegacy(registry, 0);
    case ERunIntent::ChooseEventLoot: return ChooseEventLegacy(registry, 1);
    case ERunIntent::ChooseEventOption: return ChooseEventOption(registry, choice);
    case ERunIntent::ContinueEvent: return ContinueEvent(registry);
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
