#include "debug/DemoBootstrap.h"

#include "core/factories/FDemoEntityFactory.h"
#include "core/factories/FDemoObstacleFactory.h"
#include "ecs/components/FDemoShowcaseEntity.h"

#include <vector>

namespace game {

namespace {

void DestroyDemoShowcaseEntities(entt::registry& registry) {
    std::vector<entt::entity> entities;
    auto view = registry.view<FDemoShowcaseEntity>();
    entities.assign(view.begin(), view.end());

    for (auto entity : entities) {
        registry.destroy(entity);
    }
}

} // namespace

void BootstrapDemoScene(entt::registry& registry) {
    FDemoObstacleDesc cover{};
    cover.tag = "cover_factory_obstacle";
    cover.x = 360.f;
    cover.y = 260.f;
    cover.gridX = 4;
    cover.gridY = 3;
    FDemoObstacleFactory::create(registry, cover);

    FDemoObstacleDesc laneMarker{};
    laneMarker.tag = "low_layer_grid_marker";
    laneMarker.x = 120.f;
    laneMarker.y = 360.f;
    laneMarker.width = 420.f;
    laneMarker.height = 18.f;
    laneMarker.layer = 1;
    laneMarker.gridX = 1;
    laneMarker.gridY = 5;
    laneMarker.fillR = 60;
    laneMarker.fillG = 120;
    laneMarker.fillB = 80;
    laneMarker.fillA = 120;
    FDemoObstacleFactory::create(registry, laneMarker);

    FDemoEntityDesc scout{};
    scout.tag = "factory_moving_scout";
    scout.x = 120.f;
    scout.y = 160.f;
    scout.vx = 35.f;
    scout.vy = 0.f;
    scout.gridX = 1;
    scout.gridY = 2;
    scout.commandId = "move_to_cover";
    scout.commandName = "Move To Cover";
    scout.fillR = 80;
    scout.fillG = 160;
    scout.fillB = 240;
    FDemoEntityFactory::create(registry, scout);

    FDemoEntityDesc collisionProbe{};
    collisionProbe.tag = "eventbus_collision_probe";
    collisionProbe.x = 388.f;
    collisionProbe.y = 278.f;
    collisionProbe.vx = 0.f;
    collisionProbe.vy = 0.f;
    collisionProbe.gridX = 4;
    collisionProbe.gridY = 3;
    collisionProbe.commandId = "hold";
    collisionProbe.commandName = "Hold Position";
    collisionProbe.fillR = 240;
    collisionProbe.fillG = 180;
    collisionProbe.fillB = 70;
    FDemoEntityFactory::create(registry, collisionProbe);

    FDemoEntityDesc defeated{};
    defeated.tag = "state_tag_defeated_unit";
    defeated.x = 520.f;
    defeated.y = 160.f;
    defeated.vx = 0.f;
    defeated.vy = 0.f;
    defeated.gridX = 6;
    defeated.gridY = 2;
    defeated.commandId = "none";
    defeated.commandName = "Defeated State";
    defeated.fillR = 210;
    defeated.fillG = 80;
    defeated.fillB = 90;
    defeated.defeated = true;
    FDemoEntityFactory::create(registry, defeated);
}

entt::entity CreateDebugDemoEntity(entt::registry& registry) {
    return FDemoEntityFactory::create(registry);
}

void ResetDemoScene(entt::registry& registry) {
    DestroyDemoShowcaseEntities(registry);
    BootstrapDemoScene(registry);
}

} // namespace game
