#include "ecs/systems/EmberCombatHudSystem.h"

#include <algorithm>
#include <imgui.h>
#include "core/EngineState.h"
#include "core/InputState.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTerrain.h"

namespace game::ecs {
namespace {
constexpr ImVec4 Gold{1.f, .86f, .24f, 1.f};
constexpr ImVec4 Blue{.31f, .55f, 1.f, 1.f};
constexpr ImVec4 Red{.9f, .31f, .31f, 1.f};

bool CanTarget(entt::registry& registry, entt::entity actor, entt::entity target, const FResolvedSkill& skill) {
    if (!IsAlive(registry, target) || !registry.all_of<FHealth, FGridPosition>(target)) return false;
    const bool own = skill.target == "ally" || skill.target == "self";
    if (registry.all_of<FPartyMember>(target) != own || (skill.target == "self" && target != actor)) return false;
    const auto from = registry.get<FGridPosition>(actor);
    const auto to = registry.get<FGridPosition>(target);
    return IsInRange(from, to, skill.range) && HasGridLineOfSight(registry, from, to);
}
} // namespace

void EmberCombatHudSystem::update(entt::registry& registry, float) {
    auto* selection = registry.ctx().find<FGridSelection>();
    auto* runtime = registry.ctx().find<FTurnRuntime>();
    const auto* outcome = registry.ctx().find<FBattleOutcome>();
    auto* input = registry.ctx().find<FInputState>();
    if (!selection || !runtime || !outcome || !input) return;
    selection->reachable.clear();
    selection->affected.clear();
    selection->hovered.reset();
    selection->selected = entt::null;
    const auto snapshot = runtime->controller.snapshot();
    if (outcome->resolved || !snapshot.currentTurn) return;
    const auto token = *snapshot.currentTurn;
    const auto actor = token.actor;
    selection->selected = actor;
    if (boundTurn != token.turnSerial) { boundTurn = token.turnSerial; selectedSkill = 0; }
    if (!IsAlive(registry, actor) || !registry.all_of<FPartyMember, FActionPoints, FSkillLoadout>(actor)) return;
    auto& queue = registry.ctx().get<FActionQueue>();
    const auto& points = registry.get<FActionPoints>(actor);
    const auto& terrain = registry.ctx().get<FGridTerrain>();
    if (points.move > 0) selection->reachable = ComputeReachableCells(registry, actor, terrain.moveRange);
    const auto& loadout = registry.get<FSkillLoadout>(actor);
    constexpr EInputAction skills[]{EInputAction::Skill1, EInputAction::Skill2, EInputAction::Skill3, EInputAction::Skill4};
    for (int index = 0; index < 4; ++index) {
        if (input->isActionPressed(skills[index]) && index < static_cast<int>(loadout.slots.size())) selectedSkill = index;
    }
    if (input->isActionPressed(EInputAction::EndTurn)) {
        if (queue.pending.empty()) runtime->requests.push_back(FEndTurn{token});
        return;
    }
    if (!queue.pending.empty()) return;
    auto destination = registry.get<FGridPosition>(actor);
    bool keyMove = false;
    if (input->isActionPressed(EInputAction::MoveUp)) { --destination.y; keyMove = true; }
    else if (input->isActionPressed(EInputAction::MoveDown)) { ++destination.y; keyMove = true; }
    else if (input->isActionPressed(EInputAction::MoveLeft)) { --destination.x; keyMove = true; }
    else if (input->isActionPressed(EInputAction::MoveRight)) { ++destination.x; keyMove = true; }
    if (keyMove) { queue.pending.push_back(FMoveRequest{actor, destination, token}); return; }
    if (input->uiCapturesMouse) return;
    FCamera camera{};
    for (const auto entity : registry.view<FCamera, FTag>()) {
        if (registry.get<FTag>(entity).value == "battle_camera") camera = registry.get<FCamera>(entity);
    }
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const auto& grid = registry.ctx().get<FGridMetrics>();
    int x = 0, y = 0;
    if (!WorldToGridCell(grid, ScreenToWorld(input->mousePos, camera,
            FViewport{static_cast<float>(window.width), static_cast<float>(window.height)}), x, y)) return;
    selection->hovered = FGridPosition{x, y};
    const auto target = FindGridOccupant(registry, x, y);
    if (selectedSkill >= 0 && selectedSkill < static_cast<int>(loadout.slots.size())) {
        const auto& slot = loadout.slots[selectedSkill];
        const auto skill = ResolveSkillSlot(slot, registry.ctx().get<gameplay::FContentRegistry>());
        if (skill && skill->target == "cell") {
            const auto from = registry.get<FGridPosition>(actor);
            if (IsInRange(from, {x, y}, skill->range) && HasGridLineOfSight(registry, from, {x, y})) {
                selection->affected = ComputeSkillCells(*skill, {x, y}, from);
            }
            if (input->mouseLeftPressed) {
                queue.pending.push_back(FActionRequest{actor, target, slot.skillId, token, {x, y}});
            }
            return;
        }
    }
    if (target != entt::null && selectedSkill < static_cast<int>(loadout.slots.size())) {
        const auto& slot = loadout.slots[selectedSkill];
        const auto skill = ResolveSkillSlot(slot, registry.ctx().get<gameplay::FContentRegistry>());
        if (skill && CanTarget(registry, actor, target, *skill)) {
            selection->affected = ComputeSkillCells(*skill, {x, y}, registry.get<FGridPosition>(actor));
        }
        if (input->mouseLeftPressed) queue.pending.push_back(FActionRequest{actor, target, slot.skillId, token, {x, y}});
    } else if (input->mouseLeftPressed) {
        queue.pending.push_back(FMoveRequest{actor, {x, y}, token});
    }
}

void EmberCombatHudSystem::render(entt::registry& registry) {
    auto* outcome = registry.ctx().find<FBattleOutcome>();
    if (!outcome) return;
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const auto& run = registry.ctx().get<FDungeonRun>();
    auto& runtime = registry.ctx().get<FTurnRuntime>();
    auto& queue = registry.ctx().get<FActionQueue>();
    const auto& content = registry.ctx().get<gameplay::FContentRegistry>();
    const auto snapshot = runtime.controller.snapshot();
    const float width = static_cast<float>(window.width);
    const float height = static_cast<float>(window.height);
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus;

    ImGui::SetNextWindowPos({24, 18});
    ImGui::SetNextWindowSize({width - 48, 112});
    ImGui::SetNextWindowScroll({0, 0});
    ImGui::Begin("Expedition", nullptr, flags | ImGuiWindowFlags_NoBackground);
    ImGui::TextColored(Gold, "EMBER DUNGEON");
    ImGui::SameLine(width * .57f);
    ImGui::Text("DEPTH %02d  |  ROOM %d / 3  |  RUN LEVEL %d", run.depth, run.room + 1, run.level);
    ImGui::TextDisabled("%s  /  seed %u", run.roomName.c_str(), run.seed);
    ImGui::SameLine(width * .57f);
    ImGui::TextDisabled("EXP %d  |  materials %d  |  checkpoint %d", run.experience, run.materials, run.cleared);
    ImGui::Separator();
    ImGui::Text("ROUND %llu", static_cast<unsigned long long>(snapshot.roundNumber));
    for (const auto& entry : snapshot.entries) {
        if (entry.state == ETurnEntryState::Skipped || entry.state == ETurnEntryState::Completed) continue;
        ImGui::SameLine();
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entry.entity;
        const auto name = UnitDisplayName(registry, entry.entity);
        ImGui::TextColored(current ? Gold : registry.all_of<FPartyMember>(entry.entity) ? Blue : Red,
                           "%s%s", current ? "> " : " / ", name.substr(0, name.find(" /")).c_str());
    }
    ImGui::End();

    ImGui::SetNextWindowPos({width * .59f, 128});
    ImGui::SetNextWindowSize({width * .41f - 28, height - 152});
    ImGui::Begin("Tactics", nullptr, flags);
    ImGui::SeparatorText("YOUR PARTY");
    for (const auto entity : registry.view<FPartyMember, FHealth>()) {
        const auto& hp = registry.get<FHealth>(entity);
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entity;
        ImGui::TextColored(current ? Gold : Blue, "%s%s", current ? "> " : "", UnitDisplayName(registry, entity).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%d/%d%s", hp.current, hp.max, IsAlive(registry, entity) ? "" : "  KO");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Blue);
        ImGui::ProgressBar(static_cast<float>(hp.current) / std::max(1, hp.max), {-1, 4}, "");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::SeparatorText("ACTION");
    if (!outcome->resolved && snapshot.currentTurn) {
        const auto actor = snapshot.currentTurn->actor;
        if (IsAlive(registry, actor) && registry.all_of<FPartyMember, FActionPoints, FSkillLoadout>(actor)) {
            const auto& points = registry.get<FActionPoints>(actor);
            ImGui::TextColored(Gold, "%s", UnitDisplayName(registry, actor).c_str());
            ImGui::Text("MOVE  %d / %d   |   SKILL  %d / %d", points.move, points.moveMax, points.skill, points.skillMax);
            ImGui::TextDisabled("Click a cyan cell to move up to %d cells.", registry.ctx().get<FGridTerrain>().moveRange);
            const auto& slots = registry.get<FSkillLoadout>(actor).slots;
            for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
                ImGui::PushID(index);
                const auto resolved = ResolveSkillSlot(slots[index], content);
                const auto* row = content.skills.find(slots[index].skillId);
                const std::string label = std::to_string(index + 1) + "  " +
                    (row ? row->displayName : slots[index].skillId);
                if (ImGui::Selectable(label.c_str(), selectedSkill == index)) selectedSkill = index;
                if (ImGui::IsItemHovered() && resolved) {
                    ImGui::SetTooltip("AP %d | range %d | power %d | %s\nRunes: %zu | statuses: %zu",
                        resolved->apCost, resolved->range, resolved->power, resolved->shape.c_str(),
                        slots[index].runeIds.size(), resolved->statuses.size());
                }
                ImGui::PopID();
            }
            if (selectedSkill >= 0 && selectedSkill < static_cast<int>(slots.size())) {
                const auto skill = ResolveSkillSlot(slots[selectedSkill], content);
                if (skill) {
                    ImGui::TextDisabled("Range %d | AP %d | %s", skill->range, skill->apCost, skill->shape.c_str());
                    ImGui::TextDisabled("Click a unit or select a valid target below.");
                    for (const auto target : registry.view<FHealth, FGridPosition>()) {
                        if (!CanTarget(registry, actor, target, *skill)) continue;
                        ImGui::PushID(static_cast<int>(entt::to_integral(target)));
                        ImGui::BeginDisabled(points.skill < skill->apCost || !queue.pending.empty());
                        if (ImGui::SmallButton(UnitDisplayName(registry, target).c_str())) {
                            queue.pending.push_back(FActionRequest{actor, target, slots[selectedSkill].skillId,
                                *snapshot.currentTurn, registry.get<FGridPosition>(target)});
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                }
            }
            ImGui::BeginDisabled(!queue.pending.empty());
            if (ImGui::Button("END TURN  [Space]", {-1, 32})) runtime.requests.push_back(FEndTurn{*snapshot.currentTurn});
            ImGui::EndDisabled();
        } else {
            ImGui::TextColored(Red, "Enemy is planning...");
        }
    } else {
        ImGui::TextDisabled("Room resolved. Choose the next step.");
    }
    ImGui::Spacing();
    ImGui::SeparatorText("BATTLE JOURNAL");
    ImGui::BeginChild("journal", {0, 0});
    if (const auto* log = registry.ctx().find<FCombatLog>()) {
        const auto start = log->lines.size() > 7 ? log->lines.size() - 7 : 0;
        for (auto index = start; index < log->lines.size(); ++index) ImGui::TextWrapped("%s", log->lines[index].c_str());
    }
    ImGui::EndChild();
    ImGui::End();

    ImGui::SetNextWindowPos({24, height - 76});
    ImGui::SetNextWindowSize({width * .56f, 72});
    ImGui::SetNextWindowScroll({0, 0});
    ImGui::Begin("Controls", nullptr, flags | ImGuiWindowFlags_NoBackground);
    ImGui::TextDisabled("WASD move  |  1-4 skill  |  click target  |  Space end turn");
    ImGui::TextDisabled("Yellow = active  /  Cyan = reachable  /  Spikes hurt along the path");
    ImGui::End();
}

} // namespace game::ecs
