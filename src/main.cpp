#include "core/Engine.h"
#include <cstdlib>

int main(int argc, char* argv[])
{
    game::Engine engine;

    if (!engine.initialize("Game01P - ECS Prototype", 1280, 720)) {
        return EXIT_FAILURE;
    }

    engine.run();

    engine.shutdown();
    return EXIT_SUCCESS;
}
