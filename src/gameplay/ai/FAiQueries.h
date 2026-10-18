#pragma once

#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/ai/FAiWorld.h"
#include "gameplay/ai/FBtNode.h"
#include "gameplay/ai/FSkillRole.h"

namespace game::ai {

/// Which of the actor's skills a leaf may consider. Authored in ai.json, so every field has a neutral default.
struct FSkillFilter {
    FRoleMask roles{kRolesAny};
    std::vector<std::string> skillIds; // empty = any skill
    std::vector<std::string> tags;     // empty = any skill; otherwise the skill must carry at least one
    bool selfOnly{false};              // friendly skills may only be aimed at the caster
    int minHits{1};                    // an area skill must catch at least this many units
};

/// One concrete way to use one skill from one standing cell, valued in expected HP swing.
struct FSkillOption {
    int slot{-1};
    entt::entity target{entt::null}; // null for cell-targeted skills
    FGridPosition cell{};
    int apCost{1};
    int value{0};
    int hits{0};
    bool kills{false};
    ESkillRole role{ESkillRole::Utility};
    std::string key; // identifies the attempt: skill id + target cell
};

/// Value of one status application on one unit, in HP-equivalents. It follows what ApplyStatus would really
/// store (a debuff the rules clamp to nothing is worth nothing) and counts only the turns the application
/// adds on top of what the unit already carries.
[[nodiscard]] int ValueOfStatus(
    const FBtContext& ctx, const FResolvedStatus& status, const FUnitView& target, bool targetIsAlly);

/// The best option available from `origin`, ranked by value per skill AP (the AP pool is the budget), then
/// raw value, then loadout order. `requireAp` = false answers the geometric question "could I act from
/// there at all" regardless of the AP left.
[[nodiscard]] std::optional<FSkillOption> FindBestOption(
    const FBtContext& ctx, const FGridPosition& origin, const FSkillFilter& filter, bool requireAp);

/// Longest range among the actor's skills that pass the filter; 0 when none does.
[[nodiscard]] int FilterRange(const FBtContext& ctx, const FSkillFilter& filter);

/// Expected damage the actor would take next turn if it ended its move on `cell`: the sum of the best hit of
/// every hostile unit that could reach the cell (its move plus its longest damage range).
[[nodiscard]] int DangerAt(const FBtContext& ctx, const FGridPosition& cell);

/// Manhattan distance from `cell` to the closest hostile unit; a large number when none is alive.
[[nodiscard]] int NearestHostileDistance(const FBtContext& ctx, const FGridPosition& cell);

/// Steps from every cell to the nearest cell the actor could act from (walls block, units do not: a blocker
/// usually moves). kUnreachable marks cells with no route.
inline constexpr int kUnreachable = 1000;
[[nodiscard]] std::vector<int> ComputeFiringField(const FBtContext& ctx, const FSkillFilter& filter);

/// HP lost to the spikes along the route the resolver would walk to `destination` (the actor's own cell = 0).
[[nodiscard]] int TrapDamageOnRoute(const FBtContext& ctx, const FGridPosition& destination);

} // namespace game::ai
