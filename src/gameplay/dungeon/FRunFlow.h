#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FDungeonRun.h"

namespace game {

/// How a run-flow transition leaves the app layer. The rules never return an app-layer state type: they
/// return this neutral result and the app layer maps it to its own state stack (WP-02b).
enum class ERunExit {
    None,
    NextRoom,
    ShowModal,
    LeaveRun,
    ToPrepare,
    ToTitle
};

/// Modal ids a run phase opens. The app layer (WP-02b) reuses these constants for its catalog and its
/// state map; never re-spell the literal in another file.
inline constexpr std::string_view kRunModalReward = "ember.reward";
inline constexpr std::string_view kRunModalEvent = "ember.event";
inline constexpr std::string_view kRunModalCamp = "ember.camp";
inline constexpr std::string_view kRunModalRunEnd = "ember.runEnd";

struct FRunResult {
    ERunExit exit{ERunExit::None};
    std::string modalId;  // ShowModal only: one of the kRunModal* constants
};

/// The intent a run view (or a state) asks the run flow to apply. The views post it; the app layer's
/// modal host consumes it and applies it exactly once (WP-02b).
///
/// The event room is two-stage (WP-09 §4): `ChooseEventOption` resolves the choice carried in the request
/// payload and stays on the result screen; `ContinueEvent` closes it and advances the room.
/// `ChooseEventRest`/`ChooseEventLoot` are the legacy aliases — option 0/1 plus the continue, in one step
/// (the pre-WP-09 behaviour every unedited caller relies on).
enum class ERunIntent {
    ContinueReward,
    ChooseEventRest,
    ChooseEventLoot,
    ChooseEventOption,
    ContinueEvent,
    LeaveCamp,
    AbandonRun,
    RetryAfterDefeat,
    LeaveRun,
    TakeFork,
    ReturnInTriumph  // WP-46: end the run as a success at a theme-cleared camp
};

/// The one-frame request in the context. Only one may be pending; the newest post wins.
struct FRunIntentRequest {
    ERunIntent intent{ERunIntent::ContinueReward};
    int choice{-1};  // the event option index for ChooseEventOption; -1 = none
};

/// Posts a run intent. A second post in the same frame logs a warning and replaces the first request.
void PostRunIntent(entt::registry& registry, ERunIntent intent, int choice = -1);

/// Installs a new run (seed, depth from the checkpoint, party) and saves it. The only new-run entry point;
/// the legacy StartDungeonRun delegates here. The run creation itself stays in FDungeonRun.cpp.
void StartRun(entt::registry& registry, std::uint32_t seed, bool fresh);

/// Applies one intent to the run's current phase. A wrong-phase intent returns an error and changes
/// nothing — no state, no save, no event. `choice` carries the event option index (ChooseEventOption).
std::expected<FRunResult, std::string> ApplyRunIntent(entt::registry& registry, ERunIntent intent, int choice = -1);

/// Resolves a finished battle. A victory captures the party, advances the room rewards (heal, materials,
/// rune, EXP) and sets phase=Reward. A defeat loses the run: phase=Defeat and the save is deleted.
std::expected<FRunResult, std::string> ResolveRoomVictory(entt::registry& registry, bool victory);

/// True when a run exists, its phase is Camp, a run sequence exists, and the dungeon just cleared
/// (PositionOf(run.depth - 1)) is the last dungeon of its theme.
[[nodiscard]] bool IsThemeClearedCamp(const entt::registry& registry);
/// "<theme name> <dungeonInTheme + 1>/<dungeons in theme>", plus "  |  Cycle <cycle>" when cycle >= 1.
/// `dungeonIndex` is 0-based over the whole run (depth - 1). "" without a run sequence.
[[nodiscard]] std::string RunPlaceLabel(const entt::registry& registry, int dungeonIndex);

/// The modal id a run phase opens; empty for a phase with no modal.
[[nodiscard]] std::string RunModalIdForPhase(EDungeonPhase phase);

/// The save phase id of a core phase. It returns exactly the same strings as DungeonPhaseName for the five
/// core phases (SavedPhaseNamesUnchanged).
[[nodiscard]] const char* PhaseId(EDungeonPhase phase);

/// The core phase for a save id, or nullopt when the id is not a core phase.
[[nodiscard]] std::optional<EDungeonPhase> PhaseFromId(std::string_view id);

/// True for the five core phase ids. Room types are a different vocabulary — they live in
/// FRoomTypeRegistry.h and are never save phase ids.
[[nodiscard]] bool IsKnownPhaseId(std::string_view id);

} // namespace game
