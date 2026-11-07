#pragma once

#include <entt/entt.hpp>
#include <span>
#include <string>
#include <string_view>

/// How an app-flow transition changes the state stack.
/// The target is a catalog id; the core never interprets it (FStateCatalog owns id -> factory).
enum class EStateChangeKind
{
    None,
    Push,        // pause the current top (simulation stops, rendering stays) and push targetId
    Pop,         // exit and remove the top; the new top resumes
    ReplaceTop,  // exit the top, then push targetId in its place (depth unchanged — modal-to-modal)
    Root,        // unwind the whole stack top -> bottom, then push targetId (depth 1); empty id = unwind only
    Quit
};

struct FStateTransition
{
    EStateChangeKind kind{EStateChangeKind::None};
    std::string targetId;    // catalog id, e.g. "ember.reward" — core never interprets it

    /// kind + id in one expression (tests, prototype checks).
    friend bool operator==(const FStateTransition&, const FStateTransition&) = default;
};

/// Base interface for high-level app-flow states only (title, menus, scene flow).
/// Gameplay state such as alive/dead/attacking/stunned belongs in ECS tags and
/// event-driven systems, not in this polymorphic state stack.
class FBaseState
{
public:
    virtual ~FBaseState() = default;

    /// Per-frame logic. Returns transition intent.
    virtual FStateTransition Update(entt::registry& Registry, float DeltaTime) = 0;
    virtual void Render(entt::registry& Registry) {}

    /// Human-readable app-flow state name for diagnostics.
    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;

    [[nodiscard]] virtual std::span<const std::string_view> RequiredSystems() const noexcept { return {}; }

    /// Keep this state's screen visible while a modal is pushed above it (A1: paused, not exited).
    [[nodiscard]] virtual bool KeepVisibleUnderModals() const { return false; }

    /// Render-only systems (update stays off) to keep drawing while this state is paused under a modal.
    [[nodiscard]] virtual std::span<const std::string_view> RequiredRenderSystems() const noexcept { return {}; }

    /// Called when this state becomes active.
    virtual void OnEnter(entt::registry& Registry) {}

    /// Called when this state is being replaced.
    virtual void OnExit(entt::registry& Registry) {}

    /// Called when another state is pushed above this one. The state stays alive (no destruction).
    virtual void OnPause(entt::registry& Registry) {}

    /// Called when the state above this one is popped. The state becomes the active top again.
    virtual void OnResume(entt::registry& Registry) {}
};
