#include "gameplay/battle/FBattleSetup.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Logger.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBaseCombatStats.h"
#include "ecs/components/FBossGoals.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTurnParticipant.h"
#include "gameplay/ai/FAiBrain.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonRun.h"

namespace game {
namespace {

/// WP-22: the fallback loadout is capped to four skills — the HUD hotkeys, the 4-slot bar and the forest
/// skill_slots are all 4 (W7 review, 4-slot hard limit). units.json `skills` is the authored POOL; the
/// first four entries in pool order are the default loadout. The placement.skills override (prep screen,
/// W5 test injections, captured run loadouts) is deliberately NOT capped here — it carries the run-scope
/// loadout exactly as authored.
inline constexpr std::size_t kDefaultLoadoutSlots = 4;

[[nodiscard]] FSkillLoadout BuildLoadout(
    const FUnitPlacement& placement, const gameplay::FUnitContent& unit) {
    FSkillLoadout loadout;
    if (!placement.skills.empty()) {
        loadout.slots = placement.skills;
        return loadout;
    }
    for (const std::string& skillId : unit.skillIds) {
        if (loadout.slots.size() >= kDefaultLoadoutSlots) {
            break;
        }
        loadout.slots.push_back(FSkillSlot{skillId, {}});
    }
    return loadout;
}

/// WP-15: attaches the boss goals (body phases, part links) to a battlefield that the generator filled from a
/// mapped bosses.json entry. Parts themselves are ordinary placements — the room document creates them, this
/// block only links them (entities can never be added here).
void AttachBossGoals(entt::registry& registry, const FBattlefieldData& battlefield) {
    // Only a boss-slot dungeon battle has both the run and the marker; forest battles and validation probes
    // (SetupCombatUnits on a throwaway registry) are a no-op.
    if (!registry.ctx().contains<FDungeonRun>()) return;
    const auto* battle = registry.ctx().find<FBossBattle>();
    if (battle == nullptr) return;
    const FBossCatalog* catalog = FindBossCatalog(registry);
    if (catalog == nullptr) return;
    const FBossDef* def = catalog->Find(battle->bossId);
    if (def == nullptr) {
        // A fallback boss (unmapped slot): single phase, no parts, no gate — the pre-WP-15 guardian path.
        return;
    }

    // The body is the first enemy placement of the boss unit (the generator puts the boss before its escorts
    // and parts).
    entt::entity body = entt::null;
    for (const FUnitPlacement& placement : battlefield.units) {
        if (placement.faction == EUnitFaction::Enemy && placement.unitTemplateId == def->unit) {
            body = placement.entity;
            break;
        }
    }
    if (body == entt::null) {
        LOG_WARN("battle: boss '{}' has no placed body for unit '{}'; goals are not attached.",
                 def->id, def->unit);
        return;
    }

    FBossGoals goals;
    goals.bossId = def->id;
    goals.name = def->name;
    goals.phases.reserve(def->phases.size());
    for (const FBossPhaseDef& phase : def->phases) {
        goals.phases.push_back(
            FBossPhase{phase.hpAbove, phase.partsGate, phase.bodyImmunities, phase.notes});
    }
    // WP-15b: the first phase is in force from the first frame (only transitions are telegraphed), so the
    // body starts with phase 0's immune set. EvaluateBossGoals re-writes it on its first evaluation, which
    // also catches a body that starts in a later live phase.
    if (auto* active = registry.try_get<FActiveStatuses>(body); active != nullptr && !goals.phases.empty()) {
        active->immune = goals.phases.front().bodyImmunities;
    }
    registry.emplace_or_replace<FBossGoals>(body, std::move(goals));

    // Parts: each row claims the LAST `count` enemy placements of its unit (walking the placements backwards)
    // that are neither the body nor already claimed by this boss. The generator appends parts after the
    // escorts, so a part unit that also appears in the escort pool is still claimed correctly.
    std::vector<bool> claimed(battlefield.units.size(), false);
    int partCount = 0;
    for (const FBossPartDef& row : def->parts) {
        int remaining = row.count;
        for (std::size_t i = battlefield.units.size(); i > 0 && remaining > 0; --i) {
            const FUnitPlacement& placement = battlefield.units[i - 1];
            if (placement.faction != EUnitFaction::Enemy) continue;
            if (placement.unitTemplateId != row.unit) continue;
            if (placement.entity == body || claimed[i - 1]) continue;
            claimed[i - 1] = true;
            registry.emplace_or_replace<FBossPart>(placement.entity, FBossPart{body});
            ++partCount;
            --remaining;
        }
    }

    LOG_INFO("[boss] {} goals attached: {} phase(s), {} part(s)", def->id, def->phases.size(), partCount);
}

} // namespace

bool SetupCombatUnits(
    entt::registry& registry,
    const FBattlefieldData& battlefield,
    const gameplay::FContentRegistry& content) {
    for (const FUnitPlacement& placement : battlefield.units) {
        if (!registry.valid(placement.entity)) {
            LOG_ERROR("battle: placement '{}' has no entity handle.", placement.id);
            return false;
        }

        const gameplay::FUnitContent* unit = content.units.find(placement.unitTemplateId);
        if (unit == nullptr) {
            LOG_ERROR("battle: placement '{}' references unknown unit '{}'.",
                      placement.id, placement.unitTemplateId);
            return false;
        }

        const FSkillLoadout loadout = BuildLoadout(placement, *unit);
        for (const FSkillSlot& slot : loadout.slots) {
            if (content.skills.find(slot.skillId) == nullptr) {
                LOG_ERROR("battle: placement '{}' references unknown skill '{}'.",
                          placement.id, slot.skillId);
                return false;
            }
            // Rune limits and tag rules are part of the build rules
            // (docs/design/skill-rune-progression.md §3), so an illegal authored loadout stops the battle
            // here instead of losing runes silently mid-fight.
            if (const std::optional<std::string> problem = ValidateSkillSlot(slot, content)) {
                LOG_ERROR("battle: placement '{}' has an invalid rune loadout for skill '{}': {}.",
                          placement.id, slot.skillId, *problem);
                return false;
            }
        }

        const int maxHealth = static_cast<int>(unit->maxHealth);
        const FStats stats{unit->attack, unit->magicAttack, unit->defense, unit->magicDefense};
        // 불변 베이스(WP-03): 파생 갱신(RefreshCombatSheet)이 매번 여기서부터 재계산한다. 초기값은 아래
        // FStats/FHealth/FSpeed와 동일 — 동작 불변. 여기서 갱신을 호출하지 않는다: forest는 인라인 패시브
        // 가산(FBattleState.cpp)이 뒤에 오므로 순서가 겹치면 이중 가산이 된다.
        registry.emplace<FBaseCombatStats>(
            placement.entity, FBaseCombatStats{stats, maxHealth, static_cast<int>(unit->speed)});
        registry.emplace<FStats>(placement.entity, stats);
        registry.emplace<FHealth>(placement.entity, FHealth{maxHealth, maxHealth});
        registry.emplace<FActionPoints>(placement.entity,
                                        FActionPoints{unit->moveAp, unit->moveAp, unit->skillAp, unit->skillAp});
        registry.emplace<FSpeed>(placement.entity, static_cast<std::int32_t>(unit->speed));
        registry.emplace<FSkillLoadout>(placement.entity, loadout);
        registry.emplace<FActiveStatuses>(placement.entity);
        registry.emplace<FTurnParticipant>(placement.entity);

        // The unit's mind: its authored profile, or the default one that plays any loadout from skill roles.
        // Registries without an AI library (validation probes) get a brain lazily, on first use.
        if (const auto* library = registry.ctx().find<ai::FAiLibrary>()) {
            const ai::FAiProfile* profile =
                unit->aiProfile.empty() ? &library->fallback() : library->find(unit->aiProfile);
            if (profile == nullptr) {
                LOG_ERROR("battle: unit '{}' references unknown AI profile '{}'.", unit->id, unit->aiProfile);
                return false;
            }
            registry.emplace_or_replace<ai::FAiBrain>(placement.entity, ai::MakeBrain(*profile));
        }
    }

    // WP-15: goals/parts after the last placement (the whole battlefield is known, so part claiming can walk
    // it backwards).
    AttachBossGoals(registry, battlefield);
    return true;
}

} // namespace game
