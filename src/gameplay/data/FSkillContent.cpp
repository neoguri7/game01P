#include "gameplay/data/FSkillContent.h"

namespace game::gameplay {

FSkillContent decodeSkill(const FContentRow& row) {
    FSkillContent skill;
    skill.id = row.text(kSkillFieldId);
    skill.displayName = row.text(kSkillFieldDisplayName);
    skill.apCost = row.integer(kSkillFieldApCost, 1); // fallback: 잠정값 1 (design §4 스킬 AP 소모의 최소 단위)

    // 미결(design §4): 사거리(칸) 최종 수치 — assets/data/skills.json 값으로만 결정한다.
    skill.range = row.integer(kSkillFieldRange, 1); // fallback: 잠정값 1 (design §4 확정 잠정치 인접값)

    skill.tags = row.textArray(kSkillFieldTags);
    return skill;
}

} // namespace game::gameplay
