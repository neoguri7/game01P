#include "FHubState.h"

#include "core/InputState.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/run/FPrototypeRun.h"

#include <algorithm>
#include <string>
#include <vector>

#include <imgui.h>

namespace {

void DrawSkillSockets(
    game::FPrototypeRun& run,
    const game::gameplay::FContentRegistry& content,
    game::FPreparedMember& member,
    game::FSkillSlot& slot,
    bool canEdit) {
    const auto* skill = content.skills.find(slot.skillId);
    if (!skill) return;

    ImGui::Text("Support sockets %zu/%d", slot.runeIds.size(), skill->runeSockets);
    for (std::size_t index = 0; index < slot.runeIds.size();) {
        const std::string runeId = slot.runeIds[index];
        const auto* rune = content.runes.find(runeId);
        const std::string label = std::string(rune ? rune->displayName : runeId) + "  [x]";
        ImGui::PushID(runeId.c_str());
        ImGui::BeginDisabled(!canEdit);
        const bool remove = ImGui::SmallButton(label.c_str());
        ImGui::EndDisabled();
        ImGui::PopID();
        if (remove && run.UnequipRune(member.placementId, slot.skillId, runeId)) continue;
        ++index;
    }

    std::vector<std::string> shownRuneIds;
    for (const auto& rune : content.runes.rows) {
        if (std::ranges::find(shownRuneIds, rune.id) != shownRuneIds.end()) continue;
        shownRuneIds.push_back(rune.id);
        const std::size_t owned = static_cast<std::size_t>(std::ranges::count(run.runeInventory, rune.id));
        if (owned == 0) continue;
        game::FSkillSlot candidate = slot;
        candidate.runeIds.push_back(rune.id);
        const std::optional<std::string> problem = game::ValidateSkillSlot(candidate, content);
        const std::string label = "Socket " + rune.displayName + " x" + std::to_string(owned);
        ImGui::PushID(rune.id.c_str());
        ImGui::BeginDisabled(!canEdit || problem.has_value());
        const bool attach = ImGui::SmallButton(label.c_str());
        ImGui::EndDisabled();
        if (problem && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Cannot socket: %s", problem->c_str());
        }
        ImGui::PopID();
        if (attach) (void)run.EquipRune(member.placementId, slot.skillId, rune.id, content);
    }
}

void DrawMemberLoadout(
    game::FPrototypeRun& run,
    const game::gameplay::FContentRegistry& content,
    game::FPreparedMember& member,
    const game::gameplay::FUnitContent& unit) {
    ImGui::BeginDisabled(run.active);
    ImGui::Checkbox(unit.displayName.c_str(), &member.enabled);
    ImGui::EndDisabled();
    if (!member.enabled) return;

    const bool canEdit = run.CanEditLoadout();
    ImGui::BeginDisabled(!canEdit);
    for (const std::string& skillId : unit.skillIds) {
        const auto* skill = content.skills.find(skillId);
        if (!skill) continue;
        const auto slot = std::ranges::find(member.skills, skillId, &game::FSkillSlot::skillId);
        bool equipped = slot != member.skills.end();
        ImGui::BeginDisabled(!equipped && member.skills.size() >= static_cast<std::size_t>(run.skillSlots));
        if (ImGui::Checkbox(skill->displayName.c_str(), &equipped)) {
            if (equipped) member.skills.push_back({skillId, {}});
            else (void)run.UnequipSkill(member.placementId, skillId);
        }
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();

    if (member.skills.empty()) {
        ImGui::TextDisabled("Equip a skill to open its support sockets.");
        return;
    }
    ImGui::TextDisabled("Expand a skill to configure its support sockets.");
    for (game::FSkillSlot& slot : member.skills) {
        const auto* skill = content.skills.find(slot.skillId);
        if (!skill) continue;
        ImGui::PushID(slot.skillId.c_str());
        const std::string label = skill->displayName + "  [" + std::to_string(slot.runeIds.size()) + "/"
            + std::to_string(skill->runeSockets) + " supports]";
        if (ImGui::TreeNode(label.c_str())) {
            DrawSkillSockets(run, content, member, slot, canEdit);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void DrawPassiveProgression(
    game::FPrototypeRun& run,
    const game::gameplay::FContentRegistry& content,
    int& selectedMemberIndex) {
    ImGui::SeparatorText("Run XP / Passive Tree");
    if (!run.active || run.party.empty()) {
        ImGui::TextDisabled("Experience is run-scoped. Clear a battle to earn XP and a connected passive point.");
        return;
    }

    selectedMemberIndex = std::clamp(selectedMemberIndex, 0, static_cast<int>(run.party.size()) - 1);
    const auto memberLabel = [&](std::size_t index) {
        const auto* unit = content.units.find(run.party[index].unitId);
        return unit ? unit->displayName : run.party[index].placementId;
    };
    const std::string currentLabel = memberLabel(static_cast<std::size_t>(selectedMemberIndex));
    if (ImGui::BeginCombo("Character", currentLabel.c_str())) {
        for (std::size_t index = 0; index < run.party.size(); ++index) {
            const bool selected = static_cast<int>(index) == selectedMemberIndex;
            const std::string label = memberLabel(index);
            if (ImGui::Selectable(label.c_str(), selected)) selectedMemberIndex = static_cast<int>(index);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const game::FPreparedMember& member = run.party[static_cast<std::size_t>(selectedMemberIndex)];
    const int requiredExperience = run.ExperienceForNextLevel(member);
    ImGui::Text("Level %d   EXP %d / %d   Passive points %d", member.level, member.experience,
                requiredExperience, member.passivePoints);
    const game::FPassiveStatBonuses bonuses = run.PassiveBonuses(member.placementId);
    ImGui::TextDisabled("Tree bonuses: +%d HP, +%d ATK, +%d MATK, +%d DEF, +%d MDEF, +%d speed",
                        bonuses.maxHealth, bonuses.attack, bonuses.magicAttack, bonuses.defense,
                        bonuses.magicDefense, bonuses.speed);

    ImGui::BeginChild("passive-tree-map", ImVec2(0, 235), ImGuiChildFlags_Borders);
    const ImVec2 mapOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 mapSize = ImGui::GetContentRegionAvail();
    const auto positionFor = [&](const game::FPassiveNode& node) {
        constexpr float margin = 28.0f;
        return ImVec2(mapOrigin.x + margin + node.x * std::max(1.0f, mapSize.x - margin * 2.0f) / 100.0f,
                      mapOrigin.y + margin + node.y * std::max(1.0f, mapSize.y - margin * 2.0f) / 100.0f);
    };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const game::FPassiveNode& node : run.passiveNodes) {
        if (node.parentId.empty()) continue;
        const auto parent = std::ranges::find(run.passiveNodes, node.parentId, &game::FPassiveNode::id);
        if (parent == run.passiveNodes.end()) continue;
        const bool connected = node.parentId == run.passiveRootNodeId
            || std::ranges::find(member.allocatedNodes, node.parentId) != member.allocatedNodes.end();
        const bool allocated = std::ranges::find(member.allocatedNodes, node.id) != member.allocatedNodes.end();
        const ImU32 color = allocated ? IM_COL32(255, 211, 105, 255)
            : connected ? IM_COL32(77, 174, 255, 210) : IM_COL32(76, 82, 99, 150);
        drawList->AddLine(positionFor(node), positionFor(*parent), color, allocated ? 3.0f : 2.0f);
    }
    for (const game::FPassiveNode& node : run.passiveNodes) {
        const bool allocated = node.id == run.passiveRootNodeId
            || std::ranges::find(member.allocatedNodes, node.id) != member.allocatedNodes.end();
        const bool available = run.CanAllocatePassiveNode(member.placementId, node.id);
        const ImVec2 center = positionFor(node);
        const ImU32 fill = allocated ? IM_COL32(255, 199, 82, 255)
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

    for (const game::FPassiveNode& node : run.passiveNodes) {
        if (node.id == run.passiveRootNodeId) continue;
        const bool allocated = std::ranges::find(member.allocatedNodes, node.id) != member.allocatedNodes.end();
        const bool canAllocate = run.CanAllocatePassiveNode(member.placementId, node.id);
        const char* state = allocated ? " (Allocated)" : canAllocate ? " (Connected)" : " (Locked)";
        if (!ImGui::TreeNode(node.id.c_str(), "%s%s", node.displayName.c_str(), state)) continue;
        ImGui::TextWrapped("%s", node.description.c_str());
        if (canAllocate) {
            ImGui::BeginDisabled(member.passivePoints <= 0);
            if (ImGui::Button("Allocate passive point")) {
                (void)run.AllocatePassiveNode(member.placementId, node.id);
            }
            ImGui::EndDisabled();
        } else if (!allocated) {
            const auto parent = std::ranges::find(run.passiveNodes, node.parentId, &game::FPassiveNode::id);
            ImGui::TextDisabled("Connect %s first.", parent == run.passiveNodes.end()
                ? node.parentId.c_str() : parent->displayName.c_str());
        }
        ImGui::TreePop();
    }
}

} // namespace

void FHubState::OnEnter(entt::registry& Registry)
{
    (void)Registry;
    proceedRequested = false;
    backRequested = false;
    selectedProgressionMember = 0;
}

void FHubState::OnExit(entt::registry& Registry)
{
    (void)Registry;
}

EStateTransition FHubState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    const auto* input = Registry.ctx().find<game::FInputState>();
    auto* run = Registry.ctx().find<game::FPrototypeRun>();
    const auto* content = Registry.ctx().find<game::gameplay::FContentRegistry>();
    if (!run || !content) return EStateTransition::None;
    if (backRequested || (input && input->isActionPressed(game::EInputAction::Cancel))) {
        run->End();
        return EStateTransition::ToTitle;
    }
    if (const auto* room = run->CurrentRoom(); room && room->kind == game::EPrototypeRoom::Battle) {
        return EStateTransition::ToBattle;
    }
    if (proceedRequested || (input && input->isActionPressed(game::EInputAction::Confirm))) {
        proceedRequested = false;
        if (run->active) {
            if (run->CanStart(*content)) run->Advance();
        } else {
            (void)run->Start(*content);
        }
        if (const auto* room = run->CurrentRoom(); room && room->kind == game::EPrototypeRoom::Battle) {
            return EStateTransition::ToBattle;
        }
    }
    return EStateTransition::None;
}

void FHubState::Render(entt::registry& Registry)
{
    auto* run = Registry.ctx().find<game::FPrototypeRun>();
    const auto* content = Registry.ctx().find<game::gameplay::FContentRegistry>();
    if (!run || !content) return;
    const auto* room = run->CurrentRoom();
    const bool preparation = !room || room->kind == game::EPrototypeRoom::Camp;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(1140, 690), ImGuiCond_FirstUseEver);
    ImGui::Begin("Party / Run", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Cleared dungeon checkpoint: %u (session only)", run->completedDungeons);
    if (room) {
        ImGui::SeparatorText(room->title.c_str());
        ImGui::Text("Dungeon %u | room %zu/%zu", run->dungeonIndex + 1, run->roomIndex + 1, run->rooms.size());
    }
    ImGui::BeginChild("hub-content", ImVec2(0, -76));
    if (preparation) {
        ImGui::TextWrapped("Choose your party and skill gems. At the hub and camp, socket owned support runes into compatible skills.");
        for (std::size_t index = 0; index < run->party.size(); ++index) {
            auto& member = run->party[index];
            const auto* unit = content->units.find(member.unitId);
            if (!unit) continue;
            ImGui::PushID(member.placementId.c_str());
            ImGui::SetNextItemOpen(index == 0, ImGuiCond_FirstUseEver);
            if (ImGui::CollapsingHeader(unit->displayName.c_str())) {
                DrawMemberLoadout(*run, *content, member, *unit);
            }
            ImGui::PopID();
        }
    } else {
        ImGui::TextWrapped("Waystone: allocate earned passive points, then continue to the next chamber.");
    }
    DrawPassiveProgression(*run, *content, selectedProgressionMember);
    ImGui::EndChild();

    const bool valid = run->CanStart(*content);
    ImGui::BeginDisabled(!valid || (run->active && room && room->kind == game::EPrototypeRoom::Battle));
    if (ImGui::Button(run->active ? "Continue" : "Start run", ImVec2(-1, 40))) proceedRequested = true;
    ImGui::EndDisabled();
    if (!valid) ImGui::TextWrapped("Enable at least one member and equip one to four legal skills per member.");
    if (ImGui::Button(run->active ? "Abandon run / title" : "Title", ImVec2(-1, 30))) backRequested = true;
    ImGui::End();
}
