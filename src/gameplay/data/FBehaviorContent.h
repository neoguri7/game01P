#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace game::gameplay {

/// When a behaviour rule applies (design `dungeon-run.md` §2 확정 어휘).
/// why these five and no more: each one is answerable from state the battle already has (own health, the
/// downed tags, the skill range) — a condition that needs a new subsystem has to wait for that subsystem
/// (R4: no speculative vocabulary).
enum class EBehaviorCondition {
    Always,            // 무조건 — 목록의 마지막 catch-all로 쓴다
    SelfHpAtOrBelow,   // threshold = 비율(0~1)
    AllyDowned,        // 같은 편에 쓰러진 유닛이 있다
    OpponentInSkillRange,
    OpponentCountAtOrAbove, // threshold = 마리 수
};

/// What the rule does when it matches (design `dungeon-run.md` §2 확정 어휘).
/// why the action is a data value and not a callback: the executor (`FEnemyTurnSystem`) has to be able to
/// answer "what did this monster decide?" from the row alone, and a stored function pointer would move the
/// decision out of the asset (R17/R22).
enum class EBehaviorAction {
    UseFirstSkill,
    MoveTowardNearestOpponent,
    MoveAwayFromNearestOpponent,
    Wait, // 아무것도 하지 않고 턴을 넘긴다
};

/// One rule of a monster's behaviour tree, as authored in `assets/data/behaviors.json`.
/// A unit lists rule ids in `units.json` → `behavior`; the list IS the tree (위에서 첫 일치 규칙 1개 실행).
/// why a flat rule list instead of nested children: 순서가 곷 트리다 — 2층 트리(규칙 → 하위 트리)는 미결이며,
/// 평평한 목록은 그 확장을 데이터에 자식 id를 더하는 일로 만든다 (R22).
struct FBehaviorContent {
    std::string id;
    EBehaviorCondition condition{EBehaviorCondition::Always};
    double threshold{0.0}; // 비율 조건과 마리 수 조건이 공유한다 (쓰지 않는 조건은 값을 무시한다)
    EBehaviorAction action{EBehaviorAction::Wait};
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kBehaviorField* indices below, decodeBehavior and
/// assets/data/behaviors.json must keep the same order and the same keys.
inline constexpr FContentField kBehaviorFields[] = {
    {"id", EContentFieldType::Text, true},        // boundary: asset key
    {"when", EContentFieldType::Text, true},      // boundary: asset key — 조건 어휘
    {"threshold", EContentFieldType::Number, false}, // boundary: asset key
    {"then", EContentFieldType::Text, true},      // boundary: asset key — 행동 어휘
};

inline constexpr std::size_t kBehaviorFieldId = 0;
inline constexpr std::size_t kBehaviorFieldWhen = 1;
inline constexpr std::size_t kBehaviorFieldThreshold = 2;
inline constexpr std::size_t kBehaviorFieldThen = 3;

static_assert(std::size(kBehaviorFields) == 4, "kBehaviorFields and the kBehaviorField* indices drifted apart");

/// Token → enum, or nullopt for a token outside the vocabulary. Same reason as `roomKindFromText`: the parse
/// belongs next to the type whose asset vocabulary it decodes (R12).
[[nodiscard]] std::optional<EBehaviorCondition> behaviorConditionFromText(std::string_view text);
[[nodiscard]] std::optional<EBehaviorAction> behaviorActionFromText(std::string_view text);

/// Projects one validated row into a rule. Cannot fail on types (R22); the grammar check (known `when`/`then`
/// token, threshold present for the conditions that need it) belongs to FContentRegistry, which owns the
/// tables.
[[nodiscard]] FBehaviorContent decodeBehavior(const FContentRow& row);

} // namespace game::gameplay
