#include "gameplay/data/FBehaviorContent.h"

#include <string>

namespace game::gameplay {

std::optional<EBehaviorCondition> behaviorConditionFromText(std::string_view text) {
    // why the token table lives here and not in the registry: a token is part of this type's asset
    // vocabulary, so "what can a behavior row say?" has one answer (R12). A miss is nullopt, not a
    // default, because decode cannot fail (R22) — the registry turns nullopt into a boot failure (R19).
    if (text == "always") {
        return EBehaviorCondition::Always;
    }
    if (text == "self_hp_at_or_below") {
        return EBehaviorCondition::SelfHpAtOrBelow;
    }
    if (text == "ally_downed") {
        return EBehaviorCondition::AllyDowned;
    }
    if (text == "opponent_in_skill_range") {
        return EBehaviorCondition::OpponentInSkillRange;
    }
    if (text == "opponent_count_at_or_above") {
        return EBehaviorCondition::OpponentCountAtOrAbove;
    }
    return std::nullopt;
}

std::optional<EBehaviorAction> behaviorActionFromText(std::string_view text) {
    // why the token table lives here: same reason as behaviorConditionFromText (R12).
    if (text == "use_first_skill") {
        return EBehaviorAction::UseFirstSkill;
    }
    if (text == "move_toward_nearest_opponent") {
        return EBehaviorAction::MoveTowardNearestOpponent;
    }
    if (text == "move_away_from_nearest_opponent") {
        return EBehaviorAction::MoveAwayFromNearestOpponent;
    }
    if (text == "wait") {
        return EBehaviorAction::Wait;
    }
    return std::nullopt;
}

FBehaviorContent decodeBehavior(const FContentRow& row) {
    FBehaviorContent behavior;
    behavior.id = row.text(kBehaviorFieldId);

    // 미결(design §2): 조건·행동 어휘 최종 집합 — 지금은 design §2 확정 어휘만 받는다.
    // fallback: Always — an unknown token is a boot failure in FContentRegistry, so the projection only
    // needs a safe value that cannot make a live rule do something surprising.
    behavior.condition = behaviorConditionFromText(row.text(kBehaviorFieldWhen)).value_or(EBehaviorCondition::Always);

    // why threshold is one field for two conditions: 비율 조건은 0~1, 마리 수 조건은 정수 마리 수를 쓰며,
    // 쓰지 않는 조건은 값을 무시한다 (조건마다 필드를 늘리면 어느 쪽이 유효한지가 데이터에 드러나지 않는다).
    behavior.threshold = row.number(kBehaviorFieldThreshold, 0.0); // fallback: 0.0 — a missing threshold cannot pass a > 0 ratio test

    // fallback: Wait — an unknown token is a boot failure in FContentRegistry; the projection still needs a
    // safe value.
    behavior.action = behaviorActionFromText(row.text(kBehaviorFieldThen)).value_or(EBehaviorAction::Wait);
    return behavior;
}

} // namespace game::gameplay
