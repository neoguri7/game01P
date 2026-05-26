#include "core/Engine.h"
#include "debug/DebugOverlay.h"
#include "debug/DemoBootstrap.h"
#include "gameplay/SystemRegistration.h"
#include <cstdlib>

int main(int argc, char* argv[])
{
    game::Engine engine;

    if (!engine.initialize("Game01P - ECS Prototype", 1280, 720)) {
        return EXIT_FAILURE;
    }

    game::RegisterDefaultSystems(engine.getSystemManager(), engine.getRegistry());
    game::BootstrapDemoScene(engine.getRegistry());
    engine.setOverlayRenderer(game::RenderDebugOverlay);

    engine.run();

    engine.shutdown();
    return EXIT_SUCCESS;
}
