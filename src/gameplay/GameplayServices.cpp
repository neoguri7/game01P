#include "gameplay/GameplayServices.h"

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "core/SystemManager.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FEncounterContent.h"
#include "gameplay/factories/FBattleFactory.h"
#include "gameplay/run/FBattleLog.h"
#include "gameplay/run/FBattleState.h"
#include "gameplay/run/FCommandSelection.h"
#include "gameplay/systems/FBattleLogSystem.h"
#include "gameplay/systems/FBattleOutcomeSystem.h"
#include "gameplay/systems/FDamageSystem.h"
#include "gameplay/systems/FEnemyTurnSystem.h"
#include "gameplay/systems/FGridMoveSystem.h"
#include "gameplay/systems/FInitiativeSystem.h"
#include "gameplay/systems/FPlayerCommandSystem.h"
#include "gameplay/systems/FSkillResolveSystem.h"
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
    if (tables.encounters.rows.empty()) {
        LOG_ERROR("gameplay needs at least one encounter row to bootstrap a battle (design §3: 룸/던전 모델 미결).");
        return false;
    }

    // which battle boots: the first authored encounter. why not an id literal in code: the starting encounter is
    // content, and 미결(design §2/§3) 거점/던전 흐름이 정해지기 전까지 "첫 행"이 결정론적인 기본값이다.
    FBattleState& state = registry.ctx().get<FBattleState>();
    if (!FBattleFactory::buildBattle(registry, tables, tables.encounters.rows.front().id, state)) {
        LOG_ERROR("gameplay battle could not be spawned; boot stopped (see the battle error above).");
        return false;
    }

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
}

void FGameplayServices::Shutdown(entt::registry& registry) {
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
