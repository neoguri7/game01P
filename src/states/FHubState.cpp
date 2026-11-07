#include "FHubState.h"

#include "FBattleState.h"
#include "FTitleState.h"
#include "core/InputState.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/run/FPrototypeRun.h"
#include "presentation/FPassiveTreePanel.h"

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

    game::ui::DrawPassiveTreePanel(game::ui::FPassiveTreePanel{
        .nodes = &run.tree.nodes,
        .rootNodeId = run.tree.rootNodeId,
        .allocated = &member.allocatedNodes,
        .points = member.passivePoints,
        .canAllocate = [&](std::string_view nodeId) { return run.CanAllocatePassiveNode(member.placementId, nodeId); },
        .allocate = [&](std::string_view nodeId) { (void)run.AllocatePassiveNode(member.placementId, nodeId); },
    });
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

FStateTransition FHubState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    const auto* input = Registry.ctx().find<game::FInputState>();
    auto* run = Registry.ctx().find<game::FPrototypeRun>();
    const auto* content = Registry.ctx().find<game::gameplay::FContentRegistry>();
    if (!run || !content) return {};
    if (backRequested || (input && input->isActionPressed(game::EInputAction::Cancel))) {
        run->End();
        return {EStateChangeKind::ReplaceTop, std::string(kForestTitleId)};
    }
    if (const auto* room = run->CurrentRoom(); room && room->kind == game::EPrototypeRoom::Battle) {
        return {EStateChangeKind::ReplaceTop, std::string(kForestBattleId)};
    }
    if (proceedRequested || (input && input->isActionPressed(game::EInputAction::Confirm))) {
        proceedRequested = false;
        if (run->active) {
            if (run->CanStart(*content)) run->Advance();
        } else {
            (void)run->Start(*content);
        }
        if (const auto* room = run->CurrentRoom(); room && room->kind == game::EPrototypeRoom::Battle) {
            return {EStateChangeKind::ReplaceTop, std::string(kForestBattleId)};
        }
    }
    return {};
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
