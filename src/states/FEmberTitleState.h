#pragma once

/// High-level app-flow state for the title screen only.
/// Do not add entity gameplay state here; gameplay transitions belong in ECS
/// tag components and FEventBus-driven systems.
#include "FBaseState.h"
#include <string_view>

class FEmberTitleState : public FBaseState
{
public:
    FEmberTitleState() = default;
    ~FEmberTitleState() override = default;

    EStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Title"; }
};
