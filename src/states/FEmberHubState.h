#pragma once

/// High-level app-flow state for hub scene flow only.
/// Do not add entity gameplay state here; gameplay transitions belong in ECS
/// tag components and FEventBus-driven systems.
#include "FBaseState.h"
#include <string_view>

/// Catalog id owned by this state (WP-01). FStateCatalog registers under this name; nobody assembles ids.
inline constexpr std::string_view kEmberHubId = "ember.hub";

class FEmberHubState : public FBaseState
{
public:
    FEmberHubState() = default;
    ~FEmberHubState() override = default;

    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Hub"; }
    void OnEnter(entt::registry& Registry) override;
    void OnExit(entt::registry& Registry) override;
};
