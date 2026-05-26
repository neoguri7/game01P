#include "debug/DemoBootstrap.h"

#include "core/factories/FDemoEntityFactory.h"
#include "core/factories/FDemoPlayerFactory.h"

namespace game {

void BootstrapDemoScene(entt::registry& registry) {
    FDemoPlayerFactory::create(registry);
}

entt::entity CreateDebugDemoEntity(entt::registry& registry) {
    return FDemoEntityFactory::create(registry);
}

} // namespace game
