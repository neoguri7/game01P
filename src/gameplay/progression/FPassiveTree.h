#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace game {

/// 스탯 보너스 묶음 — 정본 §1.1의 6종(hp·공격력·마법공격력·속도·방어력·마법방어력)과 같은 항목을 센다.
/// 트리 노드와 물질 강화가 이 형을 공유한다.
struct FPassiveStatBonuses {
    int maxHealth{0};
    int attack{0};
    int magicAttack{0};
    int defense{0};
    int magicDefense{0};
    int speed{0};
};

/// 트리 몫에만 적용되는 총합 상한 (데이터 `caps`). nullopt = 그 스탯은 무제한 (v1 트리는 전부 nullopt).
struct FPassiveStatCaps {
    std::optional<int> maxHealth;
    std::optional<int> attack;
    std::optional<int> magicAttack;
    std::optional<int> defense;
    std::optional<int> magicDefense;
    std::optional<int> speed;
};

struct FPassiveNode {
    std::string id;
    std::string displayName;
    std::string description;
    std::string parentId;                // 문서 순서상 첫 진입 방향 엣지의 출발 노드 (v1: `parent`) — 빈 값이면 루트.
                                         // 표시·구 호출자용이며, 할당 판정은 `neighbors`를 쓴다.
    std::vector<std::string> next;       // 저작된 방향 엣지 (정규화: `parent: P`는 P.next += id가 된다).
    std::vector<std::string> neighbors;  // Load가 채운다 — 무방향 할당 인접 = 나가는 엣지 ∪ 들어오는 엣지,
                                         // 중복 없음, 순서 = 나가는 엣지(next 순서) → 들어오는 엣지(문서 순서).
    int maxRanks{1};                     // `max_ranks` — 부재 = 1.
    std::string exclusiveGroup;          // `exclusive_group` — 빈 값 = 없음.
    float x{0.0f};
    float y{0.0f};
    FPassiveStatBonuses bonuses;
};

/// allocated 안에서 nodeId가 몇 번 등장하는가 (반복 노드의 현재 랭크).
[[nodiscard]] int PassiveNodeRank(const std::vector<std::string>& allocated, std::string_view nodeId);

/// nodeId와 같은(비어 있지 않은) exclusive_group을 공유하고, nodeId 자신이 아니며, allocated에 있는
/// 첫 노드 (nodes 순서). nodeId가 없거나 그룹이 없거나 충돌이 없으면 nullptr.
/// 패널이 nodes만 들고 호출할 수 있도록 자유 함수다.
[[nodiscard]] const FPassiveNode* PassiveExclusiveConflict(
    const std::vector<FPassiveNode>& nodes, const std::vector<std::string>& allocated, std::string_view nodeId);

/// 그래프(방향 엣지 + 무방향 할당 인접)·랭크·배타·캡을 담는 불변 콘텐츠 (design §6, v2).
/// 할당 상태는 캐릭터(run member)가 들고 있고, 이 형은 그 상태를 해석만 한다.
struct FPassiveTree {
    std::vector<FPassiveNode> nodes;  // 문서 순서.
    std::string rootNodeId;
    FPassiveStatCaps caps;

    /// 스키마 검증 (v1/v2): 고유 id, 유한하고 음수가 아닌 좌표/보너스, 정확히 하나의 보너스 0 루트,
    /// 방향 비순환 DAG, 루트에서 무방향 도달 가능, max_ranks·exclusive_group·caps.
    /// 문서를 쓸 수 없으면 nullopt(이유는 로그에 남는다 — R19). 알 수 없는 최상위 키는 무시한다.
    [[nodiscard]] static std::optional<FPassiveTree> Load(const nlohmann::json& document);

    [[nodiscard]] const FPassiveNode* Find(std::string_view nodeId) const;

    /// v2 할당 규칙: 노드 존재·루트 아님; rank < maxRanks; 배타 충돌 없음; 그리고
    /// (무방향 인접 노드가 루트이거나 allocated) 또는 rank ≥ 1 (보유 노드 재할당 = 자기 인접).
    /// 포인트 보유 여부는 호출자(런)가 판단한다.
    [[nodiscard]] bool CanAllocate(const std::vector<std::string>& allocated, std::string_view nodeId) const;

    /// allocated의 모든 등장을 합산하고(반복 = 중복 합산, 미지 id는 건너뛴다), 각 스탯을 캡이 있으면 클램프한다.
    [[nodiscard]] FPassiveStatBonuses Bonuses(const std::vector<std::string>& allocated) const;

    [[nodiscard]] bool empty() const noexcept { return nodes.empty(); }
};

} // namespace game
