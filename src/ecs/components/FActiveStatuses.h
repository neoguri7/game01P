#pragma once

#include <string>
#include <vector>

namespace game {

/// One active status instance on a unit. `magnitude` is the effective value at application time
/// (definition magnitude + rune deltas, docs/design/skill-rune-progression.md §3.3): dot damage per
/// turn, control action-fail chance (%), stat attack delta. 중첩 = 갱신 — re-applying refreshes the
/// duration and takes the latest magnitude instead of adding a second entry, so one status id appears
/// at most once per unit (§4).
struct FStatusEffect {
    std::string statusId;
    int turnsRemaining{0};
    int magnitude{0};
    /// WP-13/D-12: an `on_apply` control success stores the not-yet-spent one-turn action loss here. The
    /// owner's turn-start tick consumes it (no second roll); re-applying before it is spent changes
    /// nothing, and a fresh application after the spend rolls anew.
    bool skipPending{false};
};

struct FActiveStatuses {
    std::vector<FStatusEffect> effects;
    /// WP-15: status ids this unit currently ignores — the boss body's active phase `body_immunities`
    /// (written by FBattleSetup for the initial phase and by EvaluateBossGoals on every transition). Read
    /// through IsStatusImmune (FStatusRules), the single choke point every application path goes through.
    /// Unit-level immunity fields (units.json) are a later work package (WP-14/23).
    std::vector<std::string> immune;
};

} // namespace game
