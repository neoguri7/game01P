#pragma once

#include "ecs/systems/ISystem.h"

#include "core/InputState.h"
#include "core/Logger.h"
#include "gameplay/components/FBattleDefeat.h"
#include "gameplay/components/FBattleVictory.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FDungeonContent.h"
#include "gameplay/factories/FRunFactory.h"
#include "gameplay/run/FBattleLog.h"
#include "gameplay/run/FBattleState.h"
#include "gameplay/run/FRunState.h"

#include <entt/entt.hpp>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace game::gameplay {

/// Drives the run cycle: 허브에서 Confirm → 던전 입장, 룸 승리 → 다음 룸 / 던전 클리어, 파티 전멸 → 런 종료,
/// 런 종료 후 Confirm → 허브 복귀 (design §1 확정).
/// why it is registered LAST in the pipeline: it is the only consumer of the battle's terminal STATE, so it must
/// run after FBattleOutcomeSystem judged this frame's outcome; and it spawns/destroys battles, so running it before
/// the other systems would let them observe a half-built next room in the same frame (R21/R3).
/// why the durable tag and not FBattleEndedEvent: the outcome is a state transition with a durable owner, not a
/// one-frame notification (R11), and advancing spawns a fresh battle with no terminal tag — so the reaction is
/// naturally once-per-battle without depending on an event-order contract.
struct FRunProgressionSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FRunProgression");

        FRunState* run = registry.ctx().find<FRunState>();
        const FContentRegistry* content = registry.ctx().find<FContentRegistry>();
        if (run == nullptr || content == nullptr) {
            return;
        }

        const FInputState* input = registry.ctx().find<FInputState>();
        const bool confirm = input != nullptr && input->isActionPressed(EInputAction::Confirm);

        switch (run->phase) {
        case ERunPhase::Hub:
            enterNextDungeon(registry, *content, *run, confirm);
            return;
        case ERunPhase::InDungeon:
            reactToBattle(registry, *content, *run);
            return;
        case ERunPhase::RunOver:
            if (confirm) {
                FRunFactory::returnToHub(registry, *run);
                narrate(registry, "허브 복귀");
                LOG_INFO("run: returned to the hub after defeat ({} dungeon(s) cleared).", run->clearedDungeons);
            }
            return;
        }
    }

    [[nodiscard]] std::string name() const override { return "FRunProgression"; }

private:
    static void enterNextDungeon(entt::registry& registry,
                                 const FContentRegistry& content,
                                 FRunState& run,
                                 bool confirm) {
        if (!confirm) {
            return;
        }
        if (content.dungeons.rows.empty()) {
            LOG_ERROR("run: no dungeon row to enter (content unusable; see the boot check).");
            return;
        }
        // 미결(design §1): 마지막 던전 다음은 첫 던전으로 순환 — clearedDungeons가 다음 던전을 고른다.
        const std::size_t index = static_cast<std::size_t>(run.clearedDungeons) % content.dungeons.rows.size();
        const FDungeonContent& dungeon = content.dungeons.rows[index];
        if (!FRunFactory::enterDungeon(registry, content, run, dungeon.id)) {
            return; // phase stays Hub: a failed entry must not move the run (R19)
        }
        narrate(registry, fmt::format("던전 입장: {}", dungeon.displayName));
        LOG_INFO("run: entered dungeon '{}' ({} room(s)).", dungeon.id, dungeon.roomCount());
    }

    static void reactToBattle(entt::registry& registry,
                              const FContentRegistry& content,
                              FRunState& run) {
        const FBattleState* battle = registry.ctx().find<FBattleState>();
        if (battle == nullptr || battle->battle == entt::null || !registry.valid(battle->battle)) {
            return;
        }

        if (registry.all_of<FBattleVictory>(battle->battle)) {
            const FDungeonContent* dungeon = content.dungeons.find(run.dungeonId);
            const std::string dungeonName = dungeon != nullptr ? dungeon->displayName : run.dungeonId;
            const int clearedBefore = run.clearedDungeons;
            if (!FRunFactory::advanceRoom(registry, content, run)) {
                return;
            }
            if (run.clearedDungeons > clearedBefore) {
                narrate(registry, fmt::format("던전 클리어: {}", dungeonName));
                LOG_INFO("run: dungeon cleared, back in the hub.");
            } else {
                narrate(registry, fmt::format("룸 {} 진입", run.roomIndex + 1));
                LOG_INFO("run: entered room {}.", run.roomIndex + 1);
            }
            return;
        }

        if (registry.all_of<FBattleDefeat>(battle->battle)) {
            run.phase = ERunPhase::RunOver;
            narrate(registry, fmt::format("전멸 — 런 종료 (클리어한 던전 {}개)", run.clearedDungeons));
            LOG_INFO("run: party wiped; run over ({} dungeon(s) cleared).", run.clearedDungeons);
        }
    }

    static void narrate(entt::registry& registry, std::string line) {
        if (FBattleLog* log = registry.ctx().find<FBattleLog>()) {
            log->push(std::move(line));
        }
    }
};

} // namespace game::gameplay
