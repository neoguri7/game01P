#pragma once

#include <memory>
#include <span>
#include "FBaseState.h"
#include <string_view>
#include "gameplay/factory/FScene.h"

/// Catalog id owned by this state (WP-01). FStateCatalog registers under this name; nobody assembles ids.
inline constexpr std::string_view kForestBattleId = "forest.battle";

class FBattleState final : public FBaseState
{
public:
    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Battle (grid)"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override;
    void OnEnter(entt::registry& Registry) override;
    void OnExit(entt::registry& Registry) override;

private:
    std::unique_ptr<game::FScene> scene;
    bool battleReady{false};
};
