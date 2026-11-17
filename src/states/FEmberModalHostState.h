#pragma once

#include <span>
#include <string>
#include <string_view>

#include "FBaseState.h"

/// Catalog ids owned by the menu hosts (WP-02b). They are not run phases: they open a modal and pop.
inline constexpr std::string_view kEmberSettingsId = "ember.settings";
inline constexpr std::string_view kEmberCompendiumId = "ember.compendium";

/// How a host leaves its screen. RunPhase hosts are a run phase's modal and may never pop; Menu hosts
/// close themselves (Cancel or the view's close button).
enum class EHostMode {
    RunPhase,
    Menu
};

/// One host class for every run-phase modal and the settings/compendium placeholders. The `id` is the
/// catalog id and the modal view id; a host is constructed with the id it was registered under.
class FEmberModalHostState final : public FBaseState
{
public:
    FEmberModalHostState(std::string id, EHostMode mode);

    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    void Render(entt::registry& Registry) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return name; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return {}; }

    std::string id;
    EHostMode mode;

private:
    std::string name;
};
