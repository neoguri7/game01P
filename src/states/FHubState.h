#pragma once

/// High-level app-flow state for hub scene flow only.
/// Do not add entity gameplay state here; gameplay transitions belong in ECS
/// tag components and FEventBus-driven systems.
#include "FBaseState.h"
#include <string_view>

class FHubState : public FBaseState
{
public:
    FHubState() = default;
    ~FHubState() override = default;

    EStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Hub"; }
    void OnEnter(entt::registry& Registry) override;
    void OnExit(entt::registry& Registry) override;
};
