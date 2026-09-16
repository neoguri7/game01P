#pragma once

// V36 [R26, gameplay coverage]: SDL3 must not appear in gameplay/ — drawing belongs behind the core/ renderer
// services. The fixture exists because R26's target list used to stop at ecs/states/debug.
namespace game::gameplay {

struct ZzSdlGameplayProbe {
    SDL_Window* window{nullptr};
};

} // namespace game::gameplay
