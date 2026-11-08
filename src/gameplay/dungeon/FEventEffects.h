#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FEventContent.h"

namespace game::gameplay {
struct FUnitContent;
}

namespace game {

struct FRunMember;

/// The six Run-scope event effect kinds (WP-09 §2), registered through WP-04 RegisterEffect.
inline constexpr std::string_view kEventEffectRestorePartyHp = "restore_party_hp";  // amount
inline constexpr std::string_view kEventEffectDamageParty = "damage_party";          // amount
inline constexpr std::string_view kEventEffectDamagePartyPct = "damage_party_pct";   // amount = percent
inline constexpr std::string_view kEventEffectGainMaterials = "gain_materials";      // amount
inline constexpr std::string_view kEventEffectGainRuneRandom = "gain_rune_random";   // pool (must be empty in W9)
inline constexpr std::string_view kEventEffectGainNodePoints = "gain_node_points";   // member?, amount (0 = 1)

/// Registers the six kinds with FEffectScope::Run (applier + parameter validator). Call once under the
/// [WP-09] anchor; they never fire from a Battle hook (ApplyEffect refuses the scope mismatch).
void RegisterEventEffects(entt::registry& registry);

/// What one applied choice changed — for the result line and the log. Totals over the party.
struct FEventOutcome {
    int hpRestored{0};
    int hpLost{0};
    int materials{0};
    int nodePoints{0};
    std::vector<std::string> runes;  // rune ids granted (a duplicate refund adds materials instead)
};

/// Applies every effect of the choice through ApplyEffect (Run scope), in authored order, and returns the
/// summed outcome. nullopt (nothing applied) when there is no run. Does not check the gate, log, or save —
/// the run flow owns those (FRunFlow ChooseEventOption).
[[nodiscard]] std::optional<FEventOutcome> ApplyEventChoice(entt::registry& registry, const FEventChoice& choice);

/// "+3 materials, party HP -12" — empty when nothing changed.
[[nodiscard]] std::string DescribeEventOutcome(const FEventOutcome& outcome);

/// One unit template's base value of an event stat (maxHealth/speed are truncated to int). 0 for a stat
/// outside kEventCheckStats.
[[nodiscard]] int EventUnitBaseStat(const gameplay::FUnitContent& unit, std::string_view stat);

/// "health" -> "Max HP", "attack" -> "Attack", "magic_attack" -> "Magic Attack", "defense" -> "Defense",
/// "magic_defense" -> "Magic Defense", "speed" -> "Speed"; the raw id otherwise.
[[nodiscard]] std::string EventStatLabel(std::string_view stat);

/// The party-best stat (pure — no battle entity or FCombatSheet is guaranteed during an event): max over
/// run.party of base(ResolveRunMemberUnit(registry, member.id)) + DungeonMemberStatBonus(member, stat).
/// Ties keep party order. A member whose unit cannot be resolved is skipped (one warning per call).
struct FEventStatReading {
    bool resolved{false};    // false = no run, or no member resolved
    int value{0};
    std::string memberName;  // the best member's display name (id when the name is empty)
};
[[nodiscard]] FEventStatReading EventPartyStat(const entt::registry& registry, std::string_view stat);

/// Gate verdict for one choice. No check: open, empty reason. Otherwise open = best >= difficulty, and the
/// reason reads "Attack 9 >= 9 (Alden / Warrior)" when open, "Attack 8 < 9" when closed.
struct FEventGate {
    bool open{true};
    std::string reason;
};
[[nodiscard]] FEventGate EvaluateEventGate(const entt::registry& registry, const FEventChoice& choice);

/// Per-member HP loss of one damage effect — the same function the applier uses (displayed = applied):
///  - damage_party_pct: ChestHpLoss(DungeonMemberMaxHealth, DungeonEffectiveHealth, percent / 100.0)
///  - damage_party:     max(0, min(amount, DungeonEffectiveHealth - 1)) (HP 1 floor, never KO; an unknown
///                      maximum with the -1 sentinel loses 0)
/// 0 for any other kind.
[[nodiscard]] int EventEffectLossFor(const entt::registry& registry, const FRunMember& member,
                                     const gameplay::FEffectSpec& effect);

/// The choice's per-member loss preview, one entry per run.party member in party order (zeros included —
/// an honest 0). Empty when the choice has no damage effect (or there is no run).
struct FEventMemberLoss {
    std::string memberName;
    int loss{0};
};
[[nodiscard]] std::vector<FEventMemberLoss> PreviewEventChoiceLoss(const entt::registry& registry,
                                                                   const FEventChoice& choice);

} // namespace game
