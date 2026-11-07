#pragma once

/// High-level app-flow state for the title screen only.
/// Do not add entity gameplay state here; gameplay transitions belong in ECS
/// tag components and FEventBus-driven systems.
#include "FBaseState.h"
#include <span>
#include <string_view>

/// Catalog id owned by this state (WP-01). FStateCatalog registers under this name; nobody assembles ids.
inline constexpr std::string_view kForestTitleId = "forest.title";

class FTitleState : public FBaseState
{
public:
    FTitleState() = default;
    ~FTitleState() override = default;

    EStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    void Render(entt::registry& Registry) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Title"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override;
private:
    EStateTransition requested{EStateTransition::None};
};
