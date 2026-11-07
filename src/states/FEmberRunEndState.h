#pragma once

#include <string_view>

#include "FBaseState.h"
#include "gameplay/dungeon/FRunFlow.h"

/// Catalog id owned by this state (WP-02b). The runEnd modal is a real state: it draws the defeat view
/// and offers retry (back to prepare, from the checkpoint) or leave (to the title).
inline constexpr std::string_view kEmberRunEndId = game::kRunModalRunEnd;

/// Run-end stub. The view posts RetryAfterDefeat or LeaveRun; WP-19a replaces the presentation, not the
/// contract. It never pops: a run end is a screen, not a modal.
class FEmberRunEndState final : public FBaseState
{
public:
    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    void Render(entt::registry& Registry) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "RunEnd"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return {}; }
};
