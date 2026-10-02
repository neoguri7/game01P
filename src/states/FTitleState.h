#pragma once

/// High-level app-flow state for the title screen only.
/// Do not add entity gameplay state here; gameplay transitions belong in ECS
/// tag components and FEventBus-driven systems.
#include "FBaseState.h"
#include <string_view>

class FTitleState : public FBaseState
{
public:
    FTitleState() = default;
    ~FTitleState() override = default;

    EStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Title"; }
};
