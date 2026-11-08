#pragma once

#include "ecs/components/FStats.h"

namespace game {

/// 전투 수치의 불변 베이스(WP-03 §설계): FBattleSetup이 콘텐츠 값 그대로 한 번 부착하고 이후 바뀌지
/// 않는다. 파생 갱신(RefreshCombatSheet)은 매번 여기서부터 재계산해 FStats/FHealth.max/FSpeed를
/// 덮어쓴다 — 제자리 가산(구 ApplyRunParty)과 달리 몇 번을 호출해도 값이 누적되지 않는다(멱등).
/// hp는 FHealth, 속도는 FSpeed가 파생 값을 들고 있고, 이 컴포넌트는 그 원본만 보관한다.
struct FBaseCombatStats {
    FStats stats;
    int maxHealth{0};
    int speed{0};
};

} // namespace game
