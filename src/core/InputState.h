#pragma once
#include <SDL3/SDL_events.h>
#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <array>

namespace game {

/// Centralised input state — lives in registry.ctx() (no singleton).
/// Updated once per frame by Engine::processInput(); queried by systems.
struct FInputState {
    // Mouse
    glm::vec2 mousePos{0.f, 0.f};
    bool      mouseLeftPressed{false};
    bool      mouseLeftHeld{false};

    // Keyboard
    std::array<bool, 256> keysHeld{};
    std::array<bool, 256> keysJustPressed{};

    /// True while this frame the key was pressed (consumed once per press).
    [[nodiscard]] bool isKeyDown(int scancode) const {
        return keysJustPressed[scancode & 0xFF];
    }

    [[nodiscard]] bool isKeyHeld(int scancode) const {
        return keysHeld[scancode & 0xFF];
    }

    // Called from Engine each frame
    void beginFrame() {
        mouseLeftPressed = false;
        for (auto& b : keysJustPressed) b = false;
    }

    bool processEvent(const SDL_Event& ev) {
        switch (ev.type) {
        case SDL_EVENT_MOUSE_MOTION:
            mousePos = { ev.motion.x, ev.motion.y };
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                mouseLeftPressed = true;
                mouseLeftHeld    = true;
            }
            return true;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.button == SDL_BUTTON_LEFT) mouseLeftHeld = false;
            return true;
        case SDL_EVENT_KEY_DOWN: {
            int idx = ev.key.scancode & 0xFF;
            keysHeld[idx] = true;
            // Only mark just-pressed if not already held (av repeating)
            if (!keysJustPressed[idx]) keysJustPressed[idx] = true;
            return true;
        }
        case SDL_EVENT_KEY_UP:
            keysHeld[ev.key.scancode & 0xFF] = false;
            return true;
        default:
            return false;
        }
    }

    static void Initialize(entt::registry& reg) {
        if (!reg.ctx().contains<FInputState>()) {
            reg.ctx().emplace<FInputState>();
        }
    }
};

} // namespace game
