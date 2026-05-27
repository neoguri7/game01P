#pragma once

#include "states/FBaseState.h"
#include <entt/entt.hpp>
#include <memory>
#include <stack>

namespace game {

/**
 * Lightweight high-level app-flow state machine.
 *
 * Scope:
 * - allowed: title/menu/scene-flow transitions.
 * - forbidden: entity gameplay state such as idle/running/dead/attacking.
 *
 * Gameplay state must be modeled with ECS tag components and changed by systems
 * via FEventBus events. Any system that changes gameplay state tags must keep a
 * transition table near that system.
 */
class GameStateMachine {
public:
    void pushState(std::unique_ptr<FBaseState> newState, entt::registry& reg) {
        if (!stateStack.empty()) {
            stateStack.top()->OnExit();
        }
        stateStack.push(std::move(newState));
        stateStack.top()->OnEnter();
    }

    void switchState(std::unique_ptr<FBaseState> newState, entt::registry& reg) {
        if (!stateStack.empty()) {
            stateStack.top()->OnExit();
            stateStack.pop();
        }
        stateStack.push(std::move(newState));
        stateStack.top()->OnEnter();
    }

    EStateTransition update(float dt) {
        if (stateStack.empty()) return EStateTransition::None;
        return stateStack.top()->Update(dt);
    }

    void popState() {
        if (!stateStack.empty()) {
            stateStack.top()->OnExit();
            stateStack.pop();
        }
    }

private:
    std::stack<std::unique_ptr<FBaseState>> stateStack;
};

} // namespace game
