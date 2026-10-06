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

    // docs/design/skill-rune-equipment.md §2: 나머지 필드는 베이스 스킬의 최소 표현이며 값은 데이터가 정한다.
    skill.power = row.integer(kSkillFieldPower, 0);
    skill.shape = row.text(kSkillFieldShape);
    if (skill.shape.empty()) skill.shape = "single";
    skill.areaRadius = row.integer(kSkillFieldAreaRadius, 0);
    skill.target = row.text(kSkillFieldTarget);
    if (skill.target.empty()) skill.target = "enemy";
    skill.appliesStatus = row.text(kSkillFieldAppliesStatus);
    return skill;
}

} // namespace game::gameplay
