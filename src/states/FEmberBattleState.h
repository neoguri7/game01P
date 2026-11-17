#pragma once

#include <memory>
#include <span>
#include "FBaseState.h"
#include <string_view>
#include "gameplay/factory/FScene.h"

/// Catalog id owned by this state (WP-01). FStateCatalog registers under this name; nobody assembles ids.
inline constexpr std::string_view kEmberBattleId = "ember.battle";

namespace game {
/// True while the battle owns the screen: a run exists and its phase is Battle. The leave/abandon
/// confirm is the battle's only then; a run-phase modal above the battle owns the screen.
[[nodiscard]] bool BattleOwnsScreen(const entt::registry& registry);
} // namespace game

class FEmberBattleState final : public FBaseState
{
public:
    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    void Render(entt::registry& Registry) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Battle (grid)"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override;
    /// A modal pushed above the battle must keep the field on screen while the simulation stops (WP-01 §2).
    [[nodiscard]] bool KeepVisibleUnderModals() const override { return true; }
    [[nodiscard]] std::span<const std::string_view> RequiredRenderSystems() const noexcept override;
    void OnEnter(entt::registry& Registry) override;
    void OnExit(entt::registry& Registry) override;

private:
    std::unique_ptr<game::FScene> scene;
    bool battleReady{false};
};
