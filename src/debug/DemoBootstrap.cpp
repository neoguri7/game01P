#include "debug/DemoBootstrap.h"

#include "core/factories/FDemoEntityFactory.h"
#include "core/factories/FTacticalD20CombatStateFactory.h"
#include "gameplay/tactical_d20/FTacticalD20ConfigLoader.h"

namespace game {

void BootstrapDemoScene(entt::registry& registry) {
    FTacticalD20ConfigLoader::Initialize(registry);
    FTacticalD20CombatStateFactory::createSetupState(registry);
}

entt::entity CreateDebugDemoEntity(entt::registry& registry) {
    return FDemoEntityFactory::create(registry);
}

} // namespace game
