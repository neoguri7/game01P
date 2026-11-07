#pragma once

#include <string>

#include "ecs/systems/ISystem.h"
#include "gameplay/combat/FBattleFlow.h"
#include "presentation/FBattleAnim.h"

namespace game::ecs {

/// Plays what the combat rules already decided: unit motions, skill effects and the walk between cells.
/// It only reads the queued FBattleEvents — gameplay never calls into it. After playing, it *assigns* the
/// gameplay-owned presentation gate every frame (WP-05a); gameplay only reads that gate, level-triggered,
/// through IsPresentationSettled, so it can wait for "the show is over" without any callback from here.
struct BattleAnimationSystem : ISystem {
    void update(entt::registry& registry, float deltaTime) override {
        UpdateBattleAnimations(registry, deltaTime);

        // WP-05a: assign (never accumulate). `busy` counts what still has to be shown:
        //   * timeline steps: blows not landed, projectiles not launched, deaths not applied;
        //   * active effect pictures (projectiles in flight, impact/zone clips playing);
        //   * walking units — the walk is part of the action (otherwise a unit would attack mid-walk and
        //     the next one would act before the walker arrives);
        //   * an Attack/Death clip still playing, loop clips excluded on purpose: game data then can never
        //     deadlock the gate (a looping death clip is a visual defect, validated by WP-24a, not a stall);
        //   * pendingDeaths: a KO the announcing blow has not landed for yet. Hit flinches do not hold.
        auto* gate = registry.ctx().find<FPresentationGate>();
        if (gate == nullptr) gate = &registry.ctx().emplace<FPresentationGate>();
        gate->busy = 0;
        if (const auto* timeline = registry.ctx().find<FBattleTimeline>()) {
            gate->busy += static_cast<int>(timeline->steps.size());
        }
        if (const auto* effects = registry.ctx().find<FBattleEffects>()) {
            gate->busy += static_cast<int>(effects->active.size());
        }
        for (const auto entity : registry.view<FUnitAnim>()) {
            const FUnitAnim& anim = registry.get<FUnitAnim>(entity);
            if (!anim.route.empty()) ++gate->busy;
            if ((anim.state == EUnitAnim::Attack || anim.state == EUnitAnim::Death) && !anim.player.loop &&
                !anim.player.finished) {
                ++gate->busy;
            }
            if (anim.pendingDeaths > 0) ++gate->busy;
        }
    }

    std::string name() const override { return "BattleAnimationSystem"; }
};

} // namespace game::ecs
