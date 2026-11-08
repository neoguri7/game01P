#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/combat/FCombatTypes.h"

namespace game {

/// Who issued a battle command (WP-06). Every SubmitBattleCommand caller states its side; a command that
/// entered FActionQueue::pending through a direct push (forest, the pre-WP-12 HUD, tests that predate the
/// API) has no provenance and is recorded as Unknown.
enum class ECommandSource { Player, Ai, System, Test, Unknown };

/// Why a battle command was rejected. The drain owns the 15 drain codes in the fixed order of the
/// resolver's checks (WP-06 P6-4 — that order IS the canonical one, do not re-arrange it); the submit API
/// owns PresentationBusy. `None` means accepted — every non-None result carries a reason string.
enum class ECommandReject {
    None,
    // 드레인(ActionResolverSystem) — 현재 코드 경로 실측 순서
    StaleTurn, NotActor, Unavailable, NoAp, Blocked, Dead, UnknownSkill,
    SkillResolutionFailed, BadTarget, WrongFaction, OutsideGrid, OutOfRange, NoLos,
    PreviewFailed, OutcomeResolved,
    // 제출 단계
    PresentationBusy
};

/// One battle action plus who asked for it and the battle-session sequence number. The action token
/// (FActionRequest::token / FMoveRequest::token) is the identity — there is no second clock here (P6-2:
/// no session/round/turn copy, no frame index). `seq` is issued by SubmitBattleCommand on acceptance
/// only; a rejection recorded at submit keeps 0.
struct FBattleCommand {
    FBattleAction action;
    ECommandSource source{ECommandSource::Player};
    std::uint64_t seq{0};
};

/// One recorded command outcome: the command as it was seen, the standardized code, and the exact
/// player-facing text (byte-identical to the pre-WP-06 FCombatLog lines for drain rejects).
struct FCommandResult {
    FBattleCommand command;
    ECommandReject reject{ECommandReject::None};
    std::string reason;
};

/// Provenance of one accepted command, pushed in FIFO order alongside FActionQueue::pending.
struct FCommandMeta {
    ECommandSource source{ECommandSource::Unknown};
    std::uint64_t seq{0};
};

/// Parallel FIFO to FActionQueue::pending (P6-1). Written by SubmitBattleCommand; the resolver consumes it
/// as one batch and pairs it with the batch only when the sizes match — half-trusted provenance is never
/// guessed by index, the whole batch is Unknown instead.
struct FCommandOutbox {
    std::vector<FCommandMeta> meta;
};

/// Battle-scoped record of command outcomes (created by the first submit). The session guard is
/// `sessionId` (P6-7) and `recent` is capped: older entries drop first.
inline constexpr std::size_t kMaxRecentCommandResults = 64;

struct FCommandResults {
    std::uint64_t sessionId{0};
    std::uint64_t nextSeq{1};
    std::vector<FCommandResult> recent;
};

/// 제출 단계 거부 문구 — the result's reason and the one FCombatLog line share this exact text (계약 4).
inline constexpr const char* kPresentationBusyReason = "Presentation is still settling; command rejected.";

/// resolved 전투 드레인의 결과 문구 — recorded only; the drain never writes it to FCombatLog (P6-3).
inline constexpr const char* kOutcomeResolvedReason = "The battle is already resolved; command rejected.";

/// The battle session a command belongs to — its action token's sessionId, the one clock.
[[nodiscard]] std::uint64_t CommandSessionId(const FBattleAction& action);

/// Session guard, shared by the submit API and the drain's recorder: the results belong to one battle. A
/// different sessionId clears `recent` and restarts the sequence, so a new battle can never inherit the
/// previous one's record (P6-7 — no edit to the battle state is needed for this).
void ResetCommandResultsForSession(FCommandResults& results, std::uint64_t sessionId);

/// Session guard + append; the only writer of `recent`. Keeps the record at kMaxRecentCommandResults.
void RecordCommandResult(FCommandResults& results, std::uint64_t sessionId, FCommandResult result);

/// Submit one command through the one API (계약 1–5): FCommandResults/FCommandOutbox (and FActionQueue)
/// are created on demand; on a new battle session the record is cleared and the stale outbox meta dropped;
/// a resolved battle records OutcomeResolved and refuses; an unsettled presentation records
/// PresentationBusy, pushes the one log line (when a FCombatLog exists) and refuses; otherwise the command
/// gets its seq, lands on FActionQueue::pending and its provenance on FCommandOutbox::meta. True on
/// acceptance. Validation stays the resolver's job.
[[nodiscard]] bool SubmitBattleCommand(entt::registry& registry, FBattleCommand command);

} // namespace game
