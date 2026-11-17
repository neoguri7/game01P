#pragma once

#include <string_view>

#include "FBaseState.h"
#include "presentation/FPreparationView.h"

/// Catalog id owned by this state (WP-02b). The title/runEnd carrier is consumed here exactly once.
inline constexpr std::string_view kEmberPrepareId = "ember.prepare";

/// WP-21b: the preparation screen — the hub (Expedition · Unlocks · Records). It consumes the
/// FRunStartRequest carrier once, builds the default draft from the roster, and starts the run only through
/// StartDungeonRunFromSetup (the single setup-driven new-run path) when the screen confirms. The catalog id
/// and its registration are unchanged from the WP-02 stub; the screen is not saved state.
class FEmberPreparationState final : public FBaseState
{
public:
    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    void Render(entt::registry& Registry) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Prepare"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return {}; }

private:
    game::FPreparationScreen screen;   // the draft the view edits; the state owns and validates it every frame
    bool initialized{false};           // the carrier was consumed and the default draft was built
};
