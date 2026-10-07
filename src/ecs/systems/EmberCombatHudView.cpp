#include "ecs/systems/EmberCombatHudSystem.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <imgui.h>
#include "core/EngineState.h"
#include "core/InputState.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/grid/FGridTerrain.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FUiTheme.h"

namespace game::ecs {
namespace {

namespace pal = ui::palette;

EDungeonSprite SpriteOf(const entt::registry& registry, entt::entity entity) {
    const auto* art = registry.try_get<FUnitArt>(entity);
    return art ? art->sprite : EDungeonSprite::Warrior;
}

std::string FirstName(const std::string& full) { return full.substr(0, full.find(" /")); }

std::string RoleName(const std::string& full) {
    auto at = full.find('/');
    if (at == std::string::npos) return {};
    ++at;
    while (at < full.size() && full[at] == ' ') ++at;
    return full.substr(at);
}

EDungeonSprite SkillIcon(const FResolvedSkill& skill) {
    if (skill.damageType == gameplay::kDamageTypeMagical) return EDungeonSprite::IconSpell;
    return skill.range > 2 ? EDungeonSprite::IconBow : EDungeonSprite::IconSword;
}

// Head-and-shoulders crop of a full-body sprite inside a framed square.
void DrawPortrait(entt::registry& registry, ImDrawList* draw, entt::entity entity, ImVec2 min, float size,
                  ImU32 trim, bool glow, bool dim) {
    const ImVec2 max{min.x + size, min.y + size};
    ui::DrawSlotFrame(draw, min, max, trim, glow);
    DrawDungeonImage(registry, SpriteOf(registry, entity), draw, {min.x + 4, min.y + 4}, {max.x - 4, max.y - 4},
                     {.18f, .0f}, {.82f, .64f}, dim ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE);
}

ImU32 FactionTrim(const entt::registry& registry, entt::entity entity, bool current) {
    if (current) return pal::kGoldBright;
    return registry.all_of<FPartyMember>(entity) ? pal::kParty : pal::kEnemy;
}

void TopBar(entt::registry& registry, float width, const FDungeonRun& run, const FTurnSnapshot& snapshot) {
    if (!ui::BeginPanel("Expedition", {16.f, 12.f}, {width - 32.f, 88.f})) {
        ui::EndPanel();
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    ui::HeadingText("EMBER DUNGEON", 26.f);
    ImGui::TextDisabled("%s  /  seed %u", run.roomName.c_str(), run.seed);

    float x = origin.x + 300.f;
    const float y = origin.y + 24.f;
    char round[32];
    std::snprintf(round, sizeof round, "ROUND %llu", static_cast<unsigned long long>(snapshot.roundNumber));
    ImGui::SetCursorScreenPos({x, y + 11.f});
    ImGui::TextDisabled("%s", round);
    x += 84.f;
    for (const auto& entry : snapshot.entries) {
        if (entry.state == ETurnEntryState::Skipped || entry.state == ETurnEntryState::Completed) continue;
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entry.entity;
        const ImVec2 min{x, y};
        DrawPortrait(registry, draw, entry.entity, min, 40.f, FactionTrim(registry, entry.entity, current), current,
                     !IsAlive(registry, entry.entity));
        if (ImGui::IsMouseHoveringRect(min, {min.x + 40.f, min.y + 40.f})) {
            ImGui::SetTooltip("%s", UnitDisplayName(registry, entry.entity).c_str());
        }
        x += 46.f;
    }

    char depth[96];
    std::snprintf(depth, sizeof depth, "DEPTH %02d  |  ROOM %d / 3  |  RUN LEVEL %d", run.depth, run.room + 1, run.level);
    char stats[96];
    std::snprintf(stats, sizeof stats, "EXP %d  |  materials %d  |  checkpoint %d", run.experience, run.materials, run.cleared);
    const float right = origin.x + size.x - 24.f;
    ImFont* heading = ui::HeadingFont();
    if (heading) ImGui::PushFont(heading, 18.f);
    const ImVec2 depthSize = ImGui::CalcTextSize(depth);
    ImGui::SetCursorScreenPos({right - depthSize.x, origin.y + 18.f});
    ImGui::TextColored(pal::kGoldV, "%s", depth);
    if (heading) ImGui::PopFont();
    ImGui::SetCursorScreenPos({right - ImGui::CalcTextSize(stats).x, origin.y + 46.f});
    ImGui::TextDisabled("%s", stats);
    ui::EndPanel();
}

void PartyCards(entt::registry& registry, const FTurnSnapshot& snapshot) {
    ui::SectionTitle("YOUR PARTY");
    for (const auto entity : registry.view<FPartyMember, FHealth>()) {
        const auto& hp = registry.get<FHealth>(entity);
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entity;
        const bool alive = IsAlive(registry, entity);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        DrawPortrait(registry, draw, entity, at, 50.f, current ? pal::kGoldBright : pal::kBronze, current, !alive);

        const auto full = UnitDisplayName(registry, entity);
        ImGui::SetCursorScreenPos({at.x + 62.f, at.y - 1.f});
        ImGui::TextColored(current ? pal::kGoldBrightV : pal::kTextV, "%s%s", current ? "> " : "", FirstName(full).c_str());
        ImGui::SameLine(0.f, 10.f);
        ImGui::TextDisabled("%s", RoleName(full).c_str());

        const ImVec2 barMin{at.x + 62.f, at.y + 26.f};
        const ImVec2 barMax{at.x + width, at.y + 44.f};
        ui::DrawLifeBar(draw, barMin, barMax, static_cast<float>(hp.current) / std::max(1, hp.max));
        char numbers[32];
        std::snprintf(numbers, sizeof numbers, alive ? "%d / %d" : "KO", hp.current, hp.max);
        const ImVec2 numbersSize = ImGui::CalcTextSize(numbers);
        draw->AddText({(barMin.x + barMax.x - numbersSize.x) * .5f, barMin.y + (18.f - numbersSize.y) * .5f - 1.f},
                      IM_COL32(245, 232, 205, 255), numbers);
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, 58.f});
    }
}

} // namespace

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

    TopBar(registry, width, run, snapshot);

    const bool playerTurn = !outcome->resolved && snapshot.currentTurn && IsAlive(registry, snapshot.currentTurn->actor) &&
        registry.all_of<FPartyMember, FActionPoints, FSkillLoadout>(snapshot.currentTurn->actor);

    ui::BeginPanel("Tactics", {width * .59f, 112.f}, {width * .41f - 16.f, height - 128.f});
    PartyCards(registry, snapshot);
    ImGui::Spacing();
    ui::SectionTitle("ACTION");
    if (playerTurn) {
        const auto actor = snapshot.currentTurn->actor;
        const auto& points = registry.get<FActionPoints>(actor);
        const auto& slots = registry.get<FSkillLoadout>(actor).slots;
        ImGui::TextColored(pal::kGoldBrightV, "%s", UnitDisplayName(registry, actor).c_str());
        ImGui::Text("MOVE  %d / %d   |   SKILL  %d / %d", points.move, points.moveMax, points.skill, points.skillMax);
        ImGui::TextDisabled("Click a cyan cell to move up to %d cells.", registry.ctx().get<FGridTerrain>().moveRange);
        if (selectedSkill >= 0 && selectedSkill < static_cast<int>(slots.size())) {
            const auto skill = ResolveSkillSlot(slots[selectedSkill], content);
            const auto* row = content.skills.find(slots[selectedSkill].skillId);
            if (skill && row) {
                ImGui::TextColored(pal::kGoldV, "%s", row->displayName.c_str());
                ImGui::TextDisabled("Range %d | AP %d | %s", skill->range, skill->apCost, skill->shape.c_str());
                ImGui::TextDisabled("Click a unit or select a valid target below.");
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8.f, 3.f});
                for (const auto target : registry.view<FHealth, FGridPosition>()) {
                    if (!CanTargetWithSkill(registry, actor, target, *skill)) continue;
                    ImGui::PushID(static_cast<int>(entt::to_integral(target)));
                    ImGui::BeginDisabled(points.skill < skill->apCost || !queue.pending.empty());
                    if (ui::Button(UnitDisplayName(registry, target).c_str())) {
                        queue.pending.push_back(FActionRequest{actor, target, slots[selectedSkill].skillId,
                            *snapshot.currentTurn, registry.get<FGridPosition>(target)});
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    ImGui::SameLine();
                }
                ImGui::PopStyleVar();
                ImGui::NewLine();
            }
        }
    } else if (!outcome->resolved && snapshot.currentTurn) {
        ImGui::TextColored(pal::kEnemyV, "Enemy is planning...");
    } else {
        ImGui::TextDisabled("Room resolved. Choose the next step.");
    }
    ImGui::Spacing();
    ui::SectionTitle("BATTLE JOURNAL");
    ImGui::BeginChild("journal", {0, -46.f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (const auto* log = registry.ctx().find<FCombatLog>()) {
        const auto start = log->lines.size() > 7 ? log->lines.size() - 7 : 0;
        for (auto index = start; index < log->lines.size(); ++index) {
            ImGui::PushStyleColor(ImGuiCol_Text, index + 1 == log->lines.size() ? pal::kTextV : pal::kMutedV);
            ImGui::TextWrapped("%s", log->lines[index].c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("WASD move  |  1-4 skill  |  click target  |  Space end turn");
    ImGui::TextDisabled("Yellow = active  /  Cyan = reachable  /  Spikes hurt along the path");
    ui::EndPanel();

    const float barWidth = width * .59f - 28.f;
    ui::BeginPanel("Skills", {16.f, height - 108.f}, {barWidth, 96.f});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    if (playerTurn) {
        const auto actor = snapshot.currentTurn->actor;
        const auto& points = registry.get<FActionPoints>(actor);
        const auto& slots = registry.get<FSkillLoadout>(actor).slots;
        DrawPortrait(registry, draw, actor, {origin.x + 22.f, origin.y + 16.f}, 64.f, pal::kGoldBright, true, false);

        const float gemX = origin.x + 124.f;
        ImGui::SetCursorScreenPos({origin.x + 98.f - 6.f, origin.y + 22.f});
        ImGui::TextDisabled("MOVE");
        ImGui::SetCursorScreenPos({origin.x + 98.f - 6.f, origin.y + 52.f});
        ImGui::TextDisabled("SKILL");
        for (int gem = 0; gem < points.moveMax; ++gem) {
            ui::DrawGem(draw, {gemX + 34.f + gem * 17.f, origin.y + 32.f}, 6.f, pal::kMove, gem < points.move);
        }
        for (int gem = 0; gem < points.skillMax; ++gem) {
            ui::DrawGem(draw, {gemX + 34.f + gem * 17.f, origin.y + 62.f}, 6.f, pal::kSkill, gem < points.skill);
        }

        const float slotsX = origin.x + 238.f;
        for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
            const ImVec2 min{slotsX + index * 76.f, origin.y + 14.f};
            const ImVec2 max{min.x + 52.f, min.y + 52.f};
            const auto resolved = ResolveSkillSlot(slots[index], content);
            const auto* row = content.skills.find(slots[index].skillId);
            const bool affordable = resolved && points.skill >= resolved->apCost;
            ImGui::PushID(index);
            ImGui::SetCursorScreenPos(min);
            if (ImGui::InvisibleButton("slot", {52.f, 52.f})) selectedSkill = index;
            const bool hot = ImGui::IsItemHovered();
            ui::DrawSlotFrame(draw, min, max, selectedSkill == index ? pal::kGoldBright : hot ? pal::kGold : pal::kBronze,
                              selectedSkill == index);
            if (resolved) {
                DrawDungeonImage(registry, SkillIcon(*resolved), draw, {min.x + 6.f, min.y + 6.f}, {max.x - 6.f, max.y - 6.f},
                                 {0, 0}, {1, 1}, affordable ? IM_COL32_WHITE : IM_COL32(90, 90, 90, 255));
                ui::DrawGem(draw, {max.x - 6.f, max.y - 6.f}, 5.f, pal::kSkill, affordable);
                char cost[8];
                std::snprintf(cost, sizeof cost, "%d", resolved->apCost);
                draw->AddText({max.x - 10.f, max.y - 13.f}, IM_COL32(255, 255, 255, 255), cost);
            }
            char key[4];
            std::snprintf(key, sizeof key, "%d", index + 1);
            draw->AddText({min.x + 5.f, min.y + 2.f}, pal::kGoldBright, key);
            const std::string label = row ? row->displayName : slots[index].skillId;
            const ImVec2 labelSize = ImGui::GetFont()->CalcTextSizeA(14.f, 1000.f, 0.f, label.c_str());
            const ImVec4 clip{min.x - 10.f, min.y + 54.f, max.x + 14.f, min.y + 74.f};
            draw->AddText(ImGui::GetFont(), 14.f, {std::max(clip.x, (min.x + max.x - labelSize.x) * .5f), min.y + 56.f},
                          selectedSkill == index ? pal::kGoldBright : pal::kMuted, label.c_str(), nullptr, 0.f, &clip);
            if (hot && resolved) {
                ImGui::SetTooltip("AP %d | range %d | power %d | %s\nRunes: %zu | statuses: %zu",
                    resolved->apCost, resolved->range, resolved->power, resolved->shape.c_str(),
                    slots[index].runeIds.size(), resolved->statuses.size());
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos({origin.x + barWidth - 22.f - 150.f, origin.y + 22.f});
        ImGui::BeginDisabled(!queue.pending.empty());
        if (ui::Button("END TURN  [Space]", {150.f, 50.f})) runtime.requests.push_back(FEndTurn{*snapshot.currentTurn});
        ImGui::EndDisabled();
    } else {
        ImGui::SetCursorScreenPos({origin.x + 28.f, origin.y + 26.f});
        ui::HeadingText(outcome->resolved ? "ROOM RESOLVED" : "ENEMY TURN", 24.f,
                        outcome->resolved ? pal::kGoldV : pal::kEnemyV);
        ImGui::TextDisabled(outcome->resolved ? "The party catches its breath." : "The enemy is making its move.");
    }
    ui::EndPanel();
}

} // namespace game::ecs
