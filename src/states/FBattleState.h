#pragma once

#include <memory>
#include <span>
#include "FBaseState.h"
#include <string_view>
#include "gameplay/factory/FScene.h"

class FBattleState final : public FBaseState
{
public:
    EStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Battle (grid)"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override;
    void OnEnter(entt::registry& Registry) override;
    void OnExit(entt::registry& Registry) override;

private:
    std::unique_ptr<game::FScene> scene;
    bool battleReady{false};
};
