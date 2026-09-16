#include "gameplay/GameplayServices.h"

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "gameplay/data/FContentRegistry.h"

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

    registry.ctx().emplace<FContentRegistry>(std::move(*content));
    return true;
}

void FGameplayServices::Shutdown(entt::registry& registry) {
    if (registry.ctx().contains<FContentRegistry>()) {
        registry.ctx().erase<FContentRegistry>();
    }
}

} // namespace game::gameplay
