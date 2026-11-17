#pragma once

namespace game {

/// Tag: the unit is out of the fight. docs/design/skill-rune-progression.md (전투 규칙) + design §4:
/// 쓰러진 유닛은 행동하지 않지만 그리드 자리는 유지한다. TurnSystem의 참여 대상에서 제외된다.
struct FDead {};

} // namespace game
