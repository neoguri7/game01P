#pragma once

#include "ecs/systems/ISystem.h"

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FDisplayName.h"
#include "gameplay/data/FBehaviorContent.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FSkillContent.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/run/FBattleLog.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

#include <cstddef>
#include <string>

namespace game::gameplay {

/// Turns this frame's battle events into the player-visible text of `FBattleLog` — the single narration site.
/// why one narrator instead of each system pushing its own strings: the text a player reads should be phrased
/// once (R12), and the simulation systems stay value-only, so the sprite/animation slice replaces this file plus
/// the view and leaves the simulation untouched (R9).
struct FBattleLogSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FBattleLog");

        FEventBus* bus = registry.ctx().find<FEventBus>();
        FBattleLog* log = registry.ctx().find<FBattleLog>();
        if (bus == nullptr || log == nullptr) {
            return;
        }

        const std::size_t before = log->lines.size();

        for (const FRoundStartedEvent& event : bus->frameEvents<FRoundStartedEvent>()) {
            log->push(fmt::format("=== 라운드 {} ===", event.round));
        }
        for (const FTurnStartedEvent& event : bus->frameEvents<FTurnStartedEvent>()) {
            log->push(fmt::format("{} 의 턴", unitName(registry, event.unit)));
            log->push(fmt::format("  턴 순서: {}", orderText(registry)));
        }
        // why before the move/skill loops: the enemy queues the decision before its action, and the player must
        // read "decided → did" to follow a monster's turn (design dungeon-run.md §2, R31).
        for (const FBehaviorDecidedEvent& event : bus->frameEvents<FBehaviorDecidedEvent>()) {
            if (event.ruleId.empty()) {
                log->push(fmt::format("  {} 행동: 규칙 없음 → 대기", unitName(registry, event.unit)));
            } else {
                log->push(fmt::format("  {} 행동: '{}' → {}",
                                      unitName(registry, event.unit),
                                      event.ruleId,
                                      behaviorActionText(event.action)));
            }
        }
        for (const FTurnEndedEvent& event : bus->frameEvents<FTurnEndedEvent>()) {
            log->push(fmt::format("{} 턴 종료", unitName(registry, event.unit)));
        }
        for (const FUnitMovedEvent& event : bus->frameEvents<FUnitMovedEvent>()) {
            log->push(fmt::format("{} 이동 → ({}, {})", unitName(registry, event.unit), event.x, event.y));
        }
        for (const FMoveRejectedEvent& event : bus->frameEvents<FMoveRejectedEvent>()) {
            log->push(fmt::format("  이동 불가: {}", event.reason));
        }
        for (const FSkillRequestedEvent& event : bus->frameEvents<FSkillRequestedEvent>()) {
            log->push(fmt::format("{} → {} ({} 대상)",
                                  unitName(registry, event.actor),
                                  skillName(registry, event.skillId),
                                  unitName(registry, event.target)));
        }
        for (const FSkillRejectedEvent& event : bus->frameEvents<FSkillRejectedEvent>()) {
            log->push(fmt::format("  사용 불가: {} ({})", skillName(registry, event.skillId), event.reason));
        }
        for (const FUnitDamagedEvent& event : bus->frameEvents<FUnitDamagedEvent>()) {
            log->push(fmt::format("  {} 피해 {:.0f} → 남은 HP {:.0f}",
                                  unitName(registry, event.unit),
                                  event.amount,
                                  event.remaining));
        }
        for (const FUnitDownedEvent& event : bus->frameEvents<FUnitDownedEvent>()) {
            log->push(fmt::format("{} 쓰러짐", unitName(registry, event.unit)));
        }
        for (const FBattleEndedEvent& event : bus->frameEvents<FBattleEndedEvent>()) {
            log->push(event.playerVictory ? "전투 승리" : "파티 전멸 — 전투 패배");
        }

        // why only when something was appended: a per-frame "0 lines" line would bury the battle text in the
        // developer log without ever being useful (R31 wants a state transition logged, not a heartbeat).
        if (log->lines.size() > before) {
            LOG_INFO("narration: {} line(s) appended.", log->lines.size() - before);
        }
    }

    [[nodiscard]] std::string name() const override { return "FBattleLog"; }

private:
    [[nodiscard]] static std::string unitName(entt::registry& registry, entt::entity unit) {
        if (unit == entt::null || !registry.valid(unit)) {
            return "?";
        }
        const FDisplayName* name = registry.try_get<FDisplayName>(unit);
        return name != nullptr ? name->text : fmt::format("entity {}", static_cast<int>(unit));
    }

    [[nodiscard]] static std::string skillName(entt::registry& registry, const std::string& skillId) {
        const FContentRegistry* content = registry.ctx().find<FContentRegistry>();
        const FSkillContent* skill = content != nullptr ? content->skills.find(skillId) : nullptr;
        return skill != nullptr ? skill->displayName : skillId;
    }

    /// The typed action vocabulary rendered as the words the player reads. why a switch here and not a string in
    /// the event: the event carries data, the narrator owns the phrasing (R27/R12).
    [[nodiscard]] static std::string behaviorActionText(EBehaviorAction action) {
        switch (action) {
        case EBehaviorAction::UseFirstSkill:
            return "첫 스킬";
        case EBehaviorAction::MoveTowardNearestOpponent:
            return "가장 가까운 상대에게 접근";
        case EBehaviorAction::MoveAwayFromNearestOpponent:
            return "가장 가까운 상대에게서 후퇴";
        case EBehaviorAction::Wait:
            return "대기";
        }
        return "대기";
    }

    /// The predicted order as one line — design §4 확정 requires 순서 예측 표시, and the log is where a player
    /// compares the prediction with what actually happened.
    [[nodiscard]] static std::string orderText(entt::registry& registry) {
        const FBattleState* state = registry.ctx().find<FBattleState>();
        if (state == nullptr) {
            return {};
        }

        std::string text;
        for (std::size_t index = state->cursor; index < state->order.size(); ++index) {
            if (!text.empty()) {
                text += " > ";
            }
            text += unitName(registry, state->order[index]);
        }
        return text;
    }
};

} // namespace game::gameplay
