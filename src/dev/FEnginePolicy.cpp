#include "dev/FEnginePolicy.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Logger.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"

namespace game::dev {
namespace {

/// The two context pieces every entry point needs: the run and the content tables.
struct FPolicyView {
    const FDungeonRun* run{nullptr};
    const gameplay::FContentRegistry* content{nullptr};
};

[[nodiscard]] FPolicyView ViewOf(const entt::registry& registry) {
    return FPolicyView{registry.ctx().find<FDungeonRun>(), registry.ctx().find<gameplay::FContentRegistry>()};
}

[[nodiscard]] bool HoldsRune(const FSkillSlot& slot, std::string_view runeId) {
    return std::ranges::find(slot.runeIds, runeId) != slot.runeIds.end();
}

[[nodiscard]] bool HoldsAll(const FSkillSlot& slot, const std::vector<std::string>& runeIds) {
    for (const std::string& runeId : runeIds) {
        if (!HoldsRune(slot, runeId)) return false;
    }
    return true;
}

/// Appends runeId unless the list already has it (set-union semantics for a placement plan).
void AppendRune(std::vector<std::string>& runes, std::string_view runeId) {
    if (std::ranges::find(runes, runeId) == runes.end()) runes.push_back(std::string(runeId));
}

void AppendRunes(std::vector<std::string>& runes, const std::vector<std::string>& extra) {
    for (const std::string& runeId : extra) AppendRune(runes, runeId);
}

/// Every rune of E the slot is missing, in requires_runes order.
[[nodiscard]] std::vector<std::string> MissingRunes(const FSkillSlot& slot, const gameplay::FEngineContent& engine) {
    std::vector<std::string> missing;
    for (const std::string& runeId : engine.requiresRunes) {
        if (!HoldsRune(slot, runeId)) missing.push_back(runeId);
    }
    return missing;
}

/// ValidateSkillSlot on a copy of `slot` with `extraRunes` appended; ids already present are skipped, so the
/// caller orders the plan (the candidate rune first, then the slot's reserved runes).
[[nodiscard]] bool Accepts(const gameplay::FContentRegistry& content, const FSkillSlot& slot,
                           const std::vector<std::string>& extraRunes) {
    FSkillSlot candidate = slot;
    AppendRunes(candidate.runeIds, extraRunes);
    return !ValidateSkillSlot(candidate, content).has_value();
}

/// The member's first slot with that skill id (CheckEngineIgnition's slot rule).
[[nodiscard]] bool IsFirstSlotWithSkill(const FRunMember& member, std::size_t slotIndex) {
    const std::string& skillId = member.skills[slotIndex].skillId;
    for (std::size_t index = 0; index < slotIndex; ++index) {
        if (member.skills[index].skillId == skillId) return false;
    }
    return true;
}

/// HOST for E (FEnginePolicy.h): enemy target, power > 0, the member's first slot with that skill id, and
/// ValidateSkillSlot accepts the slot's current runes plus every missing rune of E.
[[nodiscard]] bool IsHost(const gameplay::FContentRegistry& content, const FRunMember& member,
                          std::size_t slotIndex, const gameplay::FEngineContent& engine) {
    if (slotIndex >= member.skills.size()) return false;
    const gameplay::FSkillContent* skill = content.skills.find(member.skills[slotIndex].skillId);
    if (skill == nullptr || skill->target != "enemy" || skill->power <= 0) return false;
    if (!IsFirstSlotWithSkill(member, slotIndex)) return false;
    return Accepts(content, member.skills[slotIndex], MissingRunes(member.skills[slotIndex], engine));
}

/// HALF-PAIR slot for E: holds >= 1 rune of E, misses >= 1, and is a host for E.
[[nodiscard]] bool IsHalfPair(const gameplay::FContentRegistry& content, const FRunMember& member,
                              std::size_t slotIndex, const gameplay::FEngineContent& engine) {
    const FSkillSlot& slot = member.skills[slotIndex];
    bool holds = false;
    bool misses = false;
    for (const std::string& runeId : engine.requiresRunes) {
        if (HoldsRune(slot, runeId)) {
            holds = true;
        } else {
            misses = true;
        }
    }
    return holds && misses && IsHost(content, member, slotIndex, engine);
}

/// RESERVED runes of a slot: the union of the missing runes of every E the slot is a half-pair for (engine
/// row order, deduped).
[[nodiscard]] std::vector<std::string> ReservedRunes(const gameplay::FContentRegistry& content,
                                                     const FRunMember& member, std::size_t slotIndex) {
    std::vector<std::string> reserved;
    for (const gameplay::FEngineContent& engine : content.engines.rows) {
        if (!IsHalfPair(content, member, slotIndex, engine)) continue;
        AppendRunes(reserved, MissingRunes(member.skills[slotIndex], engine));
    }
    return reserved;
}

/// One fitted rune's address in the run party.
struct FRuneLocation {
    std::size_t member{0};
    std::size_t slot{0};
    std::size_t runeIndex{0};
};

[[nodiscard]] bool SameSlot(const FRuneLocation& first, const FRuneLocation& second) {
    return first.member == second.member && first.slot == second.slot;
}

/// Every rune of E fitted in the party, in requires_runes order; empty when one is not fitted (pooled or
/// unowned). A rune id is fitted at most once (owned once), so the first match is its location.
[[nodiscard]] std::vector<FRuneLocation> FittedLocations(const FDungeonRun& run,
                                                         const gameplay::FEngineContent& engine) {
    std::vector<FRuneLocation> locations;
    locations.reserve(engine.requiresRunes.size());
    for (const std::string& runeId : engine.requiresRunes) {
        bool found = false;
        for (std::size_t member = 0; member < run.party.size() && !found; ++member) {
            for (std::size_t slot = 0; slot < run.party[member].skills.size(); ++slot) {
                const std::vector<std::string>& runeIds = run.party[member].skills[slot].runeIds;
                const auto match = std::ranges::find(runeIds, runeId);
                if (match == runeIds.end()) continue;
                locations.push_back(FRuneLocation{member, slot, static_cast<std::size_t>(match - runeIds.begin())});
                found = true;
                break;
            }
        }
        if (!found) return {};
    }
    return locations;
}

/// True when every located rune sits in the same slot.
[[nodiscard]] bool AllInOneSlot(const std::vector<FRuneLocation>& locations) {
    if (locations.empty()) return true;  // a runeless engine row has nothing to gather
    for (const FRuneLocation& location : locations) {
        if (!SameSlot(location, locations.front())) return false;
    }
    return true;
}

/// Read-only (b)-style move scan for one engine whose runes are all fitted (parallel to `locations`). One
/// entry per rune in requires_runes order:
///   host: the single slot T holding E's other fitted runes exists, differs from the rune's slot, is a host
///     for E, and ValidateSkillSlot(T plus the rune) passes.
///   refusal: when host, the first reason the move would be refused — (c) "would break <E2>" (the removal
///     would break another completed pair in the rune's slot) or the removal revalidation text — else empty.
struct FMoveScan {
    std::size_t runeIndex{0};
    FRuneLocation from;
    FRuneLocation to;
    bool host{false};
    std::string refusal;
};

[[nodiscard]] std::vector<FMoveScan> ScanEngineMoves(const gameplay::FContentRegistry& content,
                                                     const FDungeonRun& run,
                                                     const gameplay::FEngineContent& engine,
                                                     const std::vector<FRuneLocation>& locations) {
    std::vector<FMoveScan> scans;
    scans.reserve(locations.size());
    for (std::size_t index = 0; index < locations.size(); ++index) {
        FMoveScan scan;
        scan.runeIndex = index;
        scan.from = locations[index];
        const std::string& runeId = engine.requiresRunes[index];

        // T: the single slot holding E's other fitted runes; none when they are spread across slots.
        bool haveTarget = false;
        for (std::size_t other = 0; other < locations.size(); ++other) {
            if (other == index) continue;
            if (!haveTarget) {
                scan.to = locations[other];
                haveTarget = true;
            } else if (!SameSlot(scan.to, locations[other])) {
                haveTarget = false;
                break;
            }
        }
        if (haveTarget && SameSlot(scan.to, scan.from)) haveTarget = false;  // all runes already in one slot
        if (!haveTarget) {
            scans.push_back(std::move(scan));
            continue;
        }

        // T must be a host for E and accept the rune itself (the explicit ValidateSkillSlot(T plus rune);
        // the host check is the same validation when T holds every other rune).
        const FRunMember& targetMember = run.party[scan.to.member];
        if (!IsHost(content, targetMember, scan.to.slot, engine)) {
            scans.push_back(std::move(scan));
            continue;
        }
        FSkillSlot equipped = targetMember.skills[scan.to.slot];
        equipped.runeIds.push_back(runeId);
        if (ValidateSkillSlot(equipped, content)) {
            scans.push_back(std::move(scan));
            continue;
        }
        scan.host = true;

        // (c): the removal must not break another completed pair in the rune's current slot.
        for (const gameplay::FEngineContent& otherEngine : content.engines.rows) {
            if (std::ranges::find(otherEngine.requiresRunes, runeId) == otherEngine.requiresRunes.end()) continue;
            if (!HoldsAll(run.party[scan.from.member].skills[scan.from.slot], otherEngine.requiresRunes)) continue;
            scan.refusal = "would break " + otherEngine.id;
            break;
        }
        if (!scan.refusal.empty()) {
            scans.push_back(std::move(scan));
            continue;
        }

        // The removal revalidation: RemoveDungeonRune erases the rune and validates the rest of the slot.
        FSkillSlot reduced = run.party[scan.from.member].skills[scan.from.slot];
        reduced.runeIds.erase(reduced.runeIds.begin() + static_cast<std::ptrdiff_t>(scan.from.runeIndex));
        if (const auto problem = ValidateSkillSlot(reduced, content)) scan.refusal = *problem;
        scans.push_back(std::move(scan));
    }
    return scans;
}

} // namespace

std::string_view EngineRuleName(EEngineRule rule) {
    switch (rule) {
    case EEngineRule::Complete: return "complete";
    case EEngineRule::Start: return "start";
    case EEngineRule::Fallback: return "fallback";
    case EEngineRule::Auto: return "auto";
    }
    return "auto";
}

std::optional<FEnginePlacement> ChooseEnginePlacement(const entt::registry& registry, std::string_view runeId) {
    const FPolicyView view = ViewOf(registry);
    if (view.run == nullptr || view.content == nullptr) return std::nullopt;
    const FDungeonRun& run = *view.run;
    const gameplay::FContentRegistry& content = *view.content;
    const std::vector<std::string> runeOnly{std::string(runeId)};

    // Rule 1 — Complete: the first half-pair slot (member -> slot -> engine row) whose missing set contains
    // this rune and that accepts it. Reservations are ignored: completing wins.
    for (std::size_t member = 0; member < run.party.size(); ++member) {
        const FRunMember& runMember = run.party[member];
        for (std::size_t slot = 0; slot < runMember.skills.size(); ++slot) {
            if (HoldsRune(runMember.skills[slot], runeId)) continue;  // the missing set cannot contain the rune
            for (const gameplay::FEngineContent& engine : content.engines.rows) {
                if (std::ranges::find(engine.requiresRunes, runeId) == engine.requiresRunes.end()) continue;
                if (!IsHalfPair(content, runMember, slot, engine)) continue;
                if (!Accepts(content, runMember.skills[slot], runeOnly)) continue;
                return FEnginePlacement{member, slot, EEngineRule::Complete, engine.id};
            }
        }
    }

    // Rule 2 — Start: the first E (row order) containing this rune whose OTHER runes are all unowned, then
    // the first host for E that accepts the rune now AND accepts the whole pair plus the slot's reservations.
    for (const gameplay::FEngineContent& engine : content.engines.rows) {
        if (std::ranges::find(engine.requiresRunes, runeId) == engine.requiresRunes.end()) continue;
        bool othersUnowned = true;
        for (const std::string& otherRune : engine.requiresRunes) {
            if (otherRune == runeId) continue;
            if (IsRuneOwned(registry, otherRune)) {
                othersUnowned = false;
                break;
            }
        }
        if (!othersUnowned) continue;
        for (std::size_t member = 0; member < run.party.size(); ++member) {
            const FRunMember& runMember = run.party[member];
            for (std::size_t slot = 0; slot < runMember.skills.size(); ++slot) {
                if (HoldsRune(runMember.skills[slot], runeId)) continue;  // a second copy would be illegal
                if (!IsHost(content, runMember, slot, engine)) continue;
                if (!Accepts(content, runMember.skills[slot], runeOnly)) continue;
                std::vector<std::string> plan = runeOnly;
                AppendRunes(plan, MissingRunes(runMember.skills[slot], engine));
                AppendRunes(plan, ReservedRunes(content, runMember, slot));
                if (!Accepts(content, runMember.skills[slot], plan)) continue;
                return FEnginePlacement{member, slot, EEngineRule::Start, engine.id};
            }
        }
    }

    // Rule 3 — Fallback: the first slot accepting (current ∪ {runeId} ∪ reserved); then Auto: the first slot
    // accepting (current ∪ {runeId}).
    for (std::size_t member = 0; member < run.party.size(); ++member) {
        const FRunMember& runMember = run.party[member];
        for (std::size_t slot = 0; slot < runMember.skills.size(); ++slot) {
            if (HoldsRune(runMember.skills[slot], runeId)) continue;
            std::vector<std::string> plan = runeOnly;
            AppendRunes(plan, ReservedRunes(content, runMember, slot));
            if (Accepts(content, runMember.skills[slot], plan)) {
                return FEnginePlacement{member, slot, EEngineRule::Fallback, {}};
            }
        }
    }
    for (std::size_t member = 0; member < run.party.size(); ++member) {
        const FRunMember& runMember = run.party[member];
        for (std::size_t slot = 0; slot < runMember.skills.size(); ++slot) {
            if (HoldsRune(runMember.skills[slot], runeId)) continue;
            if (Accepts(content, runMember.skills[slot], runeOnly)) {
                return FEnginePlacement{member, slot, EEngineRule::Auto, {}};
            }
        }
    }
    return std::nullopt;
}

int ScoreEngineCard(const entt::registry& registry, std::string_view runeId) {
    if (IsRuneOwned(registry, runeId)) return 0;  // a duplicate card is a refund, not a rune
    const std::optional<FEnginePlacement> placement = ChooseEnginePlacement(registry, runeId);
    if (!placement) return 0;
    if (placement->rule == EEngineRule::Complete) return kEngineCardScoreComplete;
    if (placement->rule == EEngineRule::Start) return kEngineCardScoreStart;
    return 0;
}

void EquipRuneByEnginePolicy(entt::registry& registry, std::string_view runeId) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return;
    std::optional<std::size_t> runeIndex;
    for (std::size_t index = 0; index < run->runes.size(); ++index) {
        if (run->runes[index] == runeId) {
            runeIndex = index;  // first match, exactly EquipPickedRune's lookup
            break;
        }
    }
    if (!runeIndex) {
        LOG_INFO("[autopilot] equip rune={} none: no unfitted copy", runeId);
        return;
    }
    const std::optional<FEnginePlacement> placement = ChooseEnginePlacement(registry, runeId);
    if (!placement) {
        LOG_INFO("[autopilot] equip rune={} none: no legal socket", runeId);
        return;
    }
    const std::string skillId = run->party[placement->member].skills[placement->slot].skillId;
    if (const auto error = EquipDungeonRune(registry, placement->member, placement->slot, *runeIndex)) {
        LOG_INFO("[autopilot] equip rune={} none: {}", runeId, *error);
        return;
    }
    if (placement->engineId.empty()) {
        LOG_INFO("[autopilot] equip rune={} member={} slot={} skill={} rule={}", runeId, placement->member,
                 placement->slot, skillId, EngineRuleName(placement->rule));
    } else {
        LOG_INFO("[autopilot] equip rune={} member={} slot={} skill={} rule={} engine={}", runeId, placement->member,
                 placement->slot, skillId, EngineRuleName(placement->rule), placement->engineId);
    }
}

int ConsolidateEnginesAtCamp(entt::registry& registry) {
    auto* run = registry.ctx().find<FDungeonRun>();
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (run == nullptr || content == nullptr) return 0;

    // (a) Retry every pooled rune, in pool order, through the engine rules. The pool mutates as runes fit, so
    // the attempt list is a snapshot of ids.
    const std::vector<std::string> pooled = run->runes;
    for (const std::string& runeId : pooled) {
        EquipRuneByEnginePolicy(registry, runeId);
    }

    // (b) Gather engines whose runes are all fitted but split: one move per engine, row order.
    int moves = 0;
    for (const gameplay::FEngineContent& engine : content->engines.rows) {
        const std::vector<FRuneLocation> locations = FittedLocations(*run, engine);
        if (locations.size() != engine.requiresRunes.size()) continue;  // pooled or unowned: (a) handled it
        if (AllInOneSlot(locations)) continue;                          // already whole

        bool moved = false;
        std::optional<std::string> refusal;
        for (const FMoveScan& scan : ScanEngineMoves(*content, *run, engine, locations)) {
            if (!scan.host) continue;
            if (!scan.refusal.empty()) {
                if (!refusal) refusal = scan.refusal;
                continue;
            }
            // The move: remove the rune from its slot, then fit the pool copy (always the last entry) into
            // the target host. A refused removal (its own revalidation) skips to the next candidate.
            const std::string& runeId = engine.requiresRunes[scan.runeIndex];
            if (const auto error = RemoveDungeonRune(registry, scan.from.member, scan.from.slot, scan.from.runeIndex)) {
                if (!refusal) refusal = *error;
                continue;
            }
            if (const auto error = EquipDungeonRune(registry, scan.to.member, scan.to.slot, run->runes.size() - 1)) {
                // Put the rune back so the loadout is unchanged.
                if (const auto rollback =
                        EquipDungeonRune(registry, scan.from.member, scan.from.slot, run->runes.size() - 1)) {
                    LOG_WARN("[autopilot] camp move rollback failed: {}", *rollback);
                }
                if (!refusal) refusal = *error;
                continue;
            }
            LOG_INFO("[autopilot] camp move rune={} from={}/{} to={}/{} engine={}", runeId, scan.from.member,
                     scan.from.slot, scan.to.member, scan.to.slot, engine.id);
            ++moves;
            moved = true;
            break;
        }
        if (!moved) {
            LOG_INFO("[autopilot] camp move none engine={} reason={}", engine.id,
                     refusal ? "removal refused: " + *refusal : std::string("no host"));
        }
    }
    return moves;
}

std::string_view EngineArmName(EEngineArm state) {
    switch (state) {
    case EEngineArm::Armed: return "armed";
    case EEngineArm::NotOffered: return "not-offered";
    case EEngineArm::PartnerUnfitted: return "partner-unfitted";
    case EEngineArm::MovedRefused: return "moved-refused";
    case EEngineArm::NoHost: return "no-host";
    }
    return "no-host";
}

std::vector<FEngineArmStatus> ClassifyEngines(const entt::registry& registry) {
    std::vector<FEngineArmStatus> statuses;
    const FPolicyView view = ViewOf(registry);
    if (view.run == nullptr || view.content == nullptr) return statuses;
    const FDungeonRun& run = *view.run;
    const gameplay::FContentRegistry& content = *view.content;

    for (const gameplay::FEngineContent& engine : content.engines.rows) {
        FEngineArmStatus status;
        status.engineId = engine.id;

        // Armed: a host slot holds every rune of E (CheckEngineIgnition could fire on its hit).
        for (std::size_t member = 0; member < run.party.size() && status.state != EEngineArm::Armed; ++member) {
            for (std::size_t slot = 0; slot < run.party[member].skills.size(); ++slot) {
                if (!HoldsAll(run.party[member].skills[slot], engine.requiresRunes)) continue;
                if (!IsHost(content, run.party[member], slot, engine)) continue;
                status.state = EEngineArm::Armed;
                status.member = member;
                status.slot = slot;
                break;
            }
        }
        if (status.state == EEngineArm::Armed) {
            statuses.push_back(std::move(status));
            continue;
        }

        // NotOffered: some rune of E is not owned (pool + fitted).
        bool owned = true;
        for (const std::string& runeId : engine.requiresRunes) {
            if (!IsRuneOwned(registry, runeId)) {
                owned = false;
                break;
            }
        }
        if (!owned) {
            status.state = EEngineArm::NotOffered;
            statuses.push_back(std::move(status));
            continue;
        }

        // PartnerUnfitted: a rune is pooled (a camp retry could still fit it).
        bool pooled = false;
        for (const std::string& runeId : engine.requiresRunes) {
            if (std::ranges::find(run.runes, runeId) != run.runes.end()) {
                pooled = true;
                break;
            }
        }
        if (pooled) {
            status.state = EEngineArm::PartnerUnfitted;
            statuses.push_back(std::move(status));
            continue;
        }

        // All owned and fitted: a split pair can only become whole through a camp move (b).
        const std::vector<FRuneLocation> locations = FittedLocations(run, engine);
        if (!AllInOneSlot(locations)) {
            bool candidate = false;
            bool accepted = false;
            for (const FMoveScan& scan : ScanEngineMoves(content, run, engine, locations)) {
                if (!scan.host) continue;
                candidate = true;
                if (scan.refusal.empty()) {
                    accepted = true;
                    break;
                }
            }
            if (accepted) {
                status.state = EEngineArm::PartnerUnfitted;
            } else if (candidate) {
                status.state = EEngineArm::MovedRefused;
            } else {
                status.state = EEngineArm::NoHost;
            }
        } else {
            // All in one slot, but that slot cannot host the pair (sockets/tags/OnHit).
            status.state = EEngineArm::NoHost;
        }
        statuses.push_back(std::move(status));
    }
    return statuses;
}

} // namespace game::dev
