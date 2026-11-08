#pragma once

#include <string>

#include "core/Logger.h"
#include "ecs/systems/ISystem.h"
#include "gameplay/combat/FBattleFlow.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FSettings.h"

namespace game::ecs {

/// The W3 busy expression (WP-05a), extracted verbatim (WP-24a) so the gate assignment and the skip loop
/// share one definition. Counts what still has to be shown:
///   * timeline steps: blows not landed, projectiles not launched, deaths not applied;
///   * active effect pictures (projectiles in flight, impact/zone clips playing);
///   * walking units — the walk is part of the action (otherwise a unit would attack mid-walk and
///     the next one would act before the walker arrives);
///   * an Attack/Death clip still playing, loop clips excluded on purpose: game data then can never
///     deadlock the gate (a looping death clip is a visual defect, validated by WP-24a, not a stall);
///   * pendingDeaths: a KO the announcing blow has not landed for yet. Hit flinches do not hold.
[[nodiscard]] inline int PresentationBusyCount(const entt::registry& registry) {
    int busy = 0;
    if (const auto* timeline = registry.ctx().find<FBattleTimeline>()) {
        busy += static_cast<int>(timeline->steps.size());
    }
    if (const auto* effects = registry.ctx().find<FBattleEffects>()) {
        busy += static_cast<int>(effects->active.size());
    }
    for (const auto entity : registry.view<FUnitAnim>()) {
        const FUnitAnim& anim = registry.get<FUnitAnim>(entity);
        if (!anim.route.empty()) ++busy;
        if ((anim.state == EUnitAnim::Attack || anim.state == EUnitAnim::Death) && !anim.player.loop &&
            !anim.player.finished) {
            ++busy;
        }
        if (anim.pendingDeaths > 0) ++busy;
    }
    return busy;
}

/// A skip pass advances the show in fixed 60 Hz slices so walks and clip progress see ordinary steps.
/// kMaxSkipSlices covers 3s > the worst normal chain (WP-05a: ~1.9s); on the bound the loop stops and
/// leaves what remains to the normal frame path — timeline steps are never discarded.
inline constexpr float kSkipSlice = 1.0f / 60.0f;
inline constexpr int kMaxSkipSlices = 180;

/// Plays what the combat rules already decided: unit motions, skill effects and the walk between cells.
/// It only reads the queued FBattleEvents — gameplay never calls into it. After playing, it *assigns* the
/// gameplay-owned presentation gate every frame (WP-05a); gameplay only reads that gate, level-triggered,
/// through IsPresentationSettled, so it can wait for "the show is over" without any callback from here.
/// Battle speed and skip come from the FSettings context value: presentation timing changes, the
/// (dt-free) rules and the RNG never do.
struct BattleAnimationSystem : ISystem {
    void update(entt::registry& registry, float deltaTime) override {
        const FSettings& settings = CurrentSettings(registry);  // ctx read only — no file IO in the loop
        if (settings.skipAnimations) {
            // The first pass keeps the real frame time, so ambient animation (idles, hit flinches) stays
            // on the display clock and pending events drain before the count is trusted; only the
            // catch-up passes use fixed slices. The bound stops the loop only — steps are never discarded.
            int catchUpSlices = 0;
            do {
                UpdateBattleAnimations(registry, catchUpSlices == 0 ? deltaTime : kSkipSlice);
            } while (PresentationBusyCount(registry) > 0 && ++catchUpSlices <= kMaxSkipSlices);
            if (catchUpSlices > kMaxSkipSlices && PresentationBusyCount(registry) > 0) {
                if (!skipBoundWarned) {
                    LOG_WARN("battle skip hit its slice bound; leaving the rest to the normal frame path.");
                    skipBoundWarned = true;
                }
            } else {
                skipBoundWarned = false;
            }
        } else {
            UpdateBattleAnimations(registry, deltaTime * settings.animSpeed);
        }

        // WP-05a: assign (never accumulate) — after the update, so gameplay reads the fresh value.
        auto* gate = registry.ctx().find<FPresentationGate>();
        if (gate == nullptr) gate = &registry.ctx().emplace<FPresentationGate>();
        gate->busy = PresentationBusyCount(registry);
    }

    std::string name() const override { return "BattleAnimationSystem"; }

    /// The skip-bound warning is an episode, not a frame: set while the bound stops a busy streak.
    bool skipBoundWarned{false};
};

} // namespace game::ecs
