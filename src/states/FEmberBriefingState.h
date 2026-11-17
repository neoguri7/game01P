#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "FBaseState.h"
#include "gameplay/dungeon/FRunBriefing.h"

/// Catalog id owned by this state (WP-50). The music director of WP-51 maps this literal id.
inline constexpr std::string_view kEmberBriefingId = "ember.briefing";

/// WP-50: the expedition briefing shown before the first room of every dungeon entered by Start or Descend.
/// It changes no run state; its only exit is Enter (the button's command or the Confirm key).
class FEmberBriefingState final : public FBaseState
{
public:
    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    void Render(entt::registry& Registry) override;
    void OnEnter(entt::registry& Registry) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Briefing"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return {}; }

private:
    std::optional<game::FRunBriefing> briefing;  // built once in OnEnter (the run does not change on this screen)
};
