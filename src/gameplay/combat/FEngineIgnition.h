#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/data/FContentRegistry.h"

namespace game {

/// The per-run ignition state (WP-25 §시스템): which engines already fired in the current run. Ctx-owned
/// like the effect dispatch — it survives battles by design (once per RUN, not per fight), and Shutdown
/// leaves it alone so a re-Initialize keeps the run's firing record. `runSeed` is the seed guard: a
/// different seed resets the list (a new run), the same seed keeps it (a Continue). A process restart
/// followed by Continue can therefore re-log the first ignition (effect zero, documented in the WP).
struct FEngineStats {
    std::uint32_t runSeed{0};
    std::vector<std::string> ignited; // engine ids in firing order
};

/// Registers the OnHit listener that checks every loaded engine for one landed hit. Called once from the
/// composition root (the `[WP-25]` anchor in GameplayServices.cpp); the listener dies with
/// FEffectDispatchState in Shutdown, so a re-Initialize re-registers cleanly and never duplicates.
void RegisterEngineIgnitionListener(entt::registry& registry);

/// The pure verdict for one landed hit: returns the number of engines newly ignited this call. Rules, in
/// order (WP-25 §시스템): the actor must be a party member (enemy skills never ignite); the FIRST loadout
/// slot whose skillId matches must socket every `requires_runes` id (the resolver's own slot rule); the
/// TARGET's active statuses — the pre-hit state, because the resolver fires OnHit before applying
/// statuses — must carry the trigger status at `min_magnitude` (freeze is authored magnitude 0, so
/// presence alone is the normal case). Engines are evaluated in data order; one hit may ignite several.
/// No RNG: the verdict is a function of seed/reset state, loadout and statuses alone.
[[nodiscard]] int CheckEngineIgnition(entt::registry& registry, entt::entity actor, entt::entity target,
                                      const gameplay::FContentRegistry& content, const std::string& skillId);

} // namespace game
