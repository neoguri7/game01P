#pragma once

namespace game::gameplay {

/// Initiative weight for the turn order (design §4 확정: 속도 기반 이니셔티브 + 순서 예측 표시).
/// why a component rather than a content lookup per frame: a run-scope 수치노드/룬 will modify speed later,
/// and that modification belongs to the entity, not to the content row it was spawned from (R12).
struct FInitiative {
    double speed{0.0};
};

} // namespace game::gameplay
