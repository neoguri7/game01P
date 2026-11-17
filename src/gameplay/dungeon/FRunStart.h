#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FDungeonRun.h"

namespace game {

/// The preparation screen's result (WP-21b): four roster member ids in dungeon.json party[] placement-cell
/// order, and each member's initial loadout (skill ids, size = MemberActiveSkillSlots, unique, each from
/// MemberSkillPool).
struct FRunSetup {
    std::vector<std::string> party;
    std::vector<std::vector<std::string>> skills;
};

/// ctx carrier: StartDungeonRunFromSetup inserts it; InstallDungeonRunAt consumes it exactly once.
struct FRunSetupRequest {
    FRunSetup setup;
};

/// The battlefield has four authored placement cells (dungeon.json party[]).
inline constexpr std::size_t kRunPartySize = 4;

/// The roster panel's classification of one catalog row (WP-21 §2).
enum class ERosterAvailability { Selectable, LockedStub, NeedsUnlock, NoUnit };

struct FRosterChoice {
    std::string memberId;
    std::string name;        // roster name ("???" for stubs)
    std::string trait;
    std::string unitId;      // resolved unit template id, empty when unresolved/stub
    ERosterAvailability availability{ERosterAvailability::LockedStub};
    std::string reason;      // English; empty when Selectable. NeedsUnlock: "Unlock <id> for <cost> seals"
};

/// The roster rule, single sentence (WP-21 §2): a member is selectable iff it is not a `locked: true` stub,
/// its unit resolves (ResolveRunMemberUnit), and IsUnlockGated(memberId) is false. Catalog order
/// (MetaRosterCatalog).
[[nodiscard]] std::vector<FRosterChoice> BuildRosterChoices(entt::registry& registry);
[[nodiscard]] bool IsRosterMemberSelectable(entt::registry& registry, std::string_view memberId);

/// The single skill-pool rule (R6 P6 — a gate, not a union): the unit's authored `units.json skills` in
/// order minus every skill that has an `unlocks.json kind=skill` row and is not unlocked. Empty for an
/// unresolved member.
[[nodiscard]] std::vector<std::string> MemberSkillPool(entt::registry& registry, std::string_view memberId);
/// First MemberActiveSkillSlots(member) entries of MemberSkillPool.
[[nodiscard]] std::vector<std::string> DefaultMemberLoadout(entt::registry& registry, std::string_view memberId);

/// nullopt = valid; else the English reason the Start button shows (party size != 4, duplicate member,
/// member not selectable, loadout size != active slots, duplicate skill, skill not in MemberSkillPool).
[[nodiscard]] std::optional<std::string> ValidateRunSetup(entt::registry& registry, const FRunSetup& setup);

/// D-7 preview — the SAME numbers StartRunAt applies: materials = FRunSequence::StartMaterials(index);
/// forcedRuneOffer = index > 0 && startSupport.runeOffers > 0 && rewards pity threshold > 0.
struct FStartSupportPreview {
    int materials{0};
    bool forcedRuneOffer{false};
};
[[nodiscard]] FStartSupportPreview PreviewStartSupport(entt::registry& registry, int startCleared);

/// The only setup-driven new-run path. Validates (invalid = no run, returns false + LOG_WARN). Inserts
/// FRunSetupRequest, then startCleared >= 0 ? StartRunAt(seed, startCleared) : StartRun(seed, fresh).
bool StartDungeonRunFromSetup(entt::registry& registry, std::uint32_t seed, bool fresh, int startCleared,
                              const FRunSetup& setup);

/// Boot validation (WP-21 §1/§3), called by the composition root (main.cpp) after InitializeMetaProgress;
/// tests call it directly: for every roster row that is not `locked: true` (unlock targets included): the
/// unit resolves and exists in content.units; the UNGATED base pool (authored pool minus every skill that
/// has an unlocks.json row — a fresh profile's pool) has >= the maximum active slots
/// (kBaseActiveSkillSlots, or slotCap when slotPromotion is on); every authored skill exists in
/// content.skills and ValidateSkillSlot({skill, {}}, content) passes. nullopt = valid, else the reason.
[[nodiscard]] std::optional<std::string> ValidateRosterContent(entt::registry& registry);

/// Boot validation (w9-hub critic F4, R6 P12), called by the composition root right after
/// ValidateRosterContent: every kind=skill/rune unlock catalog row must reach a real consumer, or the shop
/// would sell a no-op. kind=skill: at least one NON-locked roster member authors the id in its unit pool
/// (ResolveRunMemberUnit — the authored pool, never the gate-filtered MemberSkillPool). kind=rune: the id
/// exists in the rune table. kind=roster is not checked (the EvaluateUnlockOffer locked/missing-target
/// guard owns it) and neither is any other kind. nullopt = valid, else the first violation's reason, which
/// names the offending id.
[[nodiscard]] std::optional<std::string> ValidateUnlockTargets(entt::registry& registry);

/// The preparation draft (pure rules the view drives and the tests call). Built from the carrier.
struct FPreparationDraft {
    bool fresh{false};
    int startCleared{0};      // chosen start index, 0..maxStartCleared
    int maxStartCleared{0};   // FDungeonCheckpoint.cleared
    FRunSetup setup;
};
/// Default = the first kRunPartySize selectable roster members in dungeon.json party[] order (the authored
/// party), each with DefaultMemberLoadout; startCleared = request.startCleared >= 0 ? clamp(it) : (fresh ? 0
/// : checkpoint).
[[nodiscard]] FPreparationDraft MakeDefaultDraft(entt::registry& registry, const FRunStartRequest& request);
/// Adds a selectable member (party not full, not already in) with its default loadout, or removes a member
/// that is in.
bool ToggleDraftMember(entt::registry& registry, FPreparationDraft& draft, std::string_view memberId);
/// Sets party[memberIndex]'s slot `slot` to `skillId` (must be in its pool; a skill already in another slot
/// of the same member swaps places with it).
bool SetDraftSkill(entt::registry& registry, FPreparationDraft& draft, std::size_t memberIndex, std::size_t slot,
                   std::string_view skillId);
/// Rejects out of [0, maxStartCleared].
bool SetDraftStartCleared(FPreparationDraft& draft, int startCleared);

} // namespace game
