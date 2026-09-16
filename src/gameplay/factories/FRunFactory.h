#pragma once

#include "gameplay/run/FRunState.h"

#include <entt/entt.hpp>

#include <string_view>

namespace game::gameplay {

struct FContentRegistry;

/// The only entity-creation site for run transitions (R5). A room *is* a battle, so this factory owns the
/// "destroy the finished one, then spawn the next one" sequence; the battle itself is still built by
/// FBattleFactory. why a second factory instead of folding this into FBattleFactory: the run transition is a
/// session-level decision (which dungeon, which room), while FBattleFactory knows only "an encounter becomes
/// entities" (R12).
struct FRunFactory {
    /// Starts `dungeonId` at room 0 and moves the run to InDungeon. False means the run was NOT entered: an unknown
    /// dungeon id, an empty room list, or a failed room spawn — the phase is left as it was (Hub), so a
    /// half-entered run cannot exist (R19).
    [[nodiscard]] static bool enterDungeon(entt::registry& registry,
                                           const FContentRegistry& content,
                                           FRunState& state,
                                           std::string_view dungeonId);

    /// Moves the run to the next room; past the last room it counts a cleared dungeon and returns to the hub.
    /// False means the next room could not be spawned — the run is put back in the hub instead of being stranded.
    [[nodiscard]] static bool advanceRoom(entt::registry& registry,
                                          const FContentRegistry& content,
                                          FRunState& state);

    /// Destroys the standing battle and puts the run back in the hub (던전 진행은 그 던전 처음부터, design §1 확정).
    static void returnToHub(entt::registry& registry, FRunState& state);
};

} // namespace game::gameplay
