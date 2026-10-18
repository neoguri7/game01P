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

struct FPassiveNode {
    std::string id;
    std::string displayName;
    std::string description;
    std::string parentId;  // 비어 있으면 루트
    float x{0.0f};
    float y{0.0f};
    FPassiveStatBonuses bonuses;
};

/// 단순화 PoE 노드 트리 (design §6). 로드·검증과 할당 규칙만 담당하는 불변 콘텐츠다.
/// 할당 상태는 캐릭터(run member)가 들고 있고, 이 형은 그 상태를 해석만 한다.
struct FPassiveTree {
    std::vector<FPassiveNode> nodes;
    std::string rootNodeId;

    /// 스키마 검증: schema_version 1, 고유 id, 사이클 없는 단일 루트, 유한하고 음수가 아닌 좌표/보너스.
    /// 문서를 쓸 수 없으면 nullopt(이유는 로그에 남는다 — R19).
    [[nodiscard]] static std::optional<FPassiveTree> Load(const nlohmann::json& document);

    [[nodiscard]] const FPassiveNode* Find(std::string_view nodeId) const;

    /// 인접 규칙(design §6: 인접 노드만 할당). 루트는 할당 대상이 아니고, 자식은 부모가 할당돼야 한다.
    /// 포인트 보유 여부는 호출자(런)가 판단한다.
    [[nodiscard]] bool CanAllocate(const std::vector<std::string>& allocated, std::string_view nodeId) const;

    [[nodiscard]] FPassiveStatBonuses Bonuses(const std::vector<std::string>& allocated) const;

    [[nodiscard]] bool empty() const noexcept { return nodes.empty(); }
};

} // namespace game
