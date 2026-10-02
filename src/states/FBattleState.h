#pragma once

#include "FBaseState.h"
#include <string_view>

class FBattleState final : public FBaseState
{
public:
    EStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Battle (dummy)"; }
};
