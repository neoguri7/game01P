#include "presentation/FPassiveTreePanel.h"

#include <algorithm>
#include <format>
#include <string>

#include <imgui.h>

namespace game::ui {
namespace {

constexpr ImU32 kEdgeAllocated = IM_COL32(255, 211, 105, 255);
constexpr ImU32 kEdgeConnected = IM_COL32(77, 174, 255, 210);
constexpr ImU32 kEdgeLocked = IM_COL32(76, 82, 99, 150);
constexpr ImU32 kNodeAllocated = IM_COL32(255, 199, 82, 255);
constexpr ImU32 kNodeAvailable = IM_COL32(56, 132, 190, 255);
constexpr ImU32 kNodeLocked = IM_COL32(59, 63, 76, 255);
constexpr ImU32 kNodeOutline = IM_COL32(222, 228, 240, 220);
constexpr ImU32 kMarkInk = IM_COL32(20, 24, 32, 255);
constexpr ImU32 kLabelInk = IM_COL32(220, 224, 234, 255);
constexpr ImU32 kLabelPlate = IM_COL32(16, 18, 24, 215);

// The map is drawn in font-relative units: the theme's body font is a 17 px literal in FUiTheme.cpp
// ApplyTheme (with a 1.2 global-scale fallback for headless contexts), so the default size is not a
// single named constant; the em ratios use 16 px (380 px / 13 px at 16 px keep the old proportions).
// The margin/radius/coordinate-range constants live in the header (kPassiveMapMarginEm etc.) so tests
// can derive expectations without pixel literals.
constexpr float kMapHeightEm = 380.0f / 16.0f;
constexpr float kLabelGapEm = 3.0f / 16.0f;
/// Tooltip body wraps at this width (em): a long description must not stretch the tooltip.
constexpr float kTooltipWrapEm = 20.0f;
constexpr float kNodeOutlineWidth = 2.0f;
constexpr float kEdgeWidth = 2.0f;
constexpr float kEdgeAllocatedWidth = 3.0f;
constexpr float kLabelPlatePadding = 2.0f;
constexpr float kLabelPlateRounding = 3.0f;
constexpr int kNodeSegments = 20;

/// Repeatable nodes append their rank to the display name ("Tempered 2/5").
std::string RankLabel(const FPassiveNode& node, const std::vector<std::string>& allocated) {
    if (node.maxRanks <= 1) return node.displayName;
    return std::format("{} {}/{}", node.displayName, PassiveNodeRank(allocated, node.id), node.maxRanks);
}

/// Names of the other members of an exclusive group, in document order.
std::string ExclusiveMembers(const std::vector<FPassiveNode>& nodes, const FPassiveNode& node) {
    std::string members;
    for (const FPassiveNode& other : nodes) {
        if (other.id == node.id || other.exclusiveGroup != node.exclusiveGroup) continue;
        if (!members.empty()) members += ", ";
        members += other.displayName;
    }
    return members;
}

/// Non-zero bonuses in the callers' "Tree bonuses" order ("+2 ATK, +5 HP"); empty when all are zero.
std::string BonusSummary(const FPassiveStatBonuses& bonuses) {
    std::string text;
    const auto add = [&](const char* label, int value) {
        if (value == 0) return;
        if (!text.empty()) text += ", ";
        text += std::format("+{} {}", value, label);
    };
    add("HP", bonuses.maxHealth);
    add("ATK", bonuses.attack);
    add("MATK", bonuses.magicAttack);
    add("DEF", bonuses.defense);
    add("MDEF", bonuses.magicDefense);
    add("speed", bonuses.speed);
    return text;
}

} // namespace

FPanelPoint PassiveNodeCenter(const FPassiveNode& node, FPanelPoint mapOrigin, FPanelPoint mapSize, float fontPx) {
    const float mapMargin = kPassiveMapMarginEm * fontPx;
    return FPanelPoint{
        mapOrigin.x + mapMargin + node.x * std::max(1.0f, mapSize.x - mapMargin * 2.0f) / kPassiveCoordinateMax,
        mapOrigin.y + mapMargin + node.y * std::max(1.0f, mapSize.y - mapMargin * 2.0f) / kPassiveCoordinateMax,
    };
}

std::optional<std::size_t> HitPassiveNode(const std::vector<FPanelPoint>& centers, float radius, FPanelPoint mouse) {
    std::optional<std::size_t> nearest;
    float nearestSquared = radius * radius;
    for (std::size_t index = 0; index < centers.size(); ++index) {
        const float dx = centers[index].x - mouse.x;
        const float dy = centers[index].y - mouse.y;
        const float squared = dx * dx + dy * dy;
        if (squared > nearestSquared) continue;
        if (!nearest || squared < nearestSquared) {
            nearest = index;
            nearestSquared = squared;
        }
    }
    return nearest;
}

std::string PassiveNodeListState(EAllocationBlocker blocker, int rank, int maxRanks) {
    if (rank > 0) {
        if (maxRanks > 1) return std::format(" (Rank {}/{})", rank, maxRanks);
        return " (Allocated)";
    }
    switch (blocker) {
        case EAllocationBlocker::None: return " (Connected)";
        case EAllocationBlocker::Exclusive: return " (Excluded)";
        default: return " (Locked)";
    }
}

std::string PassiveNodeHint(EAllocationBlocker blocker, int rank, int maxRanks, int points, bool canAllocate,
                            std::string_view conflictName) {
    if (rank > 0 && blocker == EAllocationBlocker::MaxRank) {
        return maxRanks > 1 ? std::string("Maximum rank reached.") : std::string{};
    }
    switch (blocker) {
        case EAllocationBlocker::Exclusive: return std::format("Exclusive with {}.", conflictName);
        case EAllocationBlocker::NotAdjacent: return "Connect an adjacent node first.";
        case EAllocationBlocker::None:
            if (points <= 0) return "Needs a node point.";
            if (!canAllocate) return "Not available right now.";
            return {};
        default: return {};
    }
}

bool PassiveListRowVisible(EAllocationBlocker blocker, int rank) {
    return rank > 0 || blocker == EAllocationBlocker::None;
}

EAllocationBlocker PassivePanelBlocker(const FPassiveTreePanel& panel, std::string_view nodeId) {
    if (panel.blocker) return panel.blocker(nodeId);
    return panel.canAllocate && panel.canAllocate(nodeId) ? EAllocationBlocker::None : EAllocationBlocker::NotAdjacent;
}

void DrawPassiveTreePanel(const FPassiveTreePanel& panel) {
    if (panel.nodes == nullptr || panel.allocated == nullptr || panel.nodes->empty()) return;
    const auto& nodes = *panel.nodes;
    const auto& allocated = *panel.allocated;
    const float fontPx = ImGui::GetFontSize();
    const float mapHeight = kMapHeightEm * fontPx;
    const float nodeRadius = kPassiveNodeRadiusEm * fontPx;
    const float labelGap = kLabelGapEm * fontPx;
    const auto isAllocatedOrRoot = [&](std::string_view nodeId) {
        return nodeId == panel.rootNodeId || PassiveNodeRank(allocated, nodeId) > 0;
    };

    // One allocation per frame: the map click and the list button both call this latch only.
    bool allocatedThisFrame = false;
    const auto tryAllocate = [&](std::string_view nodeId) {
        if (allocatedThisFrame) return;
        allocatedThisFrame = true;
        if (panel.allocate) panel.allocate(nodeId);
    };

    ImGui::BeginChild("passive-tree-map", ImVec2(0, mapHeight), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 mapOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 mapSize = ImGui::GetContentRegionAvail();
    const FPanelPoint origin{mapOrigin.x, mapOrigin.y};
    const FPanelPoint size{mapSize.x, mapSize.y};
    std::vector<FPanelPoint> centers;
    centers.reserve(nodes.size());
    for (const FPassiveNode& node : nodes) centers.push_back(PassiveNodeCenter(node, origin, size, fontPx));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const FPassiveNode& node = nodes[index];
        for (const std::string& targetId : node.next) {
            const auto target = std::ranges::find(nodes, targetId, &FPassiveNode::id);
            if (target == nodes.end()) continue;
            const std::size_t targetIndex = static_cast<std::size_t>(target - nodes.begin());
            const bool fromAllocated = isAllocatedOrRoot(node.id);
            const bool toAllocated = isAllocatedOrRoot(target->id);
            const ImU32 color = fromAllocated && toAllocated ? kEdgeAllocated
                : fromAllocated || toAllocated ? kEdgeConnected : kEdgeLocked;
            drawList->AddLine(ImVec2(centers[index].x, centers[index].y),
                              ImVec2(centers[targetIndex].x, centers[targetIndex].y), color,
                              fromAllocated && toAllocated ? kEdgeAllocatedWidth : kEdgeWidth);
        }
    }
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const FPassiveNode& node = nodes[index];
        const bool nodeAllocated = isAllocatedOrRoot(node.id);
        const bool available = PassivePanelBlocker(panel, node.id) == EAllocationBlocker::None;
        const ImVec2 center(centers[index].x, centers[index].y);
        const ImU32 fill = nodeAllocated ? kNodeAllocated : available ? kNodeAvailable : kNodeLocked;
        drawList->AddCircleFilled(center, nodeRadius, fill, kNodeSegments);
        drawList->AddCircle(center, nodeRadius, kNodeOutline, kNodeSegments, kNodeOutlineWidth);
        const std::string mark = node.displayName.substr(0, 1);
        const ImVec2 markSize = ImGui::CalcTextSize(mark.c_str());
        drawList->AddText(ImVec2(center.x - markSize.x * 0.5f, center.y - markSize.y * 0.5f),
                          kMarkInk, mark.c_str());
        const std::string label = RankLabel(node, allocated);
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        const ImVec2 labelPos(center.x - labelSize.x * 0.5f, center.y + nodeRadius + labelGap);
        drawList->AddRectFilled(ImVec2(labelPos.x - kLabelPlatePadding, labelPos.y - kLabelPlatePadding),
                                ImVec2(labelPos.x + labelSize.x + kLabelPlatePadding,
                                       labelPos.y + labelSize.y + kLabelPlatePadding),
                                kLabelPlate, kLabelPlateRounding);
        drawList->AddText(labelPos, kLabelInk, label.c_str());
    }
    ImGui::SetCursorScreenPos(mapOrigin);
    const bool mapClicked = ImGui::InvisibleButton("##passive-map-hit",
                                                   ImVec2(std::max(1.0f, mapSize.x), std::max(1.0f, mapSize.y)));
    const ImGuiIO& io = ImGui::GetIO();
    const std::optional<std::size_t> hovered =
        HitPassiveNode(centers, nodeRadius, FPanelPoint{io.MousePos.x, io.MousePos.y});
    if (mapClicked) {
        // Only a press and a release on the same node allocates: releasing over a node after pressing
        // outside it (or on another node) is ignored.
        const std::optional<std::size_t> pressed =
            HitPassiveNode(centers, nodeRadius, FPanelPoint{io.MouseClickedPos[0].x, io.MouseClickedPos[0].y});
        if (hovered && pressed && hovered == pressed) {
            const FPassiveNode& node = nodes[*hovered];
            if (panel.canAllocate && panel.canAllocate(node.id)) tryAllocate(node.id);
        }
    }
    if (ImGui::IsItemHovered() && hovered) {
        const FPassiveNode& node = nodes[*hovered];
        const int rank = PassiveNodeRank(allocated, node.id);
        const FPassiveNode* conflict = PassiveExclusiveConflict(nodes, allocated, node.id);
        const bool canAllocate = panel.canAllocate && panel.canAllocate(node.id);
        const EAllocationBlocker blocker = PassivePanelBlocker(panel, node.id);
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(kTooltipWrapEm * fontPx);
        if (node.maxRanks > 1) {
            ImGui::Text("%s %d/%d", node.displayName.c_str(), rank, node.maxRanks);
        } else {
            ImGui::TextUnformatted(node.displayName.c_str());
        }
        ImGui::TextWrapped("%s", node.description.c_str());
        const std::string bonuses = BonusSummary(node.bonuses);
        if (!bonuses.empty()) ImGui::TextDisabled("%s", bonuses.c_str());
        if (conflict == nullptr && !node.exclusiveGroup.empty()) {
            ImGui::TextDisabled("Either/or: excludes %s.", ExclusiveMembers(nodes, node).c_str());
        }
        const std::string hint = PassiveNodeHint(
            blocker, rank, node.maxRanks, panel.points, canAllocate,
            conflict != nullptr ? std::string_view(conflict->displayName) : std::string_view{});
        if (!hint.empty()) ImGui::TextDisabled("%s", hint.c_str());
        if (canAllocate) ImGui::TextDisabled("Click to allocate.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Gold: allocated   Blue: connected   Gray: locked");

    // One row renderer for both loops: the default (connected/held) list and the folded "show all" list.
    const auto drawRow = [&](const FPassiveNode& node) {
        const int rank = PassiveNodeRank(allocated, node.id);
        const EAllocationBlocker blocker = PassivePanelBlocker(panel, node.id);
        const FPassiveNode* conflict = PassiveExclusiveConflict(nodes, allocated, node.id);
        const bool canAllocate = panel.canAllocate && panel.canAllocate(node.id);
        const std::string state = PassiveNodeListState(blocker, rank, node.maxRanks);
        if (!ImGui::TreeNode(node.id.c_str(), "%s%s", node.displayName.c_str(), state.c_str())) return;
        ImGui::TextWrapped("%s", node.description.c_str());
        if (conflict == nullptr && !node.exclusiveGroup.empty()) {
            ImGui::TextDisabled("Either/or: excludes %s.", ExclusiveMembers(nodes, node).c_str());
        }
        const std::string hint = PassiveNodeHint(
            blocker, rank, node.maxRanks, panel.points, canAllocate,
            conflict != nullptr ? std::string_view(conflict->displayName) : std::string_view{});
        if (!hint.empty()) ImGui::TextDisabled("%s", hint.c_str());
        if (blocker == EAllocationBlocker::None) {
            ImGui::BeginDisabled(!canAllocate);
            if (ImGui::Button("Allocate passive point")) tryAllocate(node.id);
            ImGui::EndDisabled();
        }
        ImGui::TreePop();
    };

    int hidden = 0;
    for (const FPassiveNode& node : nodes) {
        if (node.id == panel.rootNodeId) continue;
        if (!PassiveListRowVisible(PassivePanelBlocker(panel, node.id), PassiveNodeRank(allocated, node.id))) {
            ++hidden;
            continue;
        }
        drawRow(node);
    }
    if (hidden > 0 && ImGui::TreeNode("##passive-all", "Show all nodes (%d)", hidden)) {
        for (const FPassiveNode& node : nodes) {
            if (node.id == panel.rootNodeId) continue;
            if (PassiveListRowVisible(PassivePanelBlocker(panel, node.id), PassiveNodeRank(allocated, node.id))) {
                continue;
            }
            drawRow(node);
        }
        ImGui::TreePop();
    }
}

} // namespace game::ui
