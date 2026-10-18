#include "presentation/FPassiveTreePanel.h"

#include <algorithm>

#include <imgui.h>

namespace game::ui {

void DrawPassiveTreePanel(const FPassiveTreePanel& panel) {
    if (panel.nodes == nullptr || panel.allocated == nullptr || panel.nodes->empty()) return;
    const auto& nodes = *panel.nodes;
    const auto& allocated = *panel.allocated;
    const auto isAllocated = [&](std::string_view nodeId) {
        return std::ranges::find(allocated, nodeId) != allocated.end();
    };

    ImGui::BeginChild("passive-tree-map", ImVec2(0, 235), ImGuiChildFlags_Borders);
    const ImVec2 mapOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 mapSize = ImGui::GetContentRegionAvail();
    const auto positionFor = [&](const FPassiveNode& node) {
        constexpr float margin = 28.0f;
        return ImVec2(mapOrigin.x + margin + node.x * std::max(1.0f, mapSize.x - margin * 2.0f) / 100.0f,
                      mapOrigin.y + margin + node.y * std::max(1.0f, mapSize.y - margin * 2.0f) / 100.0f);
    };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const FPassiveNode& node : nodes) {
        if (node.parentId.empty()) continue;
        const auto parent = std::ranges::find(nodes, node.parentId, &FPassiveNode::id);
        if (parent == nodes.end()) continue;
        const bool connected = node.parentId == panel.rootNodeId || isAllocated(node.parentId);
        const bool nodeAllocated = isAllocated(node.id);
        const ImU32 color = nodeAllocated ? IM_COL32(255, 211, 105, 255)
            : connected ? IM_COL32(77, 174, 255, 210) : IM_COL32(76, 82, 99, 150);
        drawList->AddLine(positionFor(node), positionFor(*parent), color, nodeAllocated ? 3.0f : 2.0f);
    }
    for (const FPassiveNode& node : nodes) {
        const bool nodeAllocated = node.id == panel.rootNodeId || isAllocated(node.id);
        const bool available = panel.canAllocate && panel.canAllocate(node.id);
        const ImVec2 center = positionFor(node);
        const ImU32 fill = nodeAllocated ? IM_COL32(255, 199, 82, 255)
            : available ? IM_COL32(56, 132, 190, 255) : IM_COL32(59, 63, 76, 255);
        drawList->AddCircleFilled(center, 15.0f, fill, 20);
        drawList->AddCircle(center, 15.0f, IM_COL32(222, 228, 240, 220), 20, 2.0f);
        const std::string mark = node.displayName.substr(0, 1);
        const ImVec2 markSize = ImGui::CalcTextSize(mark.c_str());
        drawList->AddText(ImVec2(center.x - markSize.x * 0.5f, center.y - markSize.y * 0.5f),
                          IM_COL32(20, 24, 32, 255), mark.c_str());
        const ImVec2 labelSize = ImGui::CalcTextSize(node.displayName.c_str());
        drawList->AddText(ImVec2(center.x - labelSize.x * 0.5f, center.y + 17.0f),
                          IM_COL32(220, 224, 234, 255), node.displayName.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(mapOrigin.x, mapOrigin.y + mapSize.y));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::TextDisabled("Gold: allocated   Blue: connected   Gray: locked");

    for (const FPassiveNode& node : nodes) {
        if (node.id == panel.rootNodeId) continue;
        const bool nodeAllocated = isAllocated(node.id);
        const bool canAllocate = panel.canAllocate && panel.canAllocate(node.id);
        const char* state = nodeAllocated ? " (Allocated)" : canAllocate ? " (Connected)" : " (Locked)";
        if (!ImGui::TreeNode(node.id.c_str(), "%s%s", node.displayName.c_str(), state)) continue;
        ImGui::TextWrapped("%s", node.description.c_str());
        if (canAllocate) {
            ImGui::BeginDisabled(panel.points <= 0);
            if (ImGui::Button("Allocate passive point")) {
                if (panel.allocate) panel.allocate(node.id);
            }
            ImGui::EndDisabled();
        } else if (!nodeAllocated) {
            const auto parent = std::ranges::find(nodes, node.parentId, &FPassiveNode::id);
            ImGui::TextDisabled("Connect %s first.", parent == nodes.end()
                ? node.parentId.c_str() : parent->displayName.c_str());
        }
        ImGui::TreePop();
    }
}

} // namespace game::ui
