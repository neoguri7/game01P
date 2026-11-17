#include "gameplay/dungeon/FRunStart.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Logger.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FUnitContent.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/meta/FMetaProgress.h"

namespace game {

namespace {

/// The pool rule's single implementation (R6 P6): the unit's authored pool in order with every id the
/// predicate excludes dropped. MemberSkillPool excludes the gated skills; the boot validation's base pool
/// excludes every skill that has an unlock row at all (a fresh profile's pool).
template <typename TExcluded>
std::vector<std::string> PoolFiltered(entt::registry& registry, std::string_view memberId,
                                      TExcluded excluded) {
    const gameplay::FUnitContent* unit = ResolveRunMemberUnit(registry, memberId);
    if (unit == nullptr) return {};
    std::vector<std::string> pool;
    pool.reserve(unit->skillIds.size());
    for (const std::string& skillId : unit->skillIds) {
        if (excluded(registry, skillId)) continue;
        pool.push_back(skillId);
    }
    return pool;
}

/// The maximum active-slot count a fresh roster member must be able to fill (WP-21 §1/§3): the HUD/battle
/// contract's kBaseActiveSkillSlots, or the data cap when the slot promotion axis is on.
int MaximumActiveSlots(entt::registry& registry) {
    const auto* progression = FindMetaProgression(registry);
    if (progression != nullptr && progression->slotPromotion) {
        return std::max(kBaseActiveSkillSlots, progression->slotCap);
    }
    return kBaseActiveSkillSlots;
}

} // namespace

std::vector<std::string> MemberSkillPool(entt::registry& registry, std::string_view memberId) {
    return PoolFiltered(registry, memberId, [](entt::registry& reg, std::string_view skillId) {
        return IsUnlockGated(reg, skillId);
    });
}

std::vector<std::string> DefaultMemberLoadout(entt::registry& registry, std::string_view memberId) {
    const std::vector<std::string> pool = MemberSkillPool(registry, memberId);
    const int slots = std::max(0, MemberActiveSkillSlots(registry, memberId));
    if (static_cast<int>(pool.size()) <= slots) return pool;
    return {pool.begin(), pool.begin() + static_cast<std::ptrdiff_t>(slots)};
}

std::vector<FRosterChoice> BuildRosterChoices(entt::registry& registry) {
    std::vector<FRosterChoice> choices;
    for (const FMetaRosterRow& row : MetaRosterCatalog(registry)) {
        FRosterChoice choice;
        choice.memberId = row.id;
        choice.name = row.name;
        choice.trait = row.trait;
        if (row.locked) {
            choice.availability = ERosterAvailability::LockedStub;
            choice.reason = "Not yet recruitable";
        } else {
            const gameplay::FUnitContent* unit = ResolveRunMemberUnit(registry, row.id);
            if (unit == nullptr) {
                choice.availability = ERosterAvailability::NoUnit;
                choice.reason = "No unit template";
            } else if (IsUnlockGated(registry, row.id)) {
                choice.unitId = unit->id;
                choice.availability = ERosterAvailability::NeedsUnlock;
                const auto unlock = FindMetaUnlockRow(registry, row.id);
                choice.reason = unlock
                    ? "Unlock " + unlock->id + " for " + std::to_string(unlock->cost) + " seals"
                    : "Not yet unlocked";
            } else {
                choice.unitId = unit->id;
                choice.availability = ERosterAvailability::Selectable;
            }
        }
        choices.push_back(std::move(choice));
    }
    return choices;
}

bool IsRosterMemberSelectable(entt::registry& registry, std::string_view memberId) {
    for (const FMetaRosterRow& row : MetaRosterCatalog(registry)) {
        if (row.id != memberId) continue;
        if (row.locked) return false;
        break;
    }
    if (ResolveRunMemberUnit(registry, memberId) == nullptr) return false;
    return !IsUnlockGated(registry, memberId);
}

std::optional<std::string> ValidateRunSetup(entt::registry& registry, const FRunSetup& setup) {
    if (setup.party.size() != kRunPartySize) {
        return "The party must have exactly " + std::to_string(kRunPartySize) + " members.";
    }
    for (std::size_t i = 0; i < setup.party.size(); ++i) {
        for (std::size_t j = i + 1; j < setup.party.size(); ++j) {
            if (setup.party[i] == setup.party[j]) return "Every party member must be different.";
        }
    }
    for (std::size_t i = 0; i < setup.party.size(); ++i) {
        const std::string& memberId = setup.party[i];
        if (!IsRosterMemberSelectable(registry, memberId)) {
            return "Member '" + memberId + "' cannot join the party.";
        }
        const int slots = std::max(0, MemberActiveSkillSlots(registry, memberId));
        const std::vector<std::string> loadout =
            i < setup.skills.size() ? setup.skills[i] : std::vector<std::string>{};
        if (static_cast<int>(loadout.size()) != slots) {
            return "Every member needs exactly " + std::to_string(slots) + " skills.";
        }
        for (std::size_t slot = 0; slot < loadout.size(); ++slot) {
            for (std::size_t other = slot + 1; other < loadout.size(); ++other) {
                if (loadout[slot] == loadout[other]) return "Each skill can be picked once per member.";
            }
        }
        const std::vector<std::string> pool = MemberSkillPool(registry, memberId);
        for (const std::string& skillId : loadout) {
            if (std::ranges::find(pool, skillId) == pool.end()) {
                return "A chosen skill is not in this member's pool.";
            }
        }
    }
    return std::nullopt;
}

FStartSupportPreview PreviewStartSupport(entt::registry& registry, int startCleared) {
    FStartSupportPreview preview;
    const auto* sequence = FindRunSequence(registry);
    if (sequence == nullptr) return preview;
    const auto* checkpoint = registry.ctx().find<FDungeonCheckpoint>();
    const int index = std::clamp(startCleared, 0, checkpoint != nullptr ? std::max(0, checkpoint->cleared) : 0);
    preview.materials = sequence->StartMaterials(index);
    const auto* rewards = registry.ctx().find<FRewards>();
    preview.forcedRuneOffer = index > 0 && sequence->startSupport.runeOffers > 0 && rewards != nullptr &&
                              rewards->tables.pityThreshold > 0;
    return preview;
}

bool StartDungeonRunFromSetup(entt::registry& registry, std::uint32_t seed, bool fresh, int startCleared,
                              const FRunSetup& setup) {
    if (const auto problem = ValidateRunSetup(registry, setup)) {
        LOG_WARN("The run setup was refused: {}", *problem);
        return false;
    }
    registry.ctx().insert_or_assign<FRunSetupRequest>(FRunSetupRequest{setup});
    if (startCleared >= 0) {
        StartRunAt(registry, seed, startCleared);
    } else {
        StartRun(registry, seed, fresh);
    }
    return true;
}

std::optional<std::string> ValidateRosterContent(entt::registry& registry) {
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (content == nullptr) return "Gameplay content is unavailable.";
    const int maxSlots = MaximumActiveSlots(registry);
    for (const FMetaRosterRow& row : MetaRosterCatalog(registry)) {
        if (row.locked) continue;
        const gameplay::FUnitContent* unit = ResolveRunMemberUnit(registry, row.id);
        if (unit == nullptr) return "Roster member '" + row.id + "' does not resolve to a unit template.";
        const std::vector<std::string> basePool =
            PoolFiltered(registry, row.id, [](entt::registry& reg, std::string_view skillId) {
                return FindMetaUnlockRow(reg, skillId).has_value();
            });
        if (static_cast<int>(basePool.size()) < maxSlots) {
            return "Roster member '" + row.id + "' has fewer than " + std::to_string(maxSlots) +
                   " base skills for its active slots.";
        }
        for (const std::string& skillId : unit->skillIds) {
            if (content->skills.find(skillId) == nullptr) {
                return "Roster member '" + row.id + "' names the unknown skill '" + skillId + "'.";
            }
            if (const auto problem = ValidateSkillSlot(FSkillSlot{skillId, {}}, *content)) {
                return "Roster member '" + row.id + "' skill '" + skillId + "' is illegal: " + *problem;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> ValidateUnlockTargets(entt::registry& registry) {
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (content == nullptr) return "Gameplay content is unavailable.";
    for (const FMetaUnlockRow& row : MetaUnlockCatalog(registry)) {
        if (row.kind == "skill") {
            // The AUTHORED pools of the fieldable members (the gate-filtered MemberSkillPool would only
            // prove the row is hidden, never that a purchase opens anything).
            bool authored = false;
            for (const FMetaRosterRow& member : MetaRosterCatalog(registry)) {
                if (member.locked) continue;
                const gameplay::FUnitContent* unit = ResolveRunMemberUnit(registry, member.id);
                if (unit == nullptr) continue;
                if (std::ranges::find(unit->skillIds, row.id) != unit->skillIds.end()) {
                    authored = true;
                    break;
                }
            }
            if (!authored) {
                return "Unlock '" + row.id + "' is a skill no fieldable roster member authors.";
            }
        } else if (row.kind == "rune") {
            if (content->runes.find(row.id) == nullptr) {
                return "Unlock '" + row.id + "' is not in the rune table.";
            }
        }
    }
    return std::nullopt;
}

FPreparationDraft MakeDefaultDraft(entt::registry& registry, const FRunStartRequest& request) {
    FPreparationDraft draft;
    draft.fresh = request.fresh;
    if (const auto* checkpoint = registry.ctx().find<FDungeonCheckpoint>()) {
        draft.maxStartCleared = checkpoint->cleared;
    }
    draft.startCleared = request.startCleared >= 0
        ? std::clamp(request.startCleared, 0, draft.maxStartCleared)
        : (request.fresh ? 0 : draft.maxStartCleared);
    const auto addMember = [&registry, &draft](const std::string& memberId) {
        if (draft.setup.party.size() >= kRunPartySize) return;
        if (std::ranges::find(draft.setup.party, memberId) != draft.setup.party.end()) return;
        if (!IsRosterMemberSelectable(registry, memberId)) return;
        draft.setup.party.push_back(memberId);
        draft.setup.skills.push_back(DefaultMemberLoadout(registry, memberId));
    };
    // The authored party first (dungeon.json placement order), then the remaining selectable catalog rows
    // in catalog order.
    for (const auto& row : DungeonDefinition(registry).at("party")) {
        addMember(row.at("id").get<std::string>());
    }
    for (const FMetaRosterRow& row : MetaRosterCatalog(registry)) {
        addMember(row.id);
    }
    return draft;
}

bool ToggleDraftMember(entt::registry& registry, FPreparationDraft& draft, std::string_view memberId) {
    const auto found = std::ranges::find(draft.setup.party, memberId);
    if (found != draft.setup.party.end()) {
        const std::size_t index = static_cast<std::size_t>(found - draft.setup.party.begin());
        draft.setup.party.erase(found);
        if (index < draft.setup.skills.size()) {
            draft.setup.skills.erase(draft.setup.skills.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }
    if (draft.setup.party.size() >= kRunPartySize) return false;
    if (!IsRosterMemberSelectable(registry, memberId)) return false;
    draft.setup.party.emplace_back(memberId);
    draft.setup.skills.push_back(DefaultMemberLoadout(registry, memberId));
    return true;
}

bool SetDraftSkill(entt::registry& registry, FPreparationDraft& draft, std::size_t memberIndex, std::size_t slot,
                   std::string_view skillId) {
    if (memberIndex >= draft.setup.party.size() || memberIndex >= draft.setup.skills.size()) return false;
    std::vector<std::string>& loadout = draft.setup.skills[memberIndex];
    if (slot >= loadout.size()) return false;
    const std::vector<std::string> pool = MemberSkillPool(registry, draft.setup.party[memberIndex]);
    if (std::ranges::find(pool, skillId) == pool.end()) return false;
    const auto existing = std::ranges::find(loadout, skillId);
    if (existing != loadout.end()) {
        const std::size_t other = static_cast<std::size_t>(existing - loadout.begin());
        if (other != slot) std::swap(loadout[slot], loadout[other]);
        return true;
    }
    loadout[slot] = std::string(skillId);
    return true;
}

bool SetDraftStartCleared(FPreparationDraft& draft, int startCleared) {
    if (startCleared < 0 || startCleared > draft.maxStartCleared) return false;
    draft.startCleared = startCleared;
    return true;
}

} // namespace game
