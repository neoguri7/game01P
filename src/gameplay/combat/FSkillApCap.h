#pragma once

#include <optional>

namespace game {

struct FAssetManager;

namespace gameplay {

struct FContentRegistry;

/// WP-16 (30-decisions D-1): the default skill AP ceiling — kept at 2 until the D-1 indicator (skills per
/// round, room battle time) says otherwise. The authored `skill_ap_cap` balance key overrides it.
inline constexpr int kDefaultSkillApCap = 2;

/// WP-16: collision damage of a push that could not travel its full distance. Authored as
/// `push_collision_damage`; the default is 1 damage, no stun.
inline constexpr int kDefaultPushCollisionDamage = 1;

/// The tempo knobs of `data/dungeon`'s balance block (WP-16). Both keys are optional: an absent key keeps
/// its default, so flipping the D-1 decision is a one-line data edit with no code change.
struct FTempoTuning {
    int skillApCap{kDefaultSkillApCap};
    int pushCollisionDamage{kDefaultPushCollisionDamage};
};

/// Reads `skill_ap_cap` and `push_collision_damage` from `data/dungeon` balance. A missing document or key
/// keeps the defaults; nullopt + LOG_ERROR only when a PRESENT value is not an integer inside its bound
/// (`skill_ap_cap >= 1`, `push_collision_damage >= 0`) — the callers stop the boot on that (R19).
[[nodiscard]] std::optional<FTempoTuning> LoadTempoTuning(const FAssetManager& assets);

/// D-1 gate, file-free half: `cap` must be at least 1 and every authored skill must cost at most it.
/// One LOG_ERROR per offending skill; false means the content cannot boot under this cap.
[[nodiscard]] bool ValidateSkillApCap(int cap, const FContentRegistry& content);

/// Boot wrapper: load the tuning and check every authored skill against its cap. False = boot fails.
[[nodiscard]] bool ValidateSkillApCap(const FAssetManager& assets, const FContentRegistry& content);

} // namespace gameplay
} // namespace game
