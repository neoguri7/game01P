#include "gameplay/GameplayServices.h"

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "core/SystemManager.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/run/FBattleLog.h"
#include "gameplay/run/FBattleState.h"
#include "gameplay/run/FCommandSelection.h"
#include "gameplay/run/FRunState.h"
#include "gameplay/systems/FBattleLogSystem.h"
#include "gameplay/systems/FBattleOutcomeSystem.h"
#include "gameplay/systems/FDamageSystem.h"
#include "gameplay/systems/FEnemyTurnSystem.h"
#include "gameplay/systems/FGridMoveSystem.h"
#include "gameplay/systems/FInitiativeSystem.h"
#include "gameplay/systems/FPlayerCommandSystem.h"
#include "gameplay/systems/FSkillResolveSystem.h"
#include "gameplay/systems/FRunProgressionSystem.h"
#include "gameplay/systems/FTurnEndSystem.h"
#include "gameplay/systems/FTurnStartSystem.h"

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

    // Battle-scope services (R6: services live in the context, never in a singleton). They are created before the
    // battle so a system that runs on the first frame always finds them.
    registry.ctx().emplace<FBattleState>();
    registry.ctx().emplace<FBattleLog>();
    registry.ctx().emplace<FCommandSelection>();

    const FContentRegistry& tables = registry.ctx().get<FContentRegistry>();
    // Boot no longer spawns a battle: the player starts in the hub and enters a dungeon with Confirm (design §1).
    // why the guard is on dungeons rather than encounters: entering a dungeon is now the only way to reach a battle,
    // so a content set with no dungeon row cannot produce a run — as unusable as no encounter was when the boot
    // still spawned one (R19/R22).
    if (tables.dungeons.rows.empty()) {
        LOG_ERROR("gameplay needs at least one dungeon row to enter (design §1).");
        return false;
    }

    // Run-scope service (R6). why the seed is written here and not left to the default: design §1 확정 — the run seed
    // is fixed at 0 so the same inputs replay the same run (R19).
    FRunState& run = registry.ctx().emplace<FRunState>();
    run.seed = 0;
    run.phase = ERunPhase::Hub;

    return true;
}

void FGameplayServices::RegisterSystems(game::SystemManager& systems) {
    // The turn loop, in the order a single frame walks it. why this order: every arrow below is a queueFrame event
    // consumed by a later system of the *same* frame — Initiative decides the order, TurnStart opens a turn,
    // GridMove/PlayerCommand/EnemyTurn produce actions, Resolve checks legality, Damage applies it, Outcome judges
    // the battle, TurnEnd closes a requested turn, and Log narrates whatever happened (R3: no system calls another
    // directly). This order IS the contract (see FBattleEvents.h): a queueFrame event consumed before its producer
    // would be silently dropped by the next beginFrame(), so a request consumer must be registered after every
    // producer — that is the only reason FTurnEnd sits at the end instead of inside FTurnStartSystem.
    systems.addSystem<FInitiativeSystem>();  // order + round header
    systems.addSystem<FTurnStartSystem>();   // opens the next turn (AP refill + FTurnActive)
    systems.addSystem<FGridMoveSystem>();    // player movement (이동 AP)
    systems.addSystem<FPlayerCommandSystem>(); // player skill/target selection (스킬 AP)
    systems.addSystem<FEnemyTurnSystem>();   // 미결(design §4) 몬스터 AI 자리
    systems.addSystem<FSkillResolveSystem>(); // legality: turn, skill list, range, AP
    systems.addSystem<FDamageSystem>();      // 피해 공식 (미결(design §4)) + 쓰러짐
    systems.addSystem<FBattleOutcomeSystem>(); // 승리/패배 판정
    systems.addSystem<FTurnEndSystem>();     // closes a turn requested above (Esc / enemy finished)
    systems.addSystem<FBattleLogSystem>();   // narration last, so one frame is narrated in one place
    // why last, after the narration: this is the only system that acts on the battle's terminal STATE, so it must
    // read the outcome FBattleOutcomeSystem produced earlier in this frame, and it spawns/destroys battles — every
    // other system must have seen the finished battle, not a half-built next room (R21/R3).
    systems.addSystem<FRunProgressionSystem>(); // 허브 입장 / 룸 진행 / 던전 클리어 / 전멸
}

void FGameplayServices::Shutdown(entt::registry& registry) {
    if (registry.ctx().contains<FRunState>()) {
        registry.ctx().erase<FRunState>();
    }
    if (registry.ctx().contains<FCommandSelection>()) {
        registry.ctx().erase<FCommandSelection>();
    }
    if (registry.ctx().contains<FBattleLog>()) {
        registry.ctx().erase<FBattleLog>();
    }
    if (registry.ctx().contains<FBattleState>()) {
        registry.ctx().erase<FBattleState>();
    }
    if (registry.ctx().contains<FContentRegistry>()) {
        registry.ctx().erase<FContentRegistry>();
    }
}

} // namespace game::gameplay
