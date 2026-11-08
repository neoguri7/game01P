#include "gameplay/battle/FBattleSetup.h"

#include <cstdint>
#include <optional>
#include <string>

#include "core/Logger.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBaseCombatStats.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTurnParticipant.h"
#include "gameplay/ai/FAiBrain.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"

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
    return true;
}

} // namespace game
