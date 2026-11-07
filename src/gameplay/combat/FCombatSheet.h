#pragma once

#include <optional>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FGridPosition.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {

/// 최종 수치 = ① 베이스(FBaseCombatStats) + ② 런 보너스(FRunMember bonus*/노드 — FDungeonRun이 있을
/// 때만) + ③ 적 스케일(depth/room — FDungeonRun이 있을 때만) + ④ 빈 확장점(W1: 항목 없음).
/// 매번 ①에서 시작해 컴포넌트(FStats/FHealth.max/FSpeed)에 덮어쓰므로 같은 입력이면 같은 값이다
/// (멱등 — 제자리 가산 금지). FBaseCombatStats가 없는 엔티티는 아무것도 하지 않는다.
/// ②는 FDungeonRun 경로 전용이다 — forest 패시브(FPrototypeRun)는 FBattleState의 인라인 가산이 계속
/// 소유하므로 여기서 읽지 않는다(읽으면 이중 가산 — ForestNoDoubleBonus).
/// max가 줄면 current를 clamp하고, 늘면 current를 유지한다(비율 보존 없음). 적의 current=max는
/// 전투 시작(ApplyBattleStart)에서만 적용한다.
/// `content`는 아직 쓰지 않는다(④가 소비할 자리 — WP-04/13/18): 수치 소스는 지금 ctx(DungeonBalance/
/// FDungeonGrowth)다. 시그니처는 WP-03 설계대로 유지한다.
void RefreshCombatSheet(
    entt::registry& registry, entt::entity entity, const gameplay::FContentRegistry& content);

/// FBaseCombatStats를 가진 모든 엔티티를 일괄 갱신한다(전투 시작 경로).
void RefreshAllCombatSheets(entt::registry& registry, const gameplay::FContentRegistry& content);

/// ApplyRunParty 래퍼 내부 전용: 파티 = 현재 HP clamp(런 잔여, 1, 갱신 후 max) + 런 스킬 로드아웃,
/// 적 = current = 갱신 후 스케일 max. RefreshAllCombatSheets 뒤에 호출해야 갱신된 max로 clamp된다
/// (역순이면 베이스 max로 HP가 깎인다 — WrapperOrderKeepsHp).
void ApplyBattleStart(entt::registry& registry);

/// One unit a skill preview expects to affect, with the numbers the resolver will apply.
struct FPreviewTarget {
    entt::entity unit{entt::null};
    int damage{0};
    int heal{0};        // 실제 적용될 치유량 — 과잉 치유는 max-current로 잘린 값
    int healthAfter{0};
    bool lethal{false};
    /// 부여 목록(id/지속/강도 델타)만. 부여(ApplyStatus)는 결정적이라 롤하지 않는다 — 통제 상태의
    /// 행동 불가 chance는 WP-13이 단일 도출하고(WP-12 표시), 같은 식을 여기 다시 쓰지 않는다.
    std::vector<FResolvedStatus> statuses;
    /// 계산 근거(로그·표시용): ComputeSkillDamageTerms가 낸 항 그대로(단일 소스 — 여기서 재도출하지
    /// 않는다). 힐 대상은 0이다: 힐 로그는 근거를 쓰지 않고, 소비자가 생기면 ComputeSkillHeal 옆에
    /// 같은 방식의 단일 헬퍼로 추가한다.
    int offense{0}; // 공격 항(공격력/마법공격력 + 사용 시점 상태 보정)
    int power{0};
    int defense{0};
};

struct FSkillPreview {
    int apCost{1};
    std::vector<FGridPosition> cells;
    std::vector<FPreviewTarget> targets;
};

/// 프리뷰 = 실제 계산(WP-03): 리졸버와 같은 경로(갱신된 컴포넌트 + ComputeSkillDamage/Heal + 같은 대상
/// 규칙: 시전자 관점 진영, ComputeSkillCells, FindUnitsOnCells, 대상별 LoS)를 쓴다. AP/사거리/턴
/// 검증은 하지 않는다(리졸버 소유). RNG를 쓰지 않는다 — 상태 부여는 결정적이다.
/// statuses는 부여 목록만 담는다: 정의가 존재하는 FResolvedStatus이고, lethal 대상은 빈 목록이다.
/// FContentRegistry(또는 시전자 FStats)가 없으면 nullopt.
[[nodiscard]] std::optional<FSkillPreview> PreviewSkill(
    entt::registry& registry,
    entt::entity actor,
    const FResolvedSkill& resolved,
    const FGridPosition& aim,
    const FGridPosition& actorCell);

} // namespace game
