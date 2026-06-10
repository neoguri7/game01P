#include "debug/DemoBootstrap.h"

#include "core/factories/FDemoEntityFactory.h"

namespace game {

void BootstrapDemoScene(entt::registry& registry) {
    FDemoEntityFactory::create(registry);
}

entt::entity CreateDebugDemoEntity(entt::registry& registry) {
    return FDemoEntityFactory::create(registry);
}

} // namespace game
