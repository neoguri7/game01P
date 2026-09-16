#include "gameplay/data/FSkillContent.h"

namespace game::gameplay {

FSkillContent decodeSkill(const FContentRow& row) {
    FSkillContent skill;
    skill.id = row.text(kSkillFieldId);
    skill.displayName = row.text(kSkillFieldDisplayName);
    skill.apCost = row.integer(kSkillFieldApCost, 1); // fallback: 잠정값 1 (design §4 스킬 AP 소모의 최소 단위)

    // 미결(design §4): 사거리(칸) 최종 수치 — assets/data/skills.json 값으로만 결정한다.
    skill.range = row.integer(kSkillFieldRange, 1); // fallback: 잠정값 1 (design §4 확정 잠정치 인접값)

    // 미결(design §4): 피해 공식 — 확정 전에는 데이터의 power를 그대로 피해로 쓴다(경감·치명타 없음).
    skill.power = row.number(kSkillFieldPower, 0.0); // fallback: 0.0 (피해 없는 스킬이 정상 상태다: design §5 동작 변경 스킬)

    skill.tags = row.textArray(kSkillFieldTags);
    return skill;
}

} // namespace game::gameplay
