#pragma once

#include <chrono>
#include <SDL3/SDL.h>

namespace game {

class Time {
public:
    Time() : lastFrameTime{SDL_GetPerformanceCounter()}, deltaTime{0.f}, fpsTimer{0.f}, frameCounter{0} {}

    float updateDeltaTime() {
        using namespace std::chrono;

        const Uint64 now = SDL_GetPerformanceCounter();
        const Uint64 freq = SDL_GetPerformanceFrequency();

        const double elapsed = static_cast<double>(now - lastFrameTime) / static_cast<double>(freq);
        lastFrameTime = now;

        deltaTime = static_cast<float>(elapsed);

        // FPS tracking
        fpsTimer += deltaTime;
        ++frameCounter;
        if (fpsTimer >= 1.0f) {
            currentFps = frameCounter;
            frameCounter = 0;
            fpsTimer = 0.f;
        }

        return deltaTime;
    }

    [[nodiscard]] float getDeltaTime() const noexcept { return deltaTime; }
    [[nodiscard]] int   getFps() const noexcept { return currentFps; }

private:
    Uint64 lastFrameTime;
    float  deltaTime;
    float  fpsTimer;
    int    frameCounter;
    int    currentFps = 0;
};

} // namespace game
