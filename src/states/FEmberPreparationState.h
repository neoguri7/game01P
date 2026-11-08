#pragma once

#include <string_view>

#include "FBaseState.h"

/// Catalog id owned by this state (WP-02b). The title/runEnd carrier is consumed here exactly once.
inline constexpr std::string_view kEmberPrepareId = "ember.prepare";

/// Preparation stub. It is the only new-run path: it consumes FRunStartRequest and starts the run once.
/// WP-21 replaces this stub with the party/loadout screen; the carrier contract stays.
class FEmberPreparationState final : public FBaseState
{
public:
    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Prepare"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return {}; }
};
