#include "gameplay/data/FUnitContent.h"

namespace game::gameplay {

FUnitContent decodeUnit(const FContentRow& row) {
    FUnitContent unit;
    unit.id = row.text(kUnitFieldId);
    unit.displayName = row.text(kUnitFieldDisplayName);
    unit.maxHealth = row.number(kUnitFieldMaxHealth, 0.0); // fallback: 0.0 — schema marks this required, so the fallback never decides a fight value
    unit.speed = row.number(kUnitFieldSpeed, 0.0);         // fallback: 0.0 — absent speed means no initiative bonus

    // 미결(design §4): 이동 AP 최종 수치 — assets/data/units.json 값으로만 결정한다.
    unit.moveAp = row.integer(kUnitFieldMoveAp, 1); // fallback: 잠정값 1 (design §4 확정 잠정치)

    // 미결(design §4): 스킬 AP 최종 수치 — assets/data/units.json 값으로만 결정한다.
    unit.skillAp = row.integer(kUnitFieldSkillAp, 2); // fallback: 잠정값 2 (design §4 확정 잠정치)

    unit.skillIds = row.textArray(kUnitFieldSkills);
    unit.spriteId = row.text(kUnitFieldSprite);

    // design §2: 몬스터 행동트리 규칙 id 목록 — 순서가 우선순위이며, 표에 없는 id는 FContentRegistry가 부팅
    // 실패로 만든다(R19/R22).
    unit.behaviorIds = row.textArray(kUnitFieldBehavior);
    return unit;
}

} // namespace game::gameplay
