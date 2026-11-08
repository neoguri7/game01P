#pragma once

#include "core/Logger.h"
#include "states/FBaseState.h"
#include "states/FStateCatalog.h"
#include <entt/entt.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game {

struct FAppFlowRuntimeState {
    std::string currentState;
    std::uint64_t transitionCount{0};
};

/// Result of applying one FStateTransition against the state stack.
/// Quit is reported, not performed here: the Engine owns the running flag and shutdown.
enum class EStateApplyResult {
    None,
    Applied,
    Quit,
    Failed
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
 *
 * The machine keeps one catalog id per stack frame for diagnostics and tests; it never interprets the id.
 */
class GameStateMachine {
public:
    /// One stack frame: the catalog id it was created from and the state instance.
    struct FStackEntry {
        std::string id;
        std::unique_ptr<FBaseState> state;
    };

    /// Pause the current top (kept alive) and push `newState` above it.
    void pushState(std::string id, std::unique_ptr<FBaseState> newState, entt::registry& reg) {
        if (!newState) return;
        if (!entries.empty()) entries.back().state->OnPause(reg);
        entries.push_back({std::move(id), std::move(newState)});
        entries.back().state->OnEnter(reg);
    }

    /// Exit and remove the top state; the new top resumes. A depth-1 stack may be emptied this way.
    bool popState(entt::registry& reg) {
        if (entries.empty()) return false;
        entries.back().state->OnExit(reg);
        entries.pop_back();
        if (!entries.empty()) entries.back().state->OnResume(reg);
        return true;
    }

    /// Exit and remove the top, then enter `newState` in its place (depth unchanged when non-empty).
    bool replaceTop(std::string id, std::unique_ptr<FBaseState> newState, entt::registry& reg) {
        if (!newState) return false;
        if (!entries.empty()) {
            entries.back().state->OnExit(reg);
            entries.pop_back();
        }
        entries.push_back({std::move(id), std::move(newState)});
        entries.back().state->OnEnter(reg);
        return true;
    }

    /// Exit every state top -> bottom. The stack ends empty.
    void unwindAll(entt::registry& reg) {
        while (!entries.empty()) {
            entries.back().state->OnExit(reg);
            entries.pop_back();
        }
    }

    /// Transition vocabulary alias: Root without a target means "unwind everything".
    void popToRoot(entt::registry& reg) { unwindAll(reg); }

    FStateTransition update(entt::registry& reg, float dt) {
        if (entries.empty()) return FStateTransition{};
        return entries.back().state->Update(reg, dt);
    }

    [[nodiscard]] std::string_view getCurrentStateName() const noexcept {
        return entries.empty() ? std::string_view{} : entries.back().state->GetName();
    }

    [[nodiscard]] FBaseState* currentState() noexcept {
        return entries.empty() ? nullptr : entries.back().state.get();
    }

    [[nodiscard]] const FBaseState* currentState() const noexcept {
        return entries.empty() ? nullptr : entries.back().state.get();
    }

    /// Read-only stack view, bottom -> top.
    [[nodiscard]] std::span<const FStackEntry> stack() const noexcept { return entries; }

    /// Render bottom -> top, skipping states that are neither the top nor KeepVisibleUnderModals().
    void renderStack(entt::registry& reg) {
        if (entries.empty()) return;
        const std::size_t topIndex = entries.size() - 1;
        for (std::size_t index = 0; index <= topIndex; ++index) {
            FBaseState* state = entries[index].state.get();
            if (state == nullptr) continue;
            if (index == topIndex || state->KeepVisibleUnderModals()) state->Render(reg);
        }
    }

    /// Apply one transition. The target state is created from the catalog before the stack is touched, so an
    /// unknown or empty id fails without changing anything.
    EStateApplyResult apply(const FStateTransition& transition, const FStateCatalog& catalog, entt::registry& reg) {
        const auto createTarget = [&]() -> std::unique_ptr<FBaseState> {
            if (transition.targetId.empty()) {
                LOG_ERROR("State transition has an empty target id (kind {}).", static_cast<int>(transition.kind));
                return nullptr;
            }
            auto created = catalog.create(transition.targetId);
            if (!created) {
                LOG_ERROR("State catalog has no state registered for id '{}'.", transition.targetId);
            }
            return created;
        };

        switch (transition.kind) {
        case EStateChangeKind::None:
            return EStateApplyResult::None;
        case EStateChangeKind::Quit:
            return EStateApplyResult::Quit;
        case EStateChangeKind::Push: {
            auto target = createTarget();
            if (!target) return EStateApplyResult::Failed;
            pushState(transition.targetId, std::move(target), reg);
            return EStateApplyResult::Applied;
        }
        case EStateChangeKind::Pop:
            if (entries.size() <= 1) {
                LOG_ERROR("Cannot pop the root state; the app-flow stack must keep one state.");
                return EStateApplyResult::Failed;
            }
            (void)popState(reg);
            return EStateApplyResult::Applied;
        case EStateChangeKind::ReplaceTop: {
            auto target = createTarget();
            if (!target) return EStateApplyResult::Failed;
            (void)replaceTop(transition.targetId, std::move(target), reg);
            return EStateApplyResult::Applied;
        }
        case EStateChangeKind::Root: {
            if (transition.targetId.empty()) {
                unwindAll(reg);
                return EStateApplyResult::Applied;
            }
            auto target = createTarget();
            if (!target) return EStateApplyResult::Failed;
            unwindAll(reg);
            pushState(transition.targetId, std::move(target), reg);
            return EStateApplyResult::Applied;
        }
        }
        return EStateApplyResult::None;
    }

private:
    std::vector<FStackEntry> entries;
};

} // namespace game
