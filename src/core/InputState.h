#pragma once
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <array>

namespace game {

enum class EInputAction {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Confirm,
    Cancel,
    DebugToggle,
    EndTurn,
    Skill1,
    Skill2,
    Skill3,
    Skill4
};

/// Centralised input state — lives in registry.ctx() (no singleton).
/// Updated once per frame by Engine::processInput(); queried by systems.
/// Gameplay systems should query actions, not raw SDL scancodes.
/// Key repeat is ignored for just-pressed actions; held actions remain true.
/// When UI captures input, gameplay actions are suppressed for that frame.
struct FInputState {
    // Mouse
    glm::vec2 mousePos{0.f, 0.f};
    float     mouseWheelDelta{0.f};
    bool      mouseLeftPressed{false};
    bool      mouseLeftHeld{false};
    bool      uiCapturesKeyboard{false};
    bool      uiCapturesMouse{false};

    // Keyboard
    std::array<bool, 256> keysHeld{};
    std::array<bool, 256> keysJustPressed{};

    [[nodiscard]] bool isActionPressed(EInputAction action) const {
        if (uiCapturesKeyboard) return false;
        return keysJustPressed[ActionScancode(action) & 0xFF];
    }

    [[nodiscard]] bool isActionHeld(EInputAction action) const {
        if (uiCapturesKeyboard) return false;
        return keysHeld[ActionScancode(action) & 0xFF];
    }

    // Called from Engine each frame
    void beginFrame(bool keyboardCapturedByUi = false, bool mouseCapturedByUi = false) {
        uiCapturesKeyboard = keyboardCapturedByUi;
        uiCapturesMouse = mouseCapturedByUi;
        mouseWheelDelta = 0.f;
        mouseLeftPressed = false;
        for (auto& b : keysJustPressed) b = false;
    }

    bool processEvent(const SDL_Event& ev) {
        switch (ev.type) {
        case SDL_EVENT_MOUSE_MOTION:
            mousePos = { ev.motion.x, ev.motion.y };
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            if (!uiCapturesMouse) mouseWheelDelta += ev.wheel.y;
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            mousePos = { ev.button.x, ev.button.y };
            if (uiCapturesMouse) return false;
            if (ev.button.button == SDL_BUTTON_LEFT) {
                mouseLeftPressed = true;
                mouseLeftHeld    = true;
            }
            return true;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            mousePos = { ev.button.x, ev.button.y };
            if (ev.button.button == SDL_BUTTON_LEFT) mouseLeftHeld = false;
            return true;
        case SDL_EVENT_KEY_DOWN: {
            int idx = ev.key.scancode & 0xFF;
            keysHeld[idx] = true;
            if (!uiCapturesKeyboard && !ev.key.repeat) keysJustPressed[idx] = true;
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

private:
    static int ActionScancode(EInputAction action) {
        switch (action) {
        case EInputAction::MoveUp: return SDL_SCANCODE_W;
        case EInputAction::MoveDown: return SDL_SCANCODE_S;
        case EInputAction::MoveLeft: return SDL_SCANCODE_A;
        case EInputAction::MoveRight: return SDL_SCANCODE_D;
        case EInputAction::Confirm: return SDL_SCANCODE_RETURN;
        case EInputAction::Cancel: return SDL_SCANCODE_ESCAPE;
        case EInputAction::DebugToggle: return SDL_SCANCODE_GRAVE;
        case EInputAction::EndTurn: return SDL_SCANCODE_SPACE;
        case EInputAction::Skill1: return SDL_SCANCODE_1;
        case EInputAction::Skill2: return SDL_SCANCODE_2;
        case EInputAction::Skill3: return SDL_SCANCODE_3;
        case EInputAction::Skill4: return SDL_SCANCODE_4;
        }
        return SDL_SCANCODE_UNKNOWN;
    }
};

} // namespace game
