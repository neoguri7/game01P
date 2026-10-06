#include "gameplay/battle/FBattleSetup.h"

#include <cstdint>
#include <string>

#include "core/Logger.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTurnParticipant.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {
namespace {

[[nodiscard]] FSkillLoadout BuildLoadout(
    const FUnitPlacement& placement, const gameplay::FUnitContent& unit) {
    FSkillLoadout loadout;
    if (!placement.skills.empty()) {
        loadout.slots = placement.skills;
        return loadout;
    }
    for (const std::string& skillId : unit.skillIds) {
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
        }

        const int maxHealth = static_cast<int>(unit->maxHealth);
        registry.emplace<FStats>(placement.entity, FStats{unit->attack});
        registry.emplace<FHealth>(placement.entity, FHealth{maxHealth, maxHealth});
        registry.emplace<FActionPoints>(placement.entity,
                                        FActionPoints{unit->moveAp, unit->moveAp, unit->skillAp, unit->skillAp});
        registry.emplace<FSpeed>(placement.entity, static_cast<std::int32_t>(unit->speed));
        registry.emplace<FSkillLoadout>(placement.entity, loadout);
        registry.emplace<FActiveStatuses>(placement.entity);
        registry.emplace<FTurnParticipant>(placement.entity);
    }
    return true;
}

} // namespace game
