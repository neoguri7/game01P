#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "gameplay/progression/FPassiveTree.h"

namespace game::ui {

/// 단순화 PoE 트리 패널: 연결선 + 노드 맵 + 할당 목록. 콘텐츠(FPassiveTree)와 할당 상태를 소유하지
/// 않고 그리기만 한다 — 런 규칙은 canAllocate/allocate 콜백이 그대로 전달한다.
struct FPassiveTreePanel {
    const std::vector<FPassiveNode>* nodes{nullptr};
    std::string_view rootNodeId;
    const std::vector<std::string>* allocated{nullptr};
    int points{0};
    std::function<bool(std::string_view nodeId)> canAllocate;
    std::function<void(std::string_view nodeId)> allocate;
};

void DrawPassiveTreePanel(const FPassiveTreePanel& panel);

} // namespace game::ui
