#pragma once
#include <thread>

namespace game::core
{
    // V18 [R18]: asset/audio work moved off the main thread with no affinity declaration.
    struct ZzAsyncLoader  // NOLINT(readability-identifier-naming): deliberate fixture name
    {
        void loadInBackground()  // NOLINT(readability-convert-member-functions-to-static)
        {
            std::thread worker(
                []
                {
                    // C000: touches the SDL renderer + entt registry from a worker thread.
                });
            worker.detach();
        }
    };
}
