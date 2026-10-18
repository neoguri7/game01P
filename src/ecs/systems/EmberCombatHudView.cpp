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
#include "presentation/FIsoView.h"
#include "presentation/FUiTheme.h"

namespace game::ecs {
namespace {

namespace pal = ui::palette;

constexpr float kMargin = 16.f;
constexpr float kBarHeight = kIsoBottomReserve - 20.f;

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

int UnspentNodePoints(const FDungeonRun& run) {
    int points = 0;
    for (const FRunMember& member : run.party) points += member.passivePoints;
    return points;
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
    DrawDungeonImage(registry, SpriteOf(registry, entity), draw, {min.x + 6, min.y + 6}, {max.x - 6, max.y - 6},
                     {.18f, .0f}, {.82f, .64f}, dim ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE);
}

ImU32 FactionTrim(const entt::registry& registry, entt::entity entity, bool current) {
    if (current) return pal::kGoldBright;
    return registry.all_of<FPartyMember>(entity) ? pal::kBronze : pal::kEnemy;
}

void Label(ImDrawList* draw, ImVec2 at, float size, ImU32 color, const char* text) {
    draw->AddText(ImGui::GetFont(), size, at, color, text);
}

float LabelWidth(float size, const char* text) { return ImGui::GetFont()->CalcTextSizeA(size, 1000.f, 0.f, text).x; }

void TopBar(entt::registry& registry, float width, const FDungeonRun& run, const FTurnSnapshot& snapshot) {
    if (!ui::BeginPanel("Expedition", {kMargin, 8.f}, {width - kMargin * 2.f, 68.f})) {
        ui::EndPanel();
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    ImGui::SetCursorScreenPos({origin.x + 24.f, origin.y + 9.f});
    ui::HeadingText("EMBER DUNGEON", 22.f);
    ImGui::SetCursorScreenPos({origin.x + 24.f, origin.y + 35.f});
    ImGui::TextDisabled("%s  /  seed %u", run.roomName.c_str(), run.seed);

    float x = origin.x + 330.f;
    const float y = origin.y + 13.f;
    char round[32];
    std::snprintf(round, sizeof round, "ROUND %llu", static_cast<unsigned long long>(snapshot.roundNumber));
    ImGui::SetCursorScreenPos({x, y + 11.f});
    ImGui::TextDisabled("%s", round);
    x += 86.f;
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
    std::snprintf(depth, sizeof depth, "DEPTH %02d  |  ROOM %d / 3", run.depth, run.room + 1);
    char stats[96];
    std::snprintf(stats, sizeof stats, "MATERIALS %d  |  CHECKPOINT %d  |  UNSPENT NODES %d",
                  run.materials, run.cleared, UnspentNodePoints(run));
    const float right = origin.x + size.x - 26.f;
    ImFont* heading = ui::HeadingFont();
    if (heading) ImGui::PushFont(heading, 18.f);
    const ImVec2 depthSize = ImGui::CalcTextSize(depth);
    ImGui::SetCursorScreenPos({right - depthSize.x, origin.y + 9.f});
    ImGui::TextColored(pal::kGoldV, "%s", depth);
    if (heading) ImGui::PopFont();
    ImGui::SetCursorScreenPos({right - ImGui::CalcTextSize(stats).x, origin.y + 35.f});
    ImGui::TextDisabled("%s", stats);
    ui::EndPanel();
}

// Tooltip-style card for whoever holds the turn, like the unit card of a classic isometric RPG.
void UnitCard(entt::registry& registry, const FTurnSnapshot& snapshot, const FBattleOutcome& outcome, bool playerTurn,
              int selectedSkill, FActionQueue& queue, const gameplay::FContentRegistry& content) {
    if (!snapshot.currentTurn || outcome.resolved) return;
    const auto actor = snapshot.currentTurn->actor;
    if (!registry.all_of<FHealth>(actor)) return;
    if (!ui::BeginPanel("Unit", {kMargin, kIsoTopReserve + 12.f}, {300.f, playerTurn ? 236.f : 118.f})) {
        ui::EndPanel();
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float inner = 300.f - 48.f;
    const bool party = registry.all_of<FPartyMember>(actor);
    const auto full = UnitDisplayName(registry, actor);
    ImGui::SetCursorScreenPos({origin.x + 24.f, origin.y + 14.f});
    ui::HeadingText(FirstName(full).c_str(), 20.f, party ? pal::kGoldV : pal::kEnemyV);
    ImGui::SetCursorScreenPos({origin.x + 24.f, origin.y + 38.f});
    ImGui::TextDisabled("%s", RoleName(full).c_str());

    const auto& hp = registry.get<FHealth>(actor);
    const ImVec2 barMin{origin.x + 24.f, origin.y + 64.f};
    ui::DrawLifeBar(draw, barMin, {barMin.x + inner, barMin.y + 16.f}, static_cast<float>(hp.current) / std::max(1, hp.max));
    char numbers[32];
    std::snprintf(numbers, sizeof numbers, "%d / %d", hp.current, hp.max);
    Label(draw, {barMin.x + (inner - LabelWidth(15.f, numbers)) * .5f, barMin.y}, 15.f, IM_COL32(245, 232, 205, 255), numbers);

    if (!playerTurn) {
        ImGui::SetCursorScreenPos({origin.x + 24.f, origin.y + 90.f});
        ImGui::TextColored(pal::kEnemyV, "Enemy is planning...");
        ui::EndPanel();
        return;
    }

    const auto& points = registry.get<FActionPoints>(actor);
    Label(draw, {origin.x + 24.f, origin.y + 92.f}, 15.f, pal::kMuted, "MOVE");
    Label(draw, {origin.x + 24.f, origin.y + 114.f}, 15.f, pal::kMuted, "SKILL");
    for (int gem = 0; gem < points.moveMax; ++gem) {
        ui::DrawGem(draw, {origin.x + 86.f + gem * 17.f, origin.y + 101.f}, 6.f, pal::kMove, gem < points.move);
    }
    for (int gem = 0; gem < points.skillMax; ++gem) {
        ui::DrawGem(draw, {origin.x + 86.f + gem * 17.f, origin.y + 123.f}, 6.f, pal::kSkill, gem < points.skill);
    }

    const auto& slots = registry.get<FSkillLoadout>(actor).slots;
    if (selectedSkill >= 0 && selectedSkill < static_cast<int>(slots.size())) {
        const auto skill = ResolveSkillSlot(slots[selectedSkill], content);
        const auto* row = content.skills.find(slots[selectedSkill].skillId);
        if (skill && row) {
            ImGui::SetCursorScreenPos({origin.x + 24.f, origin.y + 144.f});
            ImGui::TextColored(pal::kGoldV, "%s", row->displayName.c_str());
            ImGui::SetCursorScreenPos({origin.x + 24.f, origin.y + 166.f});
            ImGui::TextDisabled("Range %d  |  AP %d  |  %s", skill->range, skill->apCost, skill->shape.c_str());
            ImGui::SetCursorScreenPos({origin.x + 24.f, origin.y + 192.f});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8.f, 2.f});
            for (const auto target : registry.view<FHealth, FGridPosition>()) {
                if (!CanTargetWithSkill(registry, actor, target, *skill)) continue;
                ImGui::PushID(static_cast<int>(entt::to_integral(target)));
                ImGui::BeginDisabled(points.skill < skill->apCost || !queue.pending.empty());
                if (ui::Button(FirstName(UnitDisplayName(registry, target)).c_str(), {0.f, 26.f})) {
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
    ui::EndPanel();
}

void Journal(const FBattleOutcome& outcome, const entt::registry& registry, float width, float height) {
    const float journalWidth = 330.f;
    const float journalHeight = 150.f;
    if (!ui::BeginPanel("Journal", {width - journalWidth - kMargin, height - kIsoBottomReserve - journalHeight + 6.f},
                        {journalWidth, journalHeight})) {
        ui::EndPanel();
        return;
    }
    ui::SectionTitle(outcome.resolved ? "ROOM RESOLVED" : "BATTLE JOURNAL");
    ImGui::BeginChild("journal", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (const auto* log = registry.ctx().find<FCombatLog>()) {
        const auto start = log->lines.size() > 5 ? log->lines.size() - 5 : 0;
        for (auto index = start; index < log->lines.size(); ++index) {
            ImGui::PushStyleColor(ImGuiCol_Text, index + 1 == log->lines.size() ? pal::kTextV : pal::kMutedV);
            ImGui::TextWrapped("%s", log->lines[index].c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ui::EndPanel();
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

    UnitCard(registry, snapshot, *outcome, playerTurn, selectedSkill, queue, content);
    Journal(*outcome, registry, width, height);

    // Bottom action bar: party portraits, skill slots, end turn.
    ui::BeginPanel("Skills", {kMargin, height - kBarHeight - 8.f}, {width - kMargin * 2.f, kBarHeight});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float barWidth = width - kMargin * 2.f;
    const float partyTop = origin.y + 22.f;
    float partyX = origin.x + 28.f;
    for (const auto entity : registry.view<FPartyMember, FHealth>()) {
        const auto& hp = registry.get<FHealth>(entity);
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entity;
        const bool alive = IsAlive(registry, entity);
        const ImVec2 min{partyX, partyTop};
        DrawPortrait(registry, draw, entity, min, 62.f, current ? pal::kGoldBright : pal::kBronze, current, !alive);
        ui::DrawLifeBar(draw, {min.x + 2.f, min.y + 68.f}, {min.x + 60.f, min.y + 78.f},
                        static_cast<float>(hp.current) / std::max(1, hp.max));
        const auto name = FirstName(UnitDisplayName(registry, entity));
        Label(draw, {min.x + (62.f - LabelWidth(14.f, name.c_str())) * .5f, min.y + 80.f}, 14.f,
              current ? pal::kGoldBright : pal::kMuted, name.c_str());
        if (ImGui::IsMouseHoveringRect(min, {min.x + 62.f, min.y + 62.f})) {
            ImGui::SetTooltip("%s\nHP %d / %d", UnitDisplayName(registry, entity).c_str(), hp.current, hp.max);
        }
        partyX += 80.f;
    }

    const float slotsX = origin.x + 28.f + 4 * 80.f + 56.f;
    if (playerTurn) {
        const auto actor = snapshot.currentTurn->actor;
        const auto& points = registry.get<FActionPoints>(actor);
        const auto& slots = registry.get<FSkillLoadout>(actor).slots;
        for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
            const ImVec2 min{slotsX + index * 90.f, origin.y + 24.f};
            const ImVec2 max{min.x + 64.f, min.y + 64.f};
            const auto resolved = ResolveSkillSlot(slots[index], content);
            const auto* row = content.skills.find(slots[index].skillId);
            const bool affordable = resolved && points.skill >= resolved->apCost;
            ImGui::PushID(index);
            ImGui::SetCursorScreenPos(min);
            if (ImGui::InvisibleButton("slot", {64.f, 64.f})) selectedSkill = index;
            const bool hot = ImGui::IsItemHovered();
            ui::DrawSlotFrame(draw, min, max, selectedSkill == index ? pal::kGoldBright : hot ? pal::kGold : pal::kBronze,
                              selectedSkill == index);
            if (resolved) {
                DrawDungeonImage(registry, SkillIcon(*resolved), draw, {min.x + 8.f, min.y + 8.f}, {max.x - 8.f, max.y - 8.f},
                                 {0, 0}, {1, 1}, affordable ? IM_COL32_WHITE : IM_COL32(90, 90, 90, 255));
                ui::DrawGem(draw, {max.x - 9.f, max.y - 9.f}, 5.f, pal::kSkill, affordable);
                char cost[8];
                std::snprintf(cost, sizeof cost, "%d", resolved->apCost);
                draw->AddText({max.x - 13.f, max.y - 16.f}, IM_COL32(255, 255, 255, 255), cost);
            }
            char key[4];
            std::snprintf(key, sizeof key, "%d", index + 1);
            draw->AddText({min.x + 9.f, min.y + 5.f}, pal::kGoldBright, key);
            const std::string label = row ? row->displayName : slots[index].skillId;
            const ImVec2 labelSize = ImGui::GetFont()->CalcTextSizeA(14.f, 1000.f, 0.f, label.c_str());
            const ImVec4 clip{min.x - 12.f, min.y + 66.f, max.x + 12.f, min.y + 86.f};
            draw->AddText(ImGui::GetFont(), 14.f, {std::max(clip.x, (min.x + max.x - labelSize.x) * .5f), min.y + 68.f},
                          selectedSkill == index ? pal::kGoldBright : pal::kMuted, label.c_str(), nullptr, 0.f, &clip);
            if (hot && resolved) {
                ImGui::SetTooltip("AP %d | range %d | power %d | %s\nRunes: %zu | statuses: %zu",
                    resolved->apCost, resolved->range, resolved->power, resolved->shape.c_str(),
                    slots[index].runeIds.size(), resolved->statuses.size());
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos({origin.x + barWidth - 28.f - 176.f, origin.y + 34.f});
        ImGui::BeginDisabled(!queue.pending.empty());
        if (ui::Button("END TURN  [Space]", {176.f, 56.f})) runtime.requests.push_back(FEndTurn{*snapshot.currentTurn});
        ImGui::EndDisabled();
    } else {
        ImGui::SetCursorScreenPos({slotsX, origin.y + 34.f});
        ui::HeadingText(outcome->resolved ? "ROOM RESOLVED" : "ENEMY TURN", 24.f,
                        outcome->resolved ? pal::kGoldV : pal::kEnemyV);
        ImGui::SetCursorScreenPos({slotsX, origin.y + 66.f});
        ImGui::TextDisabled(outcome->resolved ? "The party catches its breath." : "The enemy is making its move.");
    }
    const float hintX = origin.x + barWidth - 28.f - 176.f - 270.f;
    Label(draw, {hintX, origin.y + 28.f}, 15.f, pal::kMuted, "WASD move  |  1-4 skill");
    Label(draw, {hintX, origin.y + 50.f}, 15.f, pal::kMuted, "Click target  |  Space end turn");
    Label(draw, {hintX, origin.y + 76.f}, 15.f, IM_COL32(90, 225, 255, 200), "Cyan = reachable");
    Label(draw, {hintX, origin.y + 96.f}, 15.f, IM_COL32(255, 110, 90, 210), "Red = affected  /  spikes hurt");
    ui::EndPanel();
}

} // namespace game::ecs
