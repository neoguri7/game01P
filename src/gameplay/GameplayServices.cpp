#include "gameplay/GameplayServices.h"

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "gameplay/ai/FAiLibrary.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FRewardSection.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FSaveSections.h"

#include <tracy/Tracy.hpp>

#include <optional>
#include <utility>

namespace game::gameplay {

bool FGameplayServices::Initialize(entt::registry& registry) {
    ZoneScopedN("FGameplayServices::Initialize");

    // The asset boundary is initialized by Engine (core/), so gameplay only consumes it (R21). A missing
    // service is a wiring bug, not a content bug, and must not be papered over with a default table.
    const FAssetManager* assets = registry.ctx().find<FAssetManager>();
    if (assets == nullptr) {
        LOG_ERROR("gameplay services need the asset boundary: FAssetManager is missing from the registry context.");
        return false;
    }

    std::optional<FContentRegistry> content = FContentRegistry::load(*assets);
    if (!content) {
        LOG_ERROR("gameplay content is unusable; boot stopped (see the content errors above).");
        return false;
    }

    // Behavior trees reference skills, statuses and units, so they load (and are validated) after content.
    std::optional<ai::FAiLibrary> library = ai::FAiLibrary::load(*assets, *content);
    if (!library) {
        LOG_ERROR("gameplay AI profiles are unusable; boot stopped (see the ai errors above).");
        return false;
    }

    registry.ctx().emplace<FContentRegistry>(std::move(*content));
    registry.ctx().emplace<ai::FAiLibrary>(std::move(*library));

    // Later work packages add one registration line directly under their own anchor; registration happens
    // only here (the composition root), never through static self-registration.
    // [WP-07] save sections
    RegisterCoreSaveSections(registry);
    // [WP-02]
    // [WP-04]
    RegisterCoreEffects(registry);
    // [WP-09]
    // [WP-16]
    // [WP-17]
    // The offer economy is data, not a code default: a missing/invalid rewards.json stops the boot.
    if (!LoadRewards(registry, *assets)) {
        LOG_ERROR("reward offer settings are unusable; boot stopped (see the rewards errors above).");
        return false;
    }
    RegisterRewardSection(registry);
    // [WP-19a]
    // [WP-25]

    // keep last: every Register*Effects anchor is above this line
    if (!ValidateEffectContent(registry)) {
        LOG_ERROR("gameplay effect content is unusable; boot stopped (see the effect errors above).");
        return false;
    }
    return true;
}

void FGameplayServices::Shutdown(entt::registry& registry) {
    if (registry.ctx().contains<FSaveSectionRegistry>()) {
        registry.ctx().erase<FSaveSectionRegistry>();
    }
    if (registry.ctx().contains<ai::FAiLibrary>()) {
        registry.ctx().erase<ai::FAiLibrary>();
    }
    // WP-04: erase the effect layer (kind registry + listeners/dedupe) so listeners never lapse into the
    // next battle and a re-initialize starts from a clean registration state.
    if (registry.ctx().contains<FEffectRegistry>()) {
        registry.ctx().erase<FEffectRegistry>();
    }
    if (registry.ctx().contains<FEffectDispatchState>()) {
        registry.ctx().erase<FEffectDispatchState>();
    }
    if (registry.ctx().contains<FContentRegistry>()) {
        registry.ctx().erase<FContentRegistry>();
    }
}

} // namespace game::gameplay
