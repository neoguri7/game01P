#include "ecs/systems/EmberCombatHudSystem.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <imgui.h>
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
#include "presentation/FUiFrame.h"
#include "presentation/FUiTheme.h"

namespace game::ecs {
namespace {

namespace pal = ui::palette;
using ui::FRect;
using ui::FUiLayout;

constexpr float kBodySize = 17.f;

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

ImVec2 Min(const FRect& rect) { return {rect.x, rect.y}; }
ImVec2 Max(const FRect& rect) { return {rect.right(), rect.bottom()}; }
ImVec2 Size(const FRect& rect) { return {rect.w, rect.h}; }

// Head-and-shoulders crop of a full-body sprite inside a framed square.
void DrawPortrait(entt::registry& registry, ImDrawList* draw, entt::entity entity, ImVec2 min, float size, float inset,
                  ImU32 trim, bool glow, bool dim) {
    const ImVec2 max{min.x + size, min.y + size};
    ui::DrawSlotFrame(draw, min, max, trim, glow);
    DrawDungeonImage(registry, SpriteOf(registry, entity), draw, {min.x + inset, min.y + inset},
                     {max.x - inset, max.y - inset}, {.18f, .0f}, {.82f, .64f},
                     dim ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE);
}

ImU32 FactionTrim(const entt::registry& registry, entt::entity entity, bool current) {
    if (current) return pal::kGoldBright;
    return registry.all_of<FPartyMember>(entity) ? pal::kBronze : pal::kEnemy;
}

void Label(ImDrawList* draw, ImVec2 at, float size, ImU32 color, const char* text) {
    draw->AddText(ImGui::GetFont(), size, at, color, text);
}

float LabelWidth(float size, const char* text) { return ImGui::GetFont()->CalcTextSizeA(size, 1000.f, 0.f, text).x; }

// Round counter, initiative portraits and run status across the top band.
void TopBar(entt::registry& registry, const FUiLayout& layout, const FDungeonRun& run, const FTurnSnapshot& snapshot) {
    if (!ui::BeginZone("Expedition", Min(layout.topBar), Size(layout.topBar))) {
        ui::EndZone();
        return;
    }
    const float u = layout.scale;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto& zones = layout.zones;

    ImGui::SetCursorScreenPos({zones.title.x, zones.title.y});
    ui::HeadingText("EMBER DUNGEON", 22.f * u);
    {
        ui::FBodyFont body(15.f * u);
        ImGui::SetCursorScreenPos({zones.title.x, zones.title.y + 26.f * u});
        ImGui::TextDisabled("%s  /  seed %u", run.roomName.c_str(), run.seed);
    }

    const float portrait = std::min(40.f * u, zones.turnOrder.h);
    float x = zones.turnOrder.x;
    const float y = zones.turnOrder.y + (zones.turnOrder.h - portrait) * .5f;
    char round[32];
    std::snprintf(round, sizeof round, "ROUND %llu", static_cast<unsigned long long>(snapshot.roundNumber));
    Label(draw, {x, y + (portrait - 15.f * u) * .5f}, 15.f * u, pal::kMuted, round);
    x += 78.f * u;
    for (const auto& entry : snapshot.entries) {
        if (entry.state == ETurnEntryState::Skipped || entry.state == ETurnEntryState::Completed) continue;
        if (x + portrait > zones.turnOrder.right()) break;
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entry.entity;
        const ImVec2 min{x, y};
        DrawPortrait(registry, draw, entry.entity, min, portrait, 6.f * u, FactionTrim(registry, entry.entity, current),
                     current, !IsAlive(registry, entry.entity));
        if (ImGui::IsMouseHoveringRect(min, {min.x + portrait, min.y + portrait})) {
            ImGui::SetTooltip("%s", UnitDisplayName(registry, entry.entity).c_str());
        }
        x += portrait + 6.f * u;
    }

    char depth[96];
    std::snprintf(depth, sizeof depth, "DEPTH %02d  |  ROOM %d / 3", run.depth, run.room + 1);
    char stats[96];
    std::snprintf(stats, sizeof stats, "MATERIALS %d  |  CHECKPOINT %d  |  NODES %d", run.materials, run.cleared,
                  UnspentNodePoints(run));
    const float right = zones.status.right();
    {
        ImFont* heading = ui::HeadingFont();
        if (heading) ImGui::PushFont(heading, 18.f * u);
        ImGui::SetCursorScreenPos({right - ImGui::CalcTextSize(depth).x, zones.status.y});
        ImGui::TextColored(pal::kGoldV, "%s", depth);
        if (heading) ImGui::PopFont();
    }
    {
        // The font scope must close before the window does; ImGui asserts on an unbalanced font stack in End().
        ui::FBodyFont body(14.f * u);
        ImGui::SetCursorScreenPos({right - ImGui::CalcTextSize(stats).x, zones.status.y + 26.f * u});
        ImGui::TextDisabled("%s", stats);
    }
    ui::EndZone();
}

// Floating card over the playfield: the selected skill and the legal targets, as clickable names.
void TargetCard(entt::registry& registry, const FUiLayout& layout, const FTurnSnapshot& snapshot,
                const FBattleOutcome& outcome, bool playerTurn, int selectedSkill, FActionQueue& queue,
                const gameplay::FContentRegistry& content) {
    if (!playerTurn || outcome.resolved || !snapshot.currentTurn) return;
    const auto actor = snapshot.currentTurn->actor;
    const auto& slots = registry.get<FSkillLoadout>(actor).slots;
    if (selectedSkill < 0 || selectedSkill >= static_cast<int>(slots.size())) return;
    const auto skill = ResolveSkillSlot(slots[selectedSkill], content);
    const auto* row = content.skills.find(slots[selectedSkill].skillId);
    if (!skill || !row) return;

    const float u = layout.scale;
    const FRect& card = layout.zones.targets;
    if (!ui::BeginPanel("Targets", Min(card), Size(card))) {
        ui::EndPanel();
        return;
    }
    {
        // The font scope must close before the window does; ImGui asserts on an unbalanced font stack in End().
        ui::FBodyFont body(kBodySize * u);
        const ImVec2 origin = ImGui::GetWindowPos();
        const auto& points = registry.get<FActionPoints>(actor);
        ImGui::SetCursorScreenPos({origin.x + 22.f * u, origin.y + 14.f * u});
        ImGui::TextColored(pal::kGoldV, "%s", row->displayName.c_str());
        ImGui::SetCursorScreenPos({origin.x + 22.f * u, origin.y + 38.f * u});
        ImGui::TextDisabled("Range %d  |  AP %d  |  %s", skill->range, skill->apCost, skill->shape.c_str());
        ImGui::SetCursorScreenPos({origin.x + 22.f * u, origin.y + 66.f * u});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8.f * u, 2.f * u});
        const float available = card.w - 44.f * u;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        float used = 0.f;
        for (const auto target : registry.view<FHealth, FGridPosition>()) {
            if (!CanTargetWithSkill(registry, actor, target, *skill)) continue;
            const auto name = FirstName(UnitDisplayName(registry, target));
            const float width = ImGui::CalcTextSize(name.c_str()).x + 16.f * u;
            if (used > 0.f && used + spacing + width <= available) {
                ImGui::SameLine();
                used += spacing + width;
            } else {
                used = width;
            }
            ImGui::PushID(static_cast<int>(entt::to_integral(target)));
            ImGui::BeginDisabled(points.skill < skill->apCost || !queue.pending.empty());
            if (ui::Button(name.c_str(), {0.f, 26.f * u})) {
                queue.pending.push_back(FActionRequest{actor, target, slots[selectedSkill].skillId, *snapshot.currentTurn,
                                                       registry.get<FGridPosition>(target)});
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        // Without a target in range no item follows the cursor move above; ImGui asserts unless something validates it.
        ImGui::NewLine();
    }
    ui::EndPanel();
}

// The acting unit: name, life and action points. Sits in the middle of the dock, above the skill slots.
void UnitReadout(entt::registry& registry, const FUiLayout& layout, const FTurnSnapshot& snapshot,
                 const FBattleOutcome& outcome, bool playerTurn) {
    if (!snapshot.currentTurn || outcome.resolved) return;
    const auto actor = snapshot.currentTurn->actor;
    if (!registry.all_of<FHealth>(actor)) return;
    const float u = layout.scale;
    const FRect& zone = layout.zones.unit;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool party = registry.all_of<FPartyMember>(actor);
    const auto full = UnitDisplayName(registry, actor);
    const auto first = FirstName(full);

    ImGui::SetCursorScreenPos({zone.x, zone.y});
    ui::HeadingText(first.c_str(), 20.f * u, party ? pal::kGoldV : pal::kEnemyV);
    if (ImFont* heading = ui::HeadingFont()) {
        const float nameWidth = heading->CalcTextSizeA(20.f * u, 1000.f, 0.f, first.c_str()).x;
        Label(draw, {zone.x + nameWidth + 12.f * u, zone.y + 6.f * u}, 15.f * u, pal::kMuted, RoleName(full).c_str());
    } else {
        Label(draw, {zone.x + 90.f * u, zone.y + 6.f * u}, 15.f * u, pal::kMuted, RoleName(full).c_str());
    }

    const auto& hp = registry.get<FHealth>(actor);
    const ImVec2 barMin{zone.x, zone.y + 30.f * u};
    const ImVec2 barMax{zone.right(), barMin.y + 16.f * u};
    ui::DrawLifeBar(draw, barMin, barMax, static_cast<float>(hp.current) / std::max(1, hp.max));
    char numbers[32];
    std::snprintf(numbers, sizeof numbers, "%d / %d", hp.current, hp.max);
    Label(draw, {barMin.x + (zone.w - LabelWidth(15.f * u, numbers)) * .5f, barMin.y}, 15.f * u,
          IM_COL32(245, 232, 205, 255), numbers);

    const float rowY = zone.y + 54.f * u;
    if (!playerTurn) {
        Label(draw, {zone.x, rowY}, 16.f * u, pal::kEnemy, "Enemy is planning...");
        return;
    }
    const auto& points = registry.get<FActionPoints>(actor);
    const float gem = 6.f * u;
    Label(draw, {zone.x, rowY}, 14.f * u, pal::kMuted, "MOVE");
    for (int index = 0; index < points.moveMax; ++index) {
        ui::DrawGem(draw, {zone.x + 50.f * u + static_cast<float>(index) * 17.f * u, rowY + 9.f * u}, gem, pal::kMove,
                    index < points.move);
    }
    const float skillX = zone.x + 168.f * u;
    Label(draw, {skillX, rowY}, 14.f * u, pal::kMuted, "SKILL");
    for (int index = 0; index < points.skillMax; ++index) {
        ui::DrawGem(draw, {skillX + 52.f * u + static_cast<float>(index) * 17.f * u, rowY + 9.f * u}, gem, pal::kSkill,
                    index < points.skill);
    }
}

void PartyPortraits(entt::registry& registry, const FUiLayout& layout, const FTurnSnapshot& snapshot) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.party;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float well = 80.f * u;
    const float top = zone.y + 4.f * u;
    float x = zone.x;
    for (const auto entity : registry.view<FPartyMember, FHealth>()) {
        const auto& hp = registry.get<FHealth>(entity);
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entity;
        const bool alive = IsAlive(registry, entity);
        const ImVec2 min{x, top};
        DrawPortrait(registry, draw, entity, min, well, 7.f * u, current ? pal::kGoldBright : pal::kBronze, current, !alive);
        ui::DrawLifeBar(draw, {min.x + 2.f * u, min.y + 86.f * u}, {min.x + well - 2.f * u, min.y + 98.f * u},
                        static_cast<float>(hp.current) / std::max(1, hp.max));
        const auto name = FirstName(UnitDisplayName(registry, entity));
        Label(draw, {min.x + (well - LabelWidth(14.f * u, name.c_str())) * .5f, min.y + 102.f * u}, 14.f * u,
              current ? pal::kGoldBright : pal::kMuted, name.c_str());
        if (ImGui::IsMouseHoveringRect(min, {min.x + well, min.y + well})) {
            ImGui::SetTooltip("%s\nHP %d / %d", UnitDisplayName(registry, entity).c_str(), hp.current, hp.max);
        }
        x += 92.f * u;
    }
}

void SkillSlots(entt::registry& registry, const FUiLayout& layout, const FTurnSnapshot& snapshot,
                const FBattleOutcome& outcome, bool playerTurn, int& selectedSkill,
                const gameplay::FContentRegistry& content) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.skills;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!playerTurn) {
        ImGui::SetCursorScreenPos({zone.x, zone.y + 10.f * u});
        ui::HeadingText(outcome.resolved ? "ROOM RESOLVED" : "ENEMY TURN", 24.f * u,
                        outcome.resolved ? pal::kGoldV : pal::kEnemyV);
        ui::FBodyFont body(kBodySize * u);
        ImGui::SetCursorScreenPos({zone.x, zone.y + 44.f * u});
        ImGui::TextDisabled(outcome.resolved ? "The party catches its breath." : "The enemy is making its move.");
        return;
    }

    const auto actor = snapshot.currentTurn->actor;
    const auto& points = registry.get<FActionPoints>(actor);
    const auto& slots = registry.get<FSkillLoadout>(actor).slots;
    const float slot = 60.f * u;
    const float step = slots.size() > 1 ? (zone.w - slot) / 3.f : 0.f;
    for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
        const ImVec2 min{zone.x + static_cast<float>(index) * step, zone.y};
        const ImVec2 max{min.x + slot, min.y + slot};
        const auto resolved = ResolveSkillSlot(slots[index], content);
        const auto* row = content.skills.find(slots[index].skillId);
        const bool affordable = resolved && points.skill >= resolved->apCost;
        ImGui::PushID(index);
        ImGui::SetCursorScreenPos(min);
        if (ImGui::InvisibleButton("slot", {slot, slot})) selectedSkill = index;
        const bool hot = ImGui::IsItemHovered();
        ui::DrawSlotFrame(draw, min, max, selectedSkill == index ? pal::kGoldBright : hot ? pal::kGold : pal::kBronze,
                          selectedSkill == index);
        if (resolved) {
            DrawDungeonImage(registry, SkillIcon(*resolved), draw, {min.x + 8.f * u, min.y + 8.f * u},
                             {max.x - 8.f * u, max.y - 8.f * u}, {0, 0}, {1, 1},
                             affordable ? IM_COL32_WHITE : IM_COL32(90, 90, 90, 255));
            ui::DrawGem(draw, {max.x - 9.f * u, max.y - 9.f * u}, 5.f * u, pal::kSkill, affordable);
            char cost[8];
            std::snprintf(cost, sizeof cost, "%d", resolved->apCost);
            Label(draw, {max.x - 13.f * u, max.y - 17.f * u}, 15.f * u, IM_COL32(255, 255, 255, 255), cost);
        }
        char key[4];
        std::snprintf(key, sizeof key, "%d", index + 1);
        Label(draw, {min.x + 8.f * u, min.y + 4.f * u}, 15.f * u, pal::kGoldBright, key);
        const std::string name = row ? row->displayName : slots[index].skillId;
        const float labelWidth = LabelWidth(14.f * u, name.c_str());
        const ImVec4 clip{min.x - 12.f * u, min.y + slot + 2.f * u, max.x + 12.f * u, min.y + slot + 22.f * u};
        draw->AddText(ImGui::GetFont(), 14.f * u, {std::max(clip.x, (min.x + max.x - labelWidth) * .5f), min.y + slot + 4.f * u},
                      selectedSkill == index ? pal::kGoldBright : pal::kMuted, name.c_str(), nullptr, 0.f, &clip);
        if (hot && resolved) {
            ImGui::SetTooltip("AP %d | range %d | power %d | %s\nRunes: %zu | statuses: %zu",
                resolved->apCost, resolved->range, resolved->power, resolved->shape.c_str(),
                slots[index].runeIds.size(), resolved->statuses.size());
        }
        ImGui::PopID();
    }
}

void Journal(entt::registry& registry, const FUiLayout& layout, const FBattleOutcome& outcome) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.journal;
    ui::DrawRecess(ImGui::GetWindowDrawList(), Min(zone), Max(zone));
    ImGui::SetCursorScreenPos({zone.x + 12.f * u, zone.y + 4.f * u});
    ImGui::BeginChild("journal", {zone.w - 24.f * u, zone.h - 8.f * u}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ui::SectionTitle(outcome.resolved ? "ROOM RESOLVED" : "BATTLE JOURNAL", 15.f * u);
    {
        ui::FBodyFont body(15.f * u);
        if (const auto* log = registry.ctx().find<FCombatLog>()) {
            const auto start = log->lines.size() > 6 ? log->lines.size() - 6 : 0;
            for (auto index = start; index < log->lines.size(); ++index) {
                ImGui::PushStyleColor(ImGuiCol_Text, index + 1 == log->lines.size() ? pal::kTextV : pal::kMutedV);
                ImGui::TextWrapped("%s", log->lines[index].c_str());
                ImGui::PopStyleColor();
            }
        }
        // Wrapped lines can outgrow the field; keep the newest entry in view.
        ImGui::SetScrollHereY(1.f);
    }
    ImGui::EndChild();
}

void Hints(const FUiLayout& layout) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.hints;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float size = 14.f * u;
    const float line = 17.f * u;
    Label(draw, {zone.x, zone.y}, size, pal::kMuted, "WASD move  |  1-4 skill");
    Label(draw, {zone.x, zone.y + line}, size, pal::kMuted, "Space: end turn");
    float x = zone.x;
    const float y = zone.y + line * 2.f;
    const std::pair<const char*, ImU32> parts[]{{"Cyan", IM_COL32(90, 225, 255, 220)}, {" reach  ", pal::kMuted},
                                                {"Red", IM_COL32(255, 110, 90, 230)}, {" hit", pal::kMuted}};
    for (const auto& [text, color] : parts) {
        Label(draw, {x, y}, size, color, text);
        x += LabelWidth(size, text);
    }
}

} // namespace

void EmberCombatHudSystem::render(entt::registry& registry) {
    auto* outcome = registry.ctx().find<FBattleOutcome>();
    const ui::FUiLayout* layout = ui::FindUiLayout(registry);
    if (!outcome || !layout) return;
    const auto& run = registry.ctx().get<FDungeonRun>();
    auto& runtime = registry.ctx().get<FTurnRuntime>();
    auto& queue = registry.ctx().get<FActionQueue>();
    const auto& content = registry.ctx().get<gameplay::FContentRegistry>();
    const auto snapshot = runtime.controller.snapshot();

    ui::DrawUiFrame(registry);
    TopBar(registry, *layout, run, snapshot);

    const bool playerTurn = !outcome->resolved && snapshot.currentTurn && IsAlive(registry, snapshot.currentTurn->actor) &&
        registry.all_of<FPartyMember, FActionPoints, FSkillLoadout>(snapshot.currentTurn->actor);
    TargetCard(registry, *layout, snapshot, *outcome, playerTurn, selectedSkill, queue, content);

    // The dock: party portraits, the acting unit with its skill slots, and the journal beside the end-turn button.
    if (!ui::BeginZone("Dock", Min(layout->dock), Size(layout->dock))) {
        ui::EndZone();
        return;
    }
    PartyPortraits(registry, *layout, snapshot);
    UnitReadout(registry, *layout, snapshot, *outcome, playerTurn);
    SkillSlots(registry, *layout, snapshot, *outcome, playerTurn, selectedSkill, content);
    Journal(registry, *layout, *outcome);
    Hints(*layout);
    if (playerTurn) {
        const ui::FRect& endTurn = layout->zones.endTurn;
        ImGui::SetCursorScreenPos(Min(endTurn));
        ImGui::BeginDisabled(!queue.pending.empty());
        if (ui::Button("END TURN  [Space]", Size(endTurn))) runtime.requests.push_back(FEndTurn{*snapshot.currentTurn});
        ImGui::EndDisabled();
    }
    ui::EndZone();
}

} // namespace game::ecs
