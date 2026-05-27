#include "core/factories/FTacticalD20UnitFactory.h"

#include "ecs/components/FAbilityScores.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FText.h"
#include "ecs/components/FTacticalUnit.h"

#include <fmt/format.h>

namespace game {

entt::entity FTacticalD20UnitFactory::create(entt::registry& registry, const FTacticalD20UnitSpawn& spawn) {
    constexpr float tileSize = 64.f;
    constexpr float originX = 64.f;
    constexpr float originY = 64.f;

    auto entity = registry.create();
    registry.emplace<FTacticalUnit>(entity,
        spawn.id,
        spawn.team,
        spawn.displayName,
        spawn.tileX,
        spawn.tileY,
        spawn.maxHp,
        spawn.maxHp,
        spawn.armorClass,
        spawn.speedFeet,
        spawn.weaponProficiencies,
        spawn.savingThrowProficiencies,
        spawn.skillProficiencies,
        spawn.actions);
    registry.emplace<FAbilityScores>(entity, spawn.abilities);
    registry.emplace<FPosition>(entity, originX + static_cast<float>(spawn.tileX) * tileSize, originY + static_cast<float>(spawn.tileY) * tileSize);
    registry.emplace<FCollider>(entity, EColliderType::AABB, glm::vec2{0.f, 0.f}, 24.f, 24.f, 4, spawn.team);
    registry.emplace<FTag>(entity, spawn.id);
    registry.emplace<FLayer>(entity, spawn.team == "player" ? 10 : 11);
    registry.emplace<FText>(entity, fmt::format("{} HP {}/{} Conditions: -", spawn.displayName, spawn.maxHp, spawn.maxHp), "", 14, 255, 255, 255, 255);
    return entity;
}

} // namespace game