#pragma once

#include "gameplay/data/FBehaviorContent.h"

#include <entt/entt.hpp>

#include <string>

namespace game::gameplay {

/// Typed battle events (R27). Systems never call each other (R3): a side *requests* a skill and separate
/// systems resolve it, apply damage and judge the outcome. why one header for the battle's events: they are
/// one vocabulary — the turn loop — and a reader of any system should see the whole chain in one place.
///
/// Delivery rule: the producers and consumers of one chain all run inside a single frame (registration order
/// in FGameplayServices::RegisterSystems), so every event here travels through `queueFrame` and is read via
/// `frameEvents<T>()`. Nothing subscribes, so no callback can outlive its system (R15).
///
/// Ordering contract (the reason FTurnEndSystem exists): a consumer is always registered *after* the producers
/// of the events it reads, because `beginFrame()` clears the frame queues at the start of every frame. A
/// request event read at the start of the next frame is a request that was silently deleted — the event has to
/// be consumed in the same frame it is queued, or it must become state with a durable owner (R11).
/// `invariant:` every event here has exactly one narration consumer (FBattleLogSystem) plus, for the request
/// events, the one system that acts on them — an event nobody reads would be dead vocabulary (R9).

/// Published when a unit's turn opens, after its AP has been refilled (design §4 턴 자원).
struct FTurnStartedEvent {
    entt::entity unit{entt::null};
};

/// Published when a unit closes its turn: the player pressed Esc, or an enemy finished acting.
/// why a *request* and not a state change: only FBattleFactory owns the FTurnActive tag, so the "whose turn"
/// answer has one writer (R12) and the acting systems stay free of turn bookkeeping. Consumed by
/// FTurnEndSystem, which is registered after every producer of this event in the same frame.
struct FTurnEndRequestedEvent {
    entt::entity unit{entt::null};
};

/// Published by FTurnEndSystem when FTurnActive was cleared, for narration.
struct FTurnEndedEvent {
    entt::entity unit{entt::null};
};

/// First round order, published by FInitiativeSystem for narration.
struct FRoundStartedEvent {
    int round{0};
};

/// An actor asks to use one skill row on one target. Both sides use this shape: 미결(design §4) 몬스터 AI는
/// 나중에 이 생산자만 교체한다.
struct FSkillRequestedEvent {
    entt::entity actor{entt::null};
    entt::entity target{entt::null};
    std::string skillId;
};

/// Resolution accepted a request: AP was spent and the target was in range. Carries the skill power so the
/// 피해 공식 stays in exactly one place (미결(design §4)).
struct FSkillResolvedEvent {
    entt::entity actor{entt::null};
    entt::entity target{entt::null};
    std::string skillId;
    double power{0.0};
};

/// Resolution rejected a request (no AP, out of range, no turn, downed actor/target).
/// why this is an event and not only a log line: the player must see *why* the action did not happen (R19),
/// and the reason is presentation text the battle log renders.
struct FSkillRejectedEvent {
    entt::entity actor{entt::null};
    std::string skillId;
    std::string reason;
};

struct FUnitMovedEvent {
    entt::entity unit{entt::null};
    int x{0};
    int y{0};
};

/// A movement press was refused (no move AP, off-grid, occupied cell).
/// why movement has its own rejection event: it carries no skill id, and reusing FSkillRejectedEvent would make
/// the narration print a skill name that does not exist (R27: the event shape describes the request).
struct FMoveRejectedEvent {
    entt::entity unit{entt::null};
    std::string reason;
};

struct FUnitDamagedEvent {
    entt::entity unit{entt::null};
    double amount{0.0};
    double remaining{0.0};
};

struct FUnitDownedEvent {
    entt::entity unit{entt::null};
};

/// An enemy turn was decided by a behaviour rule (design `dungeon-run.md` §2). why this event and not only a
/// LOG_* line: the decision is narration the player must be able to follow (R31) — "why did this monster
/// move instead of attack" is answerable only by naming the rule that matched. `ruleId` empty means no rule
/// matched (the unit waits). `action` is the typed vocabulary from the data layer, so the log consumer does
/// not parse text (R27).
struct FBehaviorDecidedEvent {
    entt::entity unit{entt::null};
    std::string ruleId;
    EBehaviorAction action{EBehaviorAction::Wait};
};

/// Terminal state of the battle (design §4 확정: 일반 전투 목표 = 적 제거).
struct FBattleEndedEvent {
    bool playerVictory{false};
};

} // namespace game::gameplay
