#pragma once

namespace game::gameplay {

/// Turn resources, split in two and never converted (design §4 확정, 사용자 결정 m00756): 이동 AP는 이동
/// 전용, 스킬 AP는 스킬 전용. why one component with two fields: they are refilled together at turn start
/// and read together by the HUD, but no code path may move a point between them.
struct FApPool {
    int move{0};
    int skill{0};
};

} // namespace game::gameplay
