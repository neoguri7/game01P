#include "gameplay/dungeon/FEventEffects.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Logger.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/data/FEffectContent.h"
#include "gameplay/data/FUnitContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"

namespace game {
namespace {

/// The logged provenance of every event effect application (FEffectContext::source).
constexpr const char* kEventEffectSource = "event";

/// Validator bounds (WP-09 §2; the WP-09h #9 ceiling is the public kEventEffectAmountMax). Named so no
/// bound is a magic number: the flat effects need a positive amount up to the ceiling (an in-code overflow
/// guard, not balance), the chest-style cost a whole percent in [1, 99] (100 is not a trade — it floors
/// the party), and node points a non-negative count (0 reads as the default of 1) up to the same ceiling.
constexpr int kEventEffectAmountMin = 1;
constexpr int kEventDamagePercentMax = 99;
constexpr int kEventNodePointsMin = 0;
constexpr int kEventNodePointsDefault = 1;

/// The party never drops below 1 HP outside battle (F2): the floor every event damage effect respects.
constexpr int kEventDamageHpFloor = 1;

/// Percent arithmetic: the authored `damage_party_pct` amount is a whole percent.
constexpr double kEventPercentScale = 100.0;

/// The gate stat vocabulary and its display labels (WP-09 §5). The ids are the six kEventCheckStats
/// entries; EventUnitBaseStat reads the matching FUnitContent fields.
struct FEventStatName {
    std::string_view id;
    std::string_view label;
};

constexpr FEventStatName kEventStatNames[] = {
    {"health", "Max HP"},
    {"attack", "Attack"},
    {"magic_attack", "Magic Attack"},
    {"defense", "Defense"},
    {"magic_defense", "Magic Defense"},
    {"speed", "Speed"},
};

bool IsEventStatKnown(std::string_view stat) {
    return std::ranges::any_of(kEventStatNames, [stat](const FEventStatName& name) { return name.id == stat; });
}

/// The outcome accumulator ApplyEventChoice owns for the duration of one choice. The appliers add to it
/// only when it exists, so an applier reached outside ApplyEventChoice still changes the run but reports
/// nothing.
struct FEventOutcomeAccumulator {
    FEventOutcome outcome;
};

FEventOutcome* EventOutcomeOf(entt::registry& registry) {
    auto* accumulator = registry.ctx().find<FEventOutcomeAccumulator>();
    return accumulator != nullptr ? &accumulator->outcome : nullptr;
}

/// One damage effect's loss against an explicit effective health. EventEffectLossFor runs the same formula
/// against the member's current state and PreviewEventChoiceLoss against the health left by earlier
/// effects — one function, so the displayed loss is the applied loss.
int EventDamageLoss(const entt::registry& registry, const FRunMember& member,
                    const gameplay::FEffectSpec& effect, int effectiveHealth) {
    if (effect.kind == kEventEffectDamageParty) {
        // HP 1 floor, never KO. An unknown maximum with the -1 sentinel falls through
        // DungeonEffectiveHealth as effective 0 and loses 0.
        return std::max(0, std::min(effect.amount, effectiveHealth - kEventDamageHpFloor));
    }
    if (effect.kind == kEventEffectDamagePartyPct) {
        return ChestHpLoss(DungeonMemberMaxHealth(registry, member), effectiveHealth,
                           static_cast<double>(effect.amount) / kEventPercentScale);
    }
    return 0;  // not a damage effect
}

bool IsEventDamageKind(std::string_view kind) {
    return kind == kEventEffectDamageParty || kind == kEventEffectDamagePartyPct;
}

/// The health after one restore_party_hp effect — the single restore formula the applier and the preview
/// share (no second copy of the clamp rule): a known maximum clamps the gain, an unknown one does not. The
/// -1 sentinel (unclaimed = full) is the caller's concern, because materialising it is a damage effect's job.
int RestoredHealth(const entt::registry& registry, const FRunMember& member, int amount, int health) {
    const int maxHealth = DungeonMemberMaxHealth(registry, member);
    return maxHealth > 0 ? std::min(health + amount, maxHealth) : health + amount;
}

// --- appliers (Run scope) -----------------------------------------------------------------------

void ApplyRestorePartyHp(entt::registry& registry, const FEffectContext&, const gameplay::FEffectSpec& spec) {
    // One rule for the event applier and the camp/room restore (WP-09h #6): RestorePartyHealth owns the
    // sentinel skip and the clamp formula, so the two paths cannot drift apart.
    const int restored = RestorePartyHealth(registry, spec.amount);
    if (auto* outcome = EventOutcomeOf(registry)) {
        outcome->hpRestored += restored;
    }
}

void ApplyDamageParty(entt::registry& registry, const FEffectContext&, const gameplay::FEffectSpec& spec) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return;
    int lost = 0;
    for (auto& member : run->party) {
        const int loss = EventEffectLossFor(registry, member, spec);
        if (loss <= 0) continue;
        // Materialises the -1 sentinel exactly like ApplyChestHpCost: the pre-loss effective health is the
        // value this loss was computed against.
        member.health = DungeonEffectiveHealth(registry, member) - loss;
        lost += loss;
    }
    if (auto* outcome = EventOutcomeOf(registry)) {
        outcome->hpLost += lost;
    }
}

void ApplyGainMaterials(entt::registry& registry, const FEffectContext&, const gameplay::FEffectSpec& spec) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return;
    run->materials += spec.amount;
    if (auto* outcome = EventOutcomeOf(registry)) {
        outcome->materials += spec.amount;
    }
}

void ApplyGainRuneRandom(entt::registry& registry, const FEffectContext&, const gameplay::FEffectSpec& spec) {
    (void)spec;  // WP-23 widens `pool`; in W9 the validator accepts only the empty pool
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return;
    // The WP-17 draw, never a second rune-pool implementation. The first candidate is granted; an owned
    // rune follows the reward module's duplicate policy instead of duplicating.
    const std::vector<std::string> candidates = RollRuneCandidates(registry);
    if (candidates.empty()) {
        LOG_WARN("Event gain_rune_random: the rune draw came up empty; nothing was granted.");
        return;
    }
    const std::string& runeId = candidates.front();
    const auto* rewards = registry.ctx().find<FRewards>();
    auto* outcome = EventOutcomeOf(registry);
    const bool refundOwned = rewards != nullptr && rewards->tables.duplicatePolicy == kRewardDuplicatePolicyRefund &&
                             IsRuneOwned(registry, runeId);
    if (refundOwned) {
        const int refund = rewards->tables.duplicateRefundMaterials;
        run->materials += refund;
        if (outcome != nullptr) outcome->materials += refund;
        LOG_INFO("Event rune '{}' is already owned: +{} material(s).", runeId, refund);
        return;
    }
    run->runes.push_back(runeId);
    if (outcome != nullptr) outcome->runes.push_back(runeId);
    LOG_INFO("Event rune '{}' collected.", runeId);
}

void ApplyGainNodePoints(entt::registry& registry, const FEffectContext&, const gameplay::FEffectSpec& spec) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return;
    const int points = spec.amount != 0 ? spec.amount : kEventNodePointsDefault;  // 0 reads as 1
    auto* outcome = EventOutcomeOf(registry);
    if (spec.member.empty()) {  // no member = every party member
        for (auto& member : run->party) {
            member.passivePoints += points;
            if (outcome != nullptr) outcome->nodePoints += points;
        }
        return;
    }
    const auto member = std::ranges::find(run->party, spec.member, &FRunMember::id);
    if (member == run->party.end()) {
        LOG_WARN("Event gain_node_points names an unknown member '{}'; nothing was granted.", spec.member);
        return;
    }
    member->passivePoints += points;
    if (outcome != nullptr) outcome->nodePoints += points;
}

// --- validators ---------------------------------------------------------------------------------

bool ValidatePositiveAmount(const gameplay::FEffectSpec& spec) {
    return spec.amount >= kEventEffectAmountMin && spec.amount <= kEventEffectAmountMax;
}

bool ValidateDamagePercent(const gameplay::FEffectSpec& spec) {
    return spec.amount >= kEventEffectAmountMin && spec.amount <= kEventDamagePercentMax;
}

bool ValidateEmptyRunePool(const gameplay::FEffectSpec& spec) {
    // No rune pools exist in W9 (the draw reuses the WP-17 reward tables); WP-23 widens this to real pools.
    return spec.pool.empty();
}

bool ValidateNonNegativeAmount(const gameplay::FEffectSpec& spec) {
    return spec.amount >= kEventNodePointsMin && spec.amount <= kEventEffectAmountMax;
}

} // namespace

int RestorePartyHealth(entt::registry& registry, int amount) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return 0;  // nothing to restore
    int restored = 0;
    for (auto& member : run->party) {
        // -1 = unclaimed = already full: a heal never materialises the sentinel. Otherwise the run HP is
        // clamped to the member's derived max (unknown max <= 0: add without clamping — never clamp
        // toward 0).
        if (member.health < 0) continue;
        const int before = member.health;
        member.health = RestoredHealth(registry, member, amount, member.health);
        restored += std::max(0, member.health - before);
    }
    return restored;
}

void RegisterEventEffects(entt::registry& registry) {
    RegisterEffect(registry, kEventEffectRestorePartyHp, gameplay::FEffectScope::Run, ApplyRestorePartyHp,
                   ValidatePositiveAmount);
    RegisterEffect(registry, kEventEffectDamageParty, gameplay::FEffectScope::Run, ApplyDamageParty,
                   ValidatePositiveAmount);
    RegisterEffect(registry, kEventEffectDamagePartyPct, gameplay::FEffectScope::Run, ApplyDamageParty,
                   ValidateDamagePercent);
    RegisterEffect(registry, kEventEffectGainMaterials, gameplay::FEffectScope::Run, ApplyGainMaterials,
                   ValidatePositiveAmount);
    RegisterEffect(registry, kEventEffectGainRuneRandom, gameplay::FEffectScope::Run, ApplyGainRuneRandom,
                   ValidateEmptyRunePool);
    RegisterEffect(registry, kEventEffectGainNodePoints, gameplay::FEffectScope::Run, ApplyGainNodePoints,
                   ValidateNonNegativeAmount);
}

std::optional<FEventOutcome> ApplyEventChoice(entt::registry& registry, const FEventChoice& choice) {
    if (registry.ctx().find<FDungeonRun>() == nullptr) return std::nullopt;  // nothing to apply
    // A fresh accumulator: an aborted earlier application must not leak into this choice's report.
    registry.ctx().erase<FEventOutcomeAccumulator>();
    registry.ctx().emplace<FEventOutcomeAccumulator>();
    const FEffectContext context{.scope = gameplay::FEffectScope::Run, .source = kEventEffectSource};
    for (const auto& effect : choice.effects) {
        // Authored order. A refused effect (unknown or wrong-scope kind — ApplyEffect logged the reason)
        // never swallows the remaining effects.
        if (!ApplyEffect(registry, context, effect)) {
            LOG_WARN("Event effect '{}' was refused; the remaining effects still apply.", effect.kind);
        }
    }
    FEventOutcome outcome;
    if (auto* accumulator = registry.ctx().find<FEventOutcomeAccumulator>()) {
        outcome = std::move(accumulator->outcome);
    }
    registry.ctx().erase<FEventOutcomeAccumulator>();
    return outcome;
}

std::string DescribeEventOutcome(const FEventOutcome& outcome) {
    std::string text;
    const auto add = [&text](std::string_view part) {
        if (!text.empty()) text += ", ";
        text += part;
    };
    if (outcome.hpRestored > 0) add("party HP +" + std::to_string(outcome.hpRestored));
    if (outcome.hpLost > 0) add("party HP -" + std::to_string(outcome.hpLost));
    if (outcome.materials > 0) add("+" + std::to_string(outcome.materials) + " materials");
    if (outcome.nodePoints > 0) {
        add("+" + std::to_string(outcome.nodePoints) + (outcome.nodePoints == 1 ? " node point" : " node points"));
    }
    for (const auto& runeId : outcome.runes) {
        add("rune " + runeId);
    }
    return text;
}

int EventUnitBaseStat(const gameplay::FUnitContent& unit, std::string_view stat) {
    if (stat == "health") return static_cast<int>(std::floor(unit.maxHealth));
    if (stat == "attack") return unit.attack;
    if (stat == "magic_attack") return unit.magicAttack;
    if (stat == "defense") return unit.defense;
    if (stat == "magic_defense") return unit.magicDefense;
    if (stat == "speed") return static_cast<int>(std::floor(unit.speed));
    return 0;  // outside the kEventCheckStats vocabulary
}

std::string EventStatLabel(std::string_view stat) {
    for (const auto& name : kEventStatNames) {
        if (name.id == stat) return std::string(name.label);
    }
    return std::string(stat);  // outside the vocabulary: the raw id the content wrote
}

FEventStatReading EventPartyStat(const entt::registry& registry, std::string_view stat, bool warnUnresolved) {
    FEventStatReading reading;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return reading;
    bool warned = false;
    for (const auto& member : run->party) {
        const auto* unit = ResolveRunMemberUnit(registry, member.id);
        if (unit == nullptr) {
            if (warnUnresolved && !warned) {  // one warning per call, however many members fail to resolve
                warned = true;
                LOG_WARN("Event stat '{}': member '{}' has no resolvable unit; unresolvable members are skipped.",
                         stat, member.id);
            }
            continue;
        }
        const int value = EventUnitBaseStat(*unit, stat) + DungeonMemberStatBonus(member, stat);
        if (!reading.resolved || value > reading.value) {  // strict > keeps the first maximum = party order
            reading.resolved = true;
            reading.value = value;
            reading.memberName = member.displayName.empty() ? member.id : member.displayName;
        }
    }
    return reading;
}

FEventGate EvaluateEventGate(const entt::registry& registry, const FEventChoice& choice, bool warnUnresolved) {
    FEventGate gate;
    if (!choice.check) return gate;  // no gate: open, no reason
    const FEventCheck& check = *choice.check;
    if (!IsEventStatKnown(check.stat)) {
        gate.open = false;
        gate.reason = EventStatLabel(check.stat) + " unknown";
        return gate;
    }
    const FEventStatReading reading = EventPartyStat(registry, check.stat, warnUnresolved);
    if (!reading.resolved) {  // no run or no member resolved: a closed gate, never a guessed value
        gate.open = false;
        gate.reason = EventStatLabel(check.stat) + " unknown";
        return gate;
    }
    const std::string label = EventStatLabel(check.stat);
    if (reading.value >= check.difficulty) {
        gate.open = true;
        gate.reason = label + " " + std::to_string(reading.value) + " >= " + std::to_string(check.difficulty) +
                      " (" + reading.memberName + ")";
    } else {
        gate.open = false;
        gate.reason = label + " " + std::to_string(reading.value) + " < " + std::to_string(check.difficulty);
    }
    return gate;
}

int EventEffectLossFor(const entt::registry& registry, const FRunMember& member,
                       const gameplay::FEffectSpec& effect) {
    return EventDamageLoss(registry, member, effect, DungeonEffectiveHealth(registry, member));
}

std::vector<FEventMemberLoss> PreviewEventChoiceLoss(const entt::registry& registry, const FEventChoice& choice) {
    const bool hasDamage = std::ranges::any_of(choice.effects, [](const gameplay::FEffectSpec& effect) {
        return IsEventDamageKind(effect.kind);
    });
    if (!hasDamage) return {};  // no damage effect: no preview at all (an honest absence, not zeros)
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return {};
    std::vector<FEventMemberLoss> preview;
    preview.reserve(run->party.size());
    for (const auto& member : run->party) {
        FEventMemberLoss entry;
        entry.memberName = member.displayName.empty() ? member.id : member.displayName;
        // The damage applier materialises the -1 sentinel; the restore applier skips only a member still at
        // it. Track both, or "damage then heal" would preview differently than it applies.
        bool sentinel = member.health < 0;
        int healthLeft = DungeonEffectiveHealth(registry, member);
        int loss = 0;
        for (const auto& effect : choice.effects) {
            // The effects apply in authored order: a later damage effect hits the health the earlier ones
            // left, and a heal raises it. Displayed loss == applied loss.
            if (IsEventDamageKind(effect.kind)) {
                const int now = EventDamageLoss(registry, member, effect, healthLeft);
                loss += now;
                healthLeft -= now;
                if (now > 0) sentinel = false;  // the applier just wrote a materialised health
            } else if (effect.kind == kEventEffectRestorePartyHp && !sentinel) {
                healthLeft = RestoredHealth(registry, member, effect.amount, healthLeft);
            }
        }
        entry.loss = loss;
        preview.push_back(std::move(entry));
    }
    return preview;
}

} // namespace game
