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
constexpr float kMapHeightEm = 380.0f / 16.0f;
constexpr float kMapMarginEm = 28.0f / 16.0f;
constexpr float kNodeRadiusEm = 13.0f / 16.0f;
constexpr float kLabelGapEm = 3.0f / 16.0f;
constexpr float kNodeOutlineWidth = 2.0f;
constexpr float kEdgeWidth = 2.0f;
constexpr float kEdgeAllocatedWidth = 3.0f;
constexpr float kLabelPlatePadding = 2.0f;
constexpr float kLabelPlateRounding = 3.0f;
constexpr float kCoordinateMax = 100.0f;
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

/// True when the node is one edge away from the held path (a neighbor is the root or already allocated).
/// `canAllocate` also covers "no points / in battle", so adjacency is the only reason the panel may name.
bool IsAdjacentToHeld(const FPassiveNode& node, const std::vector<std::string>& allocated, std::string_view rootNodeId) {
    for (const std::string& neighborId : node.neighbors) {
        if (neighborId == rootNodeId || PassiveNodeRank(allocated, neighborId) > 0) return true;
    }
    return false;
}

/// The list suffix for a node's allocation state.
std::string ListState(const FPassiveNode& node, const std::vector<std::string>& allocated,
                      const FPassiveNode* conflict, bool canAllocate) {
    if (conflict != nullptr) return " (Excluded)";
    const int rank = PassiveNodeRank(allocated, node.id);
    if (rank > 0) {
        if (node.maxRanks > 1) return std::format(" (Rank {}/{})", rank, node.maxRanks);
        return " (Allocated)";
    }
    return canAllocate ? " (Connected)" : " (Locked)";
}

} // namespace

void DrawPassiveTreePanel(const FPassiveTreePanel& panel) {
    if (panel.nodes == nullptr || panel.allocated == nullptr || panel.nodes->empty()) return;
    const auto& nodes = *panel.nodes;
    const auto& allocated = *panel.allocated;
    const float fontPx = ImGui::GetFontSize();
    const float mapHeight = kMapHeightEm * fontPx;
    const float mapMargin = kMapMarginEm * fontPx;
    const float nodeRadius = kNodeRadiusEm * fontPx;
    const float labelGap = kLabelGapEm * fontPx;
    const auto isAllocatedOrRoot = [&](std::string_view nodeId) {
        return nodeId == panel.rootNodeId || PassiveNodeRank(allocated, nodeId) > 0;
    };

    ImGui::BeginChild("passive-tree-map", ImVec2(0, mapHeight), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 mapOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 mapSize = ImGui::GetContentRegionAvail();
    const auto positionFor = [&](const FPassiveNode& node) {
        return ImVec2(mapOrigin.x + mapMargin + node.x * std::max(1.0f, mapSize.x - mapMargin * 2.0f) / kCoordinateMax,
                      mapOrigin.y + mapMargin + node.y * std::max(1.0f, mapSize.y - mapMargin * 2.0f) / kCoordinateMax);
    };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const FPassiveNode& node : nodes) {
        for (const std::string& targetId : node.next) {
            const auto target = std::ranges::find(nodes, targetId, &FPassiveNode::id);
            if (target == nodes.end()) continue;
            const bool fromAllocated = isAllocatedOrRoot(node.id);
            const bool toAllocated = isAllocatedOrRoot(target->id);
            const ImU32 color = fromAllocated && toAllocated ? kEdgeAllocated
                : fromAllocated || toAllocated ? kEdgeConnected : kEdgeLocked;
            drawList->AddLine(positionFor(node), positionFor(*target), color,
                              fromAllocated && toAllocated ? kEdgeAllocatedWidth : kEdgeWidth);
        }
    }
    for (const FPassiveNode& node : nodes) {
        const bool nodeAllocated = isAllocatedOrRoot(node.id);
        const bool available = panel.canAllocate && panel.canAllocate(node.id);
        const ImVec2 center = positionFor(node);
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
    ImGui::SetCursorScreenPos(ImVec2(mapOrigin.x, mapOrigin.y + mapSize.y));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::TextDisabled("Gold: allocated   Blue: connected   Gray: locked");

    for (const FPassiveNode& node : nodes) {
        if (node.id == panel.rootNodeId) continue;
        const int rank = PassiveNodeRank(allocated, node.id);
        const bool nodeAllocated = rank > 0;
        const FPassiveNode* conflict = PassiveExclusiveConflict(nodes, allocated, node.id);
        const bool canAllocate = panel.canAllocate && panel.canAllocate(node.id);
        const bool atMaxRank = node.maxRanks > 1 && rank >= node.maxRanks;
        const std::string& header = node.displayName;
        const std::string state = ListState(node, allocated, conflict, canAllocate);
        if (!ImGui::TreeNode(node.id.c_str(), "%s%s", header.c_str(), state.c_str())) continue;
        ImGui::TextWrapped("%s", node.description.c_str());
        if (conflict != nullptr) {
            ImGui::TextDisabled("Exclusive with %s.", conflict->displayName.c_str());
        } else if (!node.exclusiveGroup.empty()) {
            ImGui::TextDisabled("Either/or: excludes %s.", ExclusiveMembers(nodes, node).c_str());
        }
        if (atMaxRank) {
            ImGui::TextDisabled("Maximum rank reached.");
        } else if (!nodeAllocated && !canAllocate && conflict == nullptr) {
            if (!IsAdjacentToHeld(node, allocated, panel.rootNodeId)) {
                ImGui::TextDisabled("Connect an adjacent node first.");
            } else if (panel.points <= 0) {
                ImGui::TextDisabled("Needs a node point.");
            }
        }
        if (canAllocate) {
            ImGui::BeginDisabled(panel.points <= 0);
            if (ImGui::Button("Allocate passive point")) {
                if (panel.allocate) panel.allocate(node.id);
            }
            ImGui::EndDisabled();
        }
        ImGui::TreePop();
    }
}

} // namespace game::ui
