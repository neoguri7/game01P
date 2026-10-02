#pragma once

#include "states/FBaseState.h"
#include <entt/entt.hpp>
#include <memory>
#include <stack>
#include <cstdint>
#include <string>

namespace game {

struct FAppFlowRuntimeState {
    std::string currentState;
    std::uint64_t transitionCount{0};
};

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
            stateStack.top()->OnExit(reg);
        }
        stateStack.push(std::move(newState));
        stateStack.top()->OnEnter(reg);
    }

    void switchState(std::unique_ptr<FBaseState> newState, entt::registry& reg) {
        if (!stateStack.empty()) {
            stateStack.top()->OnExit(reg);
            stateStack.pop();
        }
        stateStack.push(std::move(newState));
        stateStack.top()->OnEnter(reg);
    }

    EStateTransition update(entt::registry& reg, float dt) {
        if (stateStack.empty()) return EStateTransition::None;
        return stateStack.top()->Update(reg, dt);
    }

    [[nodiscard]] std::string_view getCurrentStateName() const noexcept {
        return stateStack.empty() ? std::string_view{} : stateStack.top()->GetName();
    }

    void popState(entt::registry& reg) {
        if (!stateStack.empty()) {
            stateStack.top()->OnExit(reg);
            stateStack.pop();
        }
    }

private:
    std::stack<std::unique_ptr<FBaseState>> stateStack;
};

} // namespace game
