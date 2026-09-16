#pragma once

#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>

#include <string_view>

namespace game::gameplay {

struct FContentRegistry;

/// The only entity-creation site for a battle (R5). Everything that wants units asks this factory, so a new
/// unit kind is a data row plus (at most) one line here, never a `create()` in a system.
struct FBattleFactory {
    /// Spawns the battle entity and every unit of `encounterId`, and fills `state` (grid size, battle entity).
    /// False means "nothing was spawned": an unknown encounter, a cell outside the grid, or two units stacked
    /// on one cell. The reason is logged (R19) — a half-spawned battle must not become playable.
    [[nodiscard]] static bool buildBattle(entt::registry& registry,
                                         const FContentRegistry& content,
                                         std::string_view encounterId,
                                         FBattleState& state);

    /// Structural changes on an already-spawned entity (R5: attaching or removing a component is an entity-structure
    /// change, not simulation logic). Keeping them here means "which components can a live entity gain or lose?" is
    /// answerable by reading this file instead of searching the systems — and a system that emplaced its own tag
    /// would put that answer in the simulation layer.
    static void openTurn(entt::registry& registry, entt::entity unit);
    static void closeTurn(entt::registry& registry, entt::entity unit);
    static void markDowned(entt::registry& registry, entt::entity unit);

    /// Terminal battle phase. why two functions instead of one phase-enum parameter: the phases are exclusive tags
    /// (R11) and two call sites do not justify an enum-to-tag switch in every reader (R8/R10).
    static void endBattleAsVictory(entt::registry& registry, entt::entity battle);
    static void endBattleAsDefeat(entt::registry& registry, entt::entity battle);
};

} // namespace game::gameplay
