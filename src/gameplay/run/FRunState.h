#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace game::gameplay {

/// Where the player is in the run cycle (design §2 확정: 허브 → 던전 연속 진입).
/// why a phase enum in a service and not a tag on an entity: this is *session* progress, not what any entity
/// is — a run with no battle currently spawned (Hub, RunOver) still has a phase, but has no battle entity to
/// hang a tag on (R11/R12).
enum class ERunPhase {
    Hub,       // 허브 — 플레이어가 던전 입장을 기다린다 (전투 엔티티 없음)
    InDungeon, // 던전 진행 중 — 현재 룸의 전투가 살아 있다
    RunOver,   // 파티 전멸 — 다음 Confirm으로 허브에 복귀한다
};

/// Run-scope service in registry.ctx() (R6: no singleton). Holds only what is not entity state: which dungeon
/// is being run, which room is open, the run seed, and how many dungeons have been cleared.
/// why clearedDungeons lives here and not in a save file: 상위 §2 확정대로 클리어한 던전 수가 진행
/// 체크포인트다. 이 슬라이스는 세션 안에서만 유지한다 (미결: 거점 저장/이어하기).
struct FRunState {
    std::string dungeonId; // 빈 문자열 = 아직 던전에 들어가지 않았다
    std::size_t roomIndex{0};
    std::uint64_t seed{0}; // 미결(design §2): 플레이어 입력·시간 시드 — 지금은 고정 0 (같은 시드 = 같은 런, R19)
    int clearedDungeons{0};
    ERunPhase phase{ERunPhase::Hub};
};

} // namespace game::gameplay
