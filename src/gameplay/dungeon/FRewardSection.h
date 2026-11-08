#pragma once

#include <entt/entt.hpp>

namespace game {

/// WP-17: the reward module's save section id. It writes `run.ext.offers` and owns that subtree's format —
/// the core save schema never names a module.
inline constexpr const char* kRewardOfferSectionId = "offers";

/// Registers the `offers` serializer (version 1) through the composition root. Registration is never static:
/// the runtime is a static library and would drop an unreferenced self-registering object.
void RegisterRewardSection(entt::registry& registry);

} // namespace game
