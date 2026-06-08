#include "debug/DemoBootstrap.h"

#include "core/factories/FDemoEntityFactory.h"
#include "core/factories/FTacticalCombatStateFactory.h"
#include "gameplay/tactical_d20/FTacticalCombatConfigLoader.h"

namespace game {

void BootstrapDemoScene(entt::registry& registry) {
    FTacticalCombatConfigLoader::Initialize(registry);
    FTacticalCombatStateFactory::createSetupState(registry);
}

entt::entity CreateDebugDemoEntity(entt::registry& registry) {
    return FDemoEntityFactory::create(registry);
}

} // namespace game
