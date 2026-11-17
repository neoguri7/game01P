#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gameplay/progression/FPassiveTree.h"

namespace game::ui {

/// 화면 좌표 한 점 — 헤더에 <imgui.h>를 넣지 않기 위한 ImVec2 대체 (순수 기하 테스트용).
struct FPanelPoint {
    float x{0.0f};
    float y{0.0f};
};

// 지도 기하 상수 — 원래 .cpp 익명 네임스페이스에 있었다. 테스트가 픽셀 리터럴 없이 기대값을 계산할
// 수 있도록 헤더로 옮겼다 (좌표 0–100, 여백·노드 반경은 폰트 크기 기준 em 비율).
inline constexpr float kPassiveMapMarginEm = 28.0f / 16.0f;
inline constexpr float kPassiveNodeRadiusEm = 13.0f / 16.0f;
inline constexpr float kPassiveCoordinateMax = 100.0f;

/// 단순화 PoE 트리 패널: 연결선 + 노드 맵 + 할당 목록. 콘텐츠(FPassiveTree)와 할당 상태를 소유하지
/// 않고 그리기만 한다 — 런 규칙은 blocker/canAllocate/allocate 콜백이 그대로 전달한다.
struct FPassiveTreePanel {
    const std::vector<FPassiveNode>* nodes{nullptr};
    std::string_view rootNodeId;
    const std::vector<std::string>* allocated{nullptr};
    int points{0};
    /// 노드별 할당 차단 사유 (FPassiveTree::Blocker). 호출자는 반드시 채운다. 빈 값이면
    /// canAllocate(id) ? None : NotAdjacent 로 대체된다 (레거시/스모크 호출자 — 포인트 0이면 Locked로 보인다).
    std::function<EAllocationBlocker(std::string_view nodeId)> blocker;
    std::function<bool(std::string_view nodeId)> canAllocate;
    std::function<void(std::string_view nodeId)> allocate;
};

void DrawPassiveTreePanel(const FPassiveTreePanel& panel);

/// 지도 안 노드 중심 — x·y를 따로 스케일하고 양쪽 여백은 kPassiveMapMarginEm * fontPx.
/// 그리기와 히트 테스트가 같은 함수를 쓴다.
[[nodiscard]] FPanelPoint PassiveNodeCenter(const FPassiveNode& node, FPanelPoint mapOrigin, FPanelPoint mapSize,
                                            float fontPx);

/// mouse에서 radius 이내(≤)의 가장 가까운 중심 인덱스. 동률이면 문서 순서 앞. 없으면 nullopt.
[[nodiscard]] std::optional<std::size_t> HitPassiveNode(const std::vector<FPanelPoint>& centers, float radius,
                                                        FPanelPoint mouse);

/// 목록 접미 라벨 — 포인트와 무관하게 트리 상태(랭크·차단 사유)만 본다.
[[nodiscard]] std::string PassiveNodeListState(EAllocationBlocker blocker, int rank, int maxRanks);

/// 펼친 행·툴팁의 힌트 문구. 빈 문자열 = 힌트 없음.
[[nodiscard]] std::string PassiveNodeHint(EAllocationBlocker blocker, int rank, int maxRanks, int points,
                                          bool canAllocate, std::string_view conflictName);

/// 축약 목록의 기본 표시 여부: 보유 행(rank > 0) 또는 연결된 행(blocker None).
[[nodiscard]] bool PassiveListRowVisible(EAllocationBlocker blocker, int rank);

/// panel.blocker(있으면) 또는 canAllocate 기반 대체 판정 (빈 blocker 호출자용).
[[nodiscard]] EAllocationBlocker PassivePanelBlocker(const FPassiveTreePanel& panel, std::string_view nodeId);

} // namespace game::ui
