#include "gameplay/factories/FRunFactory.h"

#include "core/Logger.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FDungeonContent.h"
#include "gameplay/factories/FBattleFactory.h"
#include "gameplay/run/FBattleState.h"
#include "gameplay/run/FRunState.h"

#include <cstddef>

namespace game::gameplay {
namespace {

/// Destroys the finished battle, then spawns `dungeon.roomEncounterIds[roomIndex]` as the new one.
/// why destroy-then-spawn sits in a helper: the hub/dungeon cycle is one-battle-at-a-time (see
/// FBattleFactory::destroyBattle), so every room spawn must first retire the previous battle — doing it here keeps
/// that single ordering in one place (R5).
/// why a fresh battle per room is all the "party reset" there is: design §1의 "룸 시작 시 파티 상태 = 온전"은
/// 새 전투 스폰에서 자연히 나온다 — 리셋 코드를 따로 두지 않는다.
bool spawnRoom(entt::registry& registry,
               const FContentRegistry& content,
               const FDungeonContent& dungeon,
               std::size_t roomIndex,
               FRunState& state) {
    FBattleState& battle = registry.ctx().get<FBattleState>();
    FBattleFactory::destroyBattle(registry, battle);
    if (!FBattleFactory::buildBattle(registry, content, dungeon.roomEncounterIds[roomIndex], battle)) {
        return false;
    }
    state.roomIndex = roomIndex;
    return true;
}

} // namespace

bool FRunFactory::enterDungeon(entt::registry& registry,
                               const FContentRegistry& content,
                               FRunState& state,
                               std::string_view dungeonId) {
    const FDungeonContent* dungeon = content.dungeons.find(dungeonId);
    if (dungeon == nullptr) {
        LOG_ERROR("run: dungeon '{}' is not in the content table.", dungeonId);
        return false;
    }
    if (dungeon->roomCount() == 0) {
        LOG_ERROR("run: dungeon '{}' has no rooms; refusing an empty run.", dungeonId);
        return false;
    }

    // why the run state is written only after the room spawned: a failed spawn must leave the player in the hub
    // with no dungeon id and no phase change. Writing first and rolling back is the half-entered state this
    // ordering removes (R19).
    if (!spawnRoom(registry, content, *dungeon, 0, state)) {
        LOG_ERROR("run: dungeon '{}' room 0 could not be spawned; staying in the hub.", dungeonId);
        return false;
    }

    state.dungeonId.assign(dungeonId);
    state.phase = ERunPhase::InDungeon;
    return true;
}

bool FRunFactory::advanceRoom(entt::registry& registry,
                              const FContentRegistry& content,
                              FRunState& state) {
    const FDungeonContent* dungeon = content.dungeons.find(state.dungeonId);
    if (dungeon == nullptr) {
        LOG_ERROR("run: current dungeon '{}' is not in the content table.", state.dungeonId);
        return false;
    }

    const std::size_t nextRoom = state.roomIndex + 1;
    if (nextRoom >= dungeon->roomCount()) {
        // 마지막 룸 승리 = 던전 클리어. why returnToHub before the counter: the hub return is the same transition
        // for victory and defeat, so both paths share one destroy/reset site (R5).
        returnToHub(registry, state);
        state.clearedDungeons += 1;
        LOG_INFO("run: dungeon '{}' cleared ({} total).", dungeon->id, state.clearedDungeons);
        return true;
    }

    if (!spawnRoom(registry, content, *dungeon, nextRoom, state)) {
        LOG_ERROR("run: dungeon '{}' room {} could not be spawned; returning to the hub.",
                  dungeon->id,
                  nextRoom);
        // why the hub fallback: the finished battle is already gone, so leaving the phase as InDungeon would strand
        // the run on a null battle. The hub is a recoverable state (R19).
        returnToHub(registry, state);
        return false;
    }
    LOG_INFO("run: dungeon '{}' advanced to room {}/{}.", dungeon->id, nextRoom + 1, dungeon->roomCount());
    return true;
}

void FRunFactory::returnToHub(entt::registry& registry, FRunState& state) {
    FBattleState& battle = registry.ctx().get<FBattleState>();
    FBattleFactory::destroyBattle(registry, battle);
    state.dungeonId.clear();
    state.roomIndex = 0;
    state.phase = ERunPhase::Hub;
}

} // namespace game::gameplay
