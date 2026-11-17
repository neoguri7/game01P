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

    // docs/design/skill-rune-progression.md §2: 나머지 필드는 베이스 스킬의 최소 표현이며 값은 데이터가 정한다.
    skill.power = row.integer(kSkillFieldPower, 0);
    skill.shape = row.text(kSkillFieldShape);
    if (skill.shape.empty()) skill.shape = "single";
    skill.areaRadius = row.integer(kSkillFieldAreaRadius, 0);
    skill.target = row.text(kSkillFieldTarget);
    if (skill.target.empty()) skill.target = "enemy";
    skill.appliesStatus = row.text(kSkillFieldAppliesStatus);

    // §1.1/§3.1: 피해 유형과 룬 소켓 수. 비어 있으면 물리(무피해 스킬은 계산에 쓰이지 않는다).
    skill.damageType = row.text(kSkillFieldDamageType);
    if (skill.damageType.empty()) skill.damageType = std::string(kDamageTypePhysical);
    skill.runeSockets = row.integer(kSkillFieldRuneSockets, 3); // fallback: 잠정값 3 (정본 §10)

    // WP-04: typed hooks (optional). The decoder cannot fail; an unknown event name is kept for boot
    // validation, and the source/id provenance is stamped here.
    skill.hooks = DecodeHooks(row.objectArray(kSkillFieldHooks), "skill:" + skill.id);
    return skill;
}

} // namespace game::gameplay
