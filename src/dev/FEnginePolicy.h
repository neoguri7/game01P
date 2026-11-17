#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

namespace game::dev {

/// WP-00d (dev only): the `equip=engine` / `reward=engine` rune policy of the run autopilot — a player-like
/// placement that deliberately completes engine pairs (`data/engine_ignition`) so wave QA can measure
/// ignition. It reads only what the player sees: owned runes (pool + fitted), the current loadout, the
/// engine table and the open offer — never future offers, RNG state or battle outcomes. Every legality
/// question goes through ValidateSkillSlot / ValidateDungeonRuneEquip; every change through
/// EquipDungeonRune / RemoveDungeonRune (so the run log's `loadout` lines and the save stay authoritative).
///
/// Definitions (review WP-00d P2), all evaluated on the run party (FDungeonRun::party):
///   HOST for engine E: a slot whose skill has `target == "enemy"` and `power > 0` (OnHit needs a hostile
///     power>0 hit), which is its member's FIRST slot with that skill id (CheckEngineIgnition's slot rule),
///     and for which ValidateSkillSlot accepts (current runes ∪ every missing rune of E) — sockets AND tags.
///   HALF-PAIR slot for E: holds >= 1 rune of E, misses >= 1, and is a host for E.
///   RESERVED runes of a slot: the union of the missing runes of every E the slot is a half-pair for. Rules 2
///     and 3 only place a rune where ValidateSkillSlot accepts (current ∪ {rune} ∪ reserved); rule 1 ignores
///     reservations (completing wins).
/// Walk order everywhere: member index -> slot index -> engine row order (as authored) -> card index.

/// Which rule chose a placement (logged as `rule=<name>`).
enum class EEngineRule : std::uint8_t {
    Complete,  // rule 1: completes a half-pair
    Start,     // rule 2: starts a pair on a host
    Fallback,  // rule 3: first legal socket that keeps every reservation
    Auto       // rule 3 last resort: plain first legal socket (may consume a reservation)
};

/// "complete" | "start" | "fallback" | "auto".
[[nodiscard]] std::string_view EngineRuleName(EEngineRule rule);

struct FEnginePlacement {
    std::size_t member{0};
    std::size_t slot{0};
    EEngineRule rule{EEngineRule::Auto};
    std::string engineId;  // the engine rule 1/2 served; empty for Fallback/Auto
};

/// Pure query (no state change): where `equip=engine` would socket `runeId` now. The rune need not be in the
/// pool (card scoring asks before the pick): legality is ValidateSkillSlot on (slot runes + runeId [+ the
/// slot's reserved runes]). The phase window (Reward/Camp) is NOT checked here — EquipRuneByEnginePolicy's
/// EquipDungeonRune does that. nullopt = no legal socket anywhere.
///   1 Complete: the first half-pair slot (for some E) whose missing set contains runeId and that accepts it.
///   2 Start: else, for the first E (row order) containing runeId whose OTHER runes are all unowned
///     (IsRuneOwned), the first host for E that accepts runeId now (ValidateSkillSlot(current ∪ {runeId}))
///     AND accepts (current ∪ missing(E) ∪ reserved).
///   3 Fallback: else the first slot accepting (current ∪ {runeId} ∪ reserved); Auto: else the first slot
///     accepting (current ∪ {runeId}).
[[nodiscard]] std::optional<FEnginePlacement> ChooseEnginePlacement(const entt::registry& registry,
                                                                   std::string_view runeId);

/// ScoreEngineCard's return values: 2 completes a pair (rule 1), 1 starts one (rule 2), 0 otherwise.
inline constexpr int kEngineCardScoreComplete = 2;
inline constexpr int kEngineCardScoreStart = 1;

/// `reward=engine` card score: 2 if ChooseEnginePlacement would use rule 1, 1 if rule 2, else 0. An owned rune
/// (IsRuneOwned — a duplicate card is a refund, not a rune) scores 0.
[[nodiscard]] int ScoreEngineCard(const entt::registry& registry, std::string_view runeId);

/// `equip=engine` for a picked rune: the first `run.runes` copy of runeId -> ChooseEnginePlacement ->
/// EquipDungeonRune. Exactly one line:
///   `[autopilot] equip rune=R member=M slot=S skill=K rule=<rule>[ engine=E]`, or
///   `[autopilot] equip rune=R none: no unfitted copy` / `none: no legal socket` / `none: <EquipDungeonRune error>`.
void EquipRuneByEnginePolicy(entt::registry& registry, std::string_view runeId);

/// Camp consolidation (review P4), one bounded pass:
///   (a) every pooled rune, in pool order (snapshot of ids), through EquipRuneByEnginePolicy;
///   (b) for each engine (row order) whose runes are all fitted but not in one slot: candidates are E's runes in
///       requires_runes order; moving rune r from its slot S to T (the slot holding E's other fitted rune(s)) is a
///       candidate when T is a host for E, ValidateSkillSlot(T ∪ {r}) passes, and r's removal breaks no completed
///       pair in S (c). The first candidate: RemoveDungeonRune(S, r) — a refusal (its revalidation, e.g. ember
///       still needs fire's tag) skips to the next candidate — then EquipDungeonRune(T, run.runes.size()-1); if
///       that equip fails, re-equip r into S so the loadout is unchanged.
///       One line per engine considered in (b): `[autopilot] camp move rune=R from=m/s to=m/s engine=E` or
///       `[autopilot] camp move none engine=E reason=<text>`.
/// Returns the number of moves made in (b).
int ConsolidateEnginesAtCamp(entt::registry& registry);

/// Per-engine state for the run-end line (review P7).
enum class EEngineArm : std::uint8_t {
    Armed,            // a host slot holds every rune of E (CheckEngineIgnition could fire on its hit)
    NotOffered,       // some rune of E is not owned
    PartnerUnfitted,  // all owned, but a rune is pooled, or a legal camp move would complete it
    MovedRefused,     // all fitted, split; every move toward a host is refused (removal revalidation or (c))
    NoHost            // all fitted; no slot can host the whole pair (sockets/tags/OnHit), incl. a pair
                      // completed in a non-host slot
};

/// "armed" | "not-offered" | "partner-unfitted" | "moved-refused" | "no-host".
[[nodiscard]] std::string_view EngineArmName(EEngineArm state);

struct FEngineArmStatus {
    std::string engineId;
    EEngineArm state{EEngineArm::NotOffered};
    std::size_t member{0};  // the host slot, valid when Armed
    std::size_t slot{0};
};

/// Read-only: every engine row in data order, classified from the run's current loadout and pool. Empty when
/// there is no run or no content.
[[nodiscard]] std::vector<FEngineArmStatus> ClassifyEngines(const entt::registry& registry);

} // namespace game::dev
