#pragma once

#include <entt/entt.hpp>

#include <string>
#include <vector>

namespace game::gameplay {

/// Battle → text. This is the entire presentation layer of slice 2: an image/animation slice replaces this file
/// and the one overlay call site, leaving the simulation (systems, components, rules) untouched (R9).
/// why a snapshot function instead of per-panel functions: the overlay draws whatever lines it is handed, so the
/// text layout stays a gameplay-side concern and `src/debug/` never learns battle vocabulary (R26 boundary).
struct FBattleTextView {
    /// One frame's battle as player-readable lines: header, grid, unit legend, predicted turn order, the current
    /// command, and the tail of the battle log.
    [[nodiscard]] static std::vector<std::string> snapshot(entt::registry& registry);
};

} // namespace game::gameplay
