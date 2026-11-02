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

constexpr float kBodySize = 15.f;
// hud_panel's nine-slice corner, in the picture's own pixels (assets/art/asset_contract.json).
constexpr float kPanelCorner = 24.f;
constexpr ImVec4 kClear{0.f, 0.f, 0.f, 0.f};

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

// The drawn icon of a skill without its own picture.
EDungeonSprite FallbackSkillIcon(const FResolvedSkill& skill) {
    if (skill.damageType == gameplay::kDamageTypeMagical) return EDungeonSprite::IconSpell;
    return skill.range > 2 ? EDungeonSprite::IconBow : EDungeonSprite::IconSword;
}

ImVec2 Min(const FRect& rect) { return {rect.x, rect.y}; }
ImVec2 Max(const FRect& rect) { return {rect.right(), rect.bottom()}; }
ImVec2 Size(const FRect& rect) { return {rect.w, rect.h}; }

void DrawHudPanel(entt::registry& registry, ImDrawList* draw, const FRect& rect) {
    ui::DrawNineSlice(draw, DungeonSkinPiece(registry, EDungeonSprite::HudPanel), Min(rect), Max(rect), kPanelCorner);
}

// Head-and-shoulders crop of a full-body sprite.
void DrawPortraitCrop(entt::registry& registry, ImDrawList* draw, entt::entity entity, ImVec2 min, ImVec2 max, bool dim) {
    DrawDungeonImage(registry, SpriteOf(registry, entity), draw, min, max, {.18f, .0f}, {.82f, .64f},
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

// The three small clusters along the top edge: room name, initiative order and run status.
void TopBar(entt::registry& registry, const FUiLayout& layout, const FDungeonRun& run, const FTurnSnapshot& snapshot) {
    if (!ui::BeginZone("Expedition", Min(layout.topBar), Size(layout.topBar))) {
        ui::EndZone();
        return;
    }
    const float u = layout.scale;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto& zones = layout.zones;

    ui::DrawRecess(draw, Min(zones.title), Max(zones.title));
    ImGui::SetCursorScreenPos({zones.title.x + 8.f * u, zones.title.y});
    ui::HeadingText("EMBER DUNGEON", 14.f * u);
    {
        ui::FBodyFont body(12.f * u);
        ImGui::SetCursorScreenPos({zones.title.x + 8.f * u, zones.title.y + 16.f * u});
        ImGui::TextDisabled("%s  /  seed %u", run.roomName.c_str(), run.seed);
    }

    const FRect& order = zones.turnOrder;
    DrawDungeonImage(registry, EDungeonSprite::HudTurnBar, draw, Min(order), Max(order));
    const float portrait = std::min(26.f * u, order.h);
    float x = order.x + 12.f * u;
    const float y = order.y + (order.h - portrait) * .5f;
    char round[32];
    std::snprintf(round, sizeof round, "ROUND %llu", static_cast<unsigned long long>(snapshot.roundNumber));
    Label(draw, {x, y + (portrait - 12.f * u) * .5f}, 12.f * u, pal::kMuted, round);
    x += 58.f * u;
    const float last = order.right() - 8.f * u;
    for (const auto& entry : snapshot.entries) {
        if (entry.state == ETurnEntryState::Skipped || entry.state == ETurnEntryState::Completed) continue;
        if (x + portrait > last) break;
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entry.entity;
        const ImVec2 min{x, y};
        const ImVec2 max{x + portrait, y + portrait};
        ui::DrawSlotFrame(draw, min, max, FactionTrim(registry, entry.entity, current), current);
        DrawPortraitCrop(registry, draw, entry.entity, {min.x + 3.f * u, min.y + 3.f * u}, {max.x - 3.f * u, max.y - 3.f * u},
                         !IsAlive(registry, entry.entity));
        if (ImGui::IsMouseHoveringRect(min, max)) ImGui::SetTooltip("%s", UnitDisplayName(registry, entry.entity).c_str());
        x += portrait + 3.f * u;
    }

    ui::DrawRecess(draw, Min(zones.status), Max(zones.status));
    char depth[96];
    std::snprintf(depth, sizeof depth, "DEPTH %02d  |  ROOM %d / 3", run.depth, run.room + 1);
    char stats[96];
    std::snprintf(stats, sizeof stats, "MATERIALS %d  |  CHECKPOINT %d  |  NODES %d", run.materials, run.cleared,
                  UnspentNodePoints(run));
    const float right = zones.status.right() - 8.f * u;
    {
        ImFont* heading = ui::HeadingFont();
        if (heading) ImGui::PushFont(heading, 13.f * u);
        ImGui::SetCursorScreenPos({right - ImGui::CalcTextSize(depth).x, zones.status.y + 1.f * u});
        ImGui::TextColored(pal::kGoldV, "%s", depth);
        if (heading) ImGui::PopFont();
    }
    {
        // The font scope must close before the window does; ImGui asserts on an unbalanced font stack in End().
        ui::FBodyFont body(12.f * u);
        ImGui::SetCursorScreenPos({right - ImGui::CalcTextSize(stats).x, zones.status.y + 16.f * u});
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
    if (!ui::BeginZone("Targets", Min(card), Size(card))) {
        ui::EndZone();
        return;
    }
    DrawHudPanel(registry, ImGui::GetWindowDrawList(), card);
    {
        // The font scope must close before the window does; ImGui asserts on an unbalanced font stack in End().
        ui::FBodyFont body(kBodySize * u);
        const auto& points = registry.get<FActionPoints>(actor);
        ImGui::SetCursorScreenPos({card.x + 16.f * u, card.y + 12.f * u});
        ImGui::TextColored(pal::kGoldV, "%s", row->displayName.c_str());
        ImGui::SetCursorScreenPos({card.x + 16.f * u, card.y + 31.f * u});
        ImGui::TextDisabled("Range %d  |  AP %d  |  %s", skill->range, skill->apCost, skill->shape.c_str());
        ImGui::SetCursorScreenPos({card.x + 16.f * u, card.y + 54.f * u});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.f * u, 1.f * u});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.f * u, 4.f * u});
        const float available = card.w - 32.f * u;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float rowStart = ImGui::GetCursorPosX();
        float used = 0.f;
        for (const auto target : registry.view<FHealth, FGridPosition>()) {
            if (!CanTargetWithSkill(registry, actor, target, *skill)) continue;
            const auto name = FirstName(UnitDisplayName(registry, target));
            const float width = ImGui::CalcTextSize(name.c_str()).x + 12.f * u;
            if (used > 0.f && used + spacing + width <= available) {
                ImGui::SameLine();
                used += spacing + width;
            } else {
                if (used > 0.f) ImGui::SetCursorPosX(rowStart);
                used = width;
            }
            ImGui::PushID(static_cast<int>(entt::to_integral(target)));
            ImGui::BeginDisabled(points.skill < skill->apCost || !queue.pending.empty());
            if (ui::Button(name.c_str(), {0.f, 24.f * u})) {
                queue.pending.push_back(FActionRequest{actor, target, slots[selectedSkill].skillId, *snapshot.currentTurn,
                                                       registry.get<FGridPosition>(target)});
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::PopStyleVar(2);
        // Without a target in range no item follows the cursor move above; ImGui asserts unless something validates it.
        ImGui::NewLine();
    }
    ui::EndZone();
}

// The acting unit: name, life and action points, in a small panel beside the skill bar.
void UnitReadout(entt::registry& registry, const FUiLayout& layout, const FTurnSnapshot& snapshot,
                 const FBattleOutcome& outcome, bool playerTurn) {
    if (!snapshot.currentTurn || outcome.resolved) return;
    const auto actor = snapshot.currentTurn->actor;
    if (!registry.all_of<FHealth>(actor)) return;
    const float u = layout.scale;
    const FRect& zone = layout.zones.unit;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    DrawHudPanel(registry, draw, zone);
    const bool party = registry.all_of<FPartyMember>(actor);
    const auto full = UnitDisplayName(registry, actor);
    const auto first = FirstName(full);
    // Everything stays inside the panel picture's frame.
    const float left = zone.x + 16.f * u;
    const float width = zone.w - 32.f * u;

    ImGui::SetCursorScreenPos({left, zone.y + 14.f * u});
    ui::HeadingText(first.c_str(), 14.f * u, party ? pal::kGoldV : pal::kEnemyV);
    if (ImFont* heading = ui::HeadingFont()) {
        const float nameWidth = heading->CalcTextSizeA(14.f * u, 1000.f, 0.f, first.c_str()).x;
        Label(draw, {left + nameWidth + 6.f * u, zone.y + 16.f * u}, 12.f * u, pal::kMuted, RoleName(full).c_str());
    }

    const auto& hp = registry.get<FHealth>(actor);
    const ImVec2 barMin{left, zone.y + 32.f * u};
    const ImVec2 barMax{left + width, barMin.y + 13.f * u};
    ui::DrawLifeBar(draw, barMin, barMax, static_cast<float>(hp.current) / std::max(1, hp.max));
    char numbers[32];
    std::snprintf(numbers, sizeof numbers, "%d / %d", hp.current, hp.max);
    Label(draw, {barMin.x + (width - LabelWidth(12.f * u, numbers)) * .5f, barMin.y}, 12.f * u,
          IM_COL32(245, 232, 205, 255), numbers);

    const float rowY = zone.y + 50.f * u;
    if (!playerTurn) {
        Label(draw, {left, rowY}, 13.f * u, pal::kEnemy, "Planning...");
        return;
    }
    const auto& points = registry.get<FActionPoints>(actor);
    const auto gems = [&](const char* label, float x, int filled, int total, ImU32 color) {
        Label(draw, {x, rowY}, 12.f * u, pal::kMuted, label);
        const float start = x + LabelWidth(12.f * u, label) + 8.f * u;
        for (int index = 0; index < total; ++index) {
            ui::DrawGem(draw, {start + static_cast<float>(index) * 11.f * u, rowY + 7.f * u}, 4.f * u, color, index < filled);
        }
    };
    gems("M", left, points.move, points.moveMax, pal::kMove);
    gems("S", left + width * .5f, points.skill, points.skillMax, pal::kSkill);
}

void PartyPortraits(entt::registry& registry, const FUiLayout& layout, const FTurnSnapshot& snapshot) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.party;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float well = 72.f * u;
    float x = zone.x;
    for (const auto entity : registry.view<FPartyMember, FHealth>()) {
        const auto& hp = registry.get<FHealth>(entity);
        const bool current = snapshot.currentTurn && snapshot.currentTurn->actor == entity;
        const ImVec2 min{x, zone.y};
        const ImVec2 max{x + well, zone.y + well};
        const ImVec2 innerMin{min.x + 6.f * u, min.y + 6.f * u};
        const ImVec2 innerMax{max.x - 6.f * u, max.y - 6.f * u};
        // The frame picture has a see-through centre, so the portrait goes in first.
        draw->AddRectFilled(innerMin, innerMax, IM_COL32(8, 9, 12, 235));
        DrawPortraitCrop(registry, draw, entity, innerMin, innerMax, !IsAlive(registry, entity));
        DrawDungeonImage(registry, EDungeonSprite::HudPortrait, draw, min, max);
        if (current) draw->AddRect({min.x - 1.f, min.y - 1.f}, {max.x + 1.f, max.y + 1.f}, pal::kGoldBright, 2.f, 0, 2.f);
        ui::DrawLifeBar(draw, {min.x + 2.f * u, zone.y + 74.f * u}, {max.x - 2.f * u, zone.y + 82.f * u},
                        static_cast<float>(hp.current) / std::max(1, hp.max));
        if (ImGui::IsMouseHoveringRect(min, max)) {
            ImGui::SetTooltip("%s\nHP %d / %d", UnitDisplayName(registry, entity).c_str(), hp.current, hp.max);
        }
        x += 84.f * u;
    }
}

void SkillSlots(entt::registry& registry, const FUiLayout& layout, const FTurnSnapshot& snapshot,
                const FBattleOutcome& outcome, bool playerTurn, int& selectedSkill,
                const gameplay::FContentRegistry& content) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.skills;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    DrawDungeonImage(registry, EDungeonSprite::HudBar, draw, Min(zone), Max(zone));
    if (!playerTurn) {
        const char* banner = outcome.resolved ? "ROOM RESOLVED" : "ENEMY TURN";
        ImFont* heading = ui::HeadingFont();
        const float bannerWidth = heading ? heading->CalcTextSizeA(20.f * u, 1000.f, 0.f, banner).x : LabelWidth(20.f * u, banner);
        ImGui::SetCursorScreenPos({zone.x + (zone.w - bannerWidth) * .5f, zone.y + 28.f * u});
        ui::HeadingText(banner, 20.f * u, outcome.resolved ? pal::kGoldV : pal::kEnemyV);
        return;
    }

    const auto actor = snapshot.currentTurn->actor;
    const auto& points = registry.get<FActionPoints>(actor);
    const auto& slots = registry.get<FSkillLoadout>(actor).slots;
    const float slot = 56.f * u;
    const float pad = 18.f * u;
    const float step = (zone.w - pad * 2.f - slot) / 3.f;
    const float top = zone.y + (zone.h - slot) * .5f;
    for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
        const ImVec2 min{zone.x + pad + static_cast<float>(index) * step, top};
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
            DrawDungeonImage(registry, SkillIconSprite(slots[index].skillId).value_or(FallbackSkillIcon(*resolved)), draw,
                             {min.x + 4.f * u, min.y + 4.f * u}, {max.x - 4.f * u, max.y - 4.f * u}, {0, 0}, {1, 1},
                             affordable ? IM_COL32_WHITE : IM_COL32(90, 90, 90, 255));
            ui::DrawGem(draw, {max.x - 9.f * u, max.y - 9.f * u}, 5.f * u, pal::kSkill, affordable);
            char cost[8];
            std::snprintf(cost, sizeof cost, "%d", resolved->apCost);
            Label(draw, {max.x - 13.f * u, max.y - 17.f * u}, 13.f * u, IM_COL32(255, 255, 255, 255), cost);
        }
        char key[4];
        std::snprintf(key, sizeof key, "%d", index + 1);
        Label(draw, {min.x + 6.f * u, min.y + 3.f * u}, 13.f * u, pal::kGoldBright, key);
        if (hot && resolved) {
            ImGui::SetTooltip("%s\nAP %d | range %d | power %d | %s\nRunes: %zu | statuses: %zu",
                row ? row->displayName.c_str() : slots[index].skillId.c_str(), resolved->apCost, resolved->range,
                resolved->power, resolved->shape.c_str(), slots[index].runeIds.size(), resolved->statuses.size());
        }
        ImGui::PopID();
    }
}

// Two lines of the battle so far; the longer record opens when the mouse rests on them.
void Journal(entt::registry& registry, const FUiLayout& layout) {
    const auto* log = registry.ctx().find<FCombatLog>();
    if (log == nullptr || log->lines.empty()) return;
    const float u = layout.scale;
    const FRect& zone = layout.zones.journal;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ui::DrawRecess(draw, Min(zone), Max(zone));
    const float size = 12.f * u;
    const ImVec4 clip{zone.x + 6.f * u, zone.y, zone.right() - 6.f * u, zone.bottom()};
    const std::size_t shown = std::min<std::size_t>(log->lines.size(), 2);
    const std::size_t first = log->lines.size() - shown;
    for (std::size_t index = first; index < log->lines.size(); ++index) {
        const float y = zone.y + 2.f * u + static_cast<float>(index - first) * 13.f * u;
        draw->AddText(ImGui::GetFont(), size, {clip.x, y}, index + 1 == log->lines.size() ? pal::kText : pal::kMuted,
                      log->lines[index].c_str(), nullptr, 0.f, &clip);
    }
    if (!ImGui::IsMouseHoveringRect(Min(zone), Max(zone))) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(360.f * u);
    for (std::size_t index = log->lines.size() > 8 ? log->lines.size() - 8 : 0; index < log->lines.size(); ++index) {
        ImGui::TextUnformatted(log->lines[index].c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// The controls stay out of the way behind a small help mark.
void Hints(const FUiLayout& layout) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.hints;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ui::DrawRecess(draw, Min(zone), Max(zone));
    const float size = 22.f * u;
    Label(draw, {zone.x + (zone.w - LabelWidth(size, "?")) * .5f, zone.y + (zone.h - size) * .5f}, size, pal::kMuted, "?");
    if (!ImGui::IsMouseHoveringRect(Min(zone), Max(zone))) return;
    ImGui::BeginTooltip();
    ImGui::TextUnformatted("WASD move  |  1-4 skill");
    ImGui::TextUnformatted("Space: end turn");
    ImGui::TextColored({.35f, .88f, 1.f, 1.f}, "Cyan");
    ImGui::SameLine(0.f, 0.f);
    ImGui::TextUnformatted(" reach  ");
    ImGui::SameLine(0.f, 0.f);
    ImGui::TextColored({1.f, .43f, .35f, 1.f}, "Red");
    ImGui::SameLine(0.f, 0.f);
    ImGui::TextUnformatted(" hit");
    ImGui::EndTooltip();
}

// The picture sits under an ImGui button with a clear face, so the label still reaches ImGui's text log.
bool EndTurnButton(entt::registry& registry, const FUiLayout& layout, bool enabled) {
    const FRect& zone = layout.zones.endTurn;
    const bool hot = enabled && ImGui::IsMouseHoveringRect(Min(zone), Max(zone));
    DrawDungeonImage(registry, EDungeonSprite::HudButton, ImGui::GetWindowDrawList(), Min(zone), Max(zone), {0, 0}, {1, 1},
                     !enabled ? IM_COL32(120, 120, 120, 255) : hot ? IM_COL32(255, 236, 190, 255) : IM_COL32_WHITE);
    ImGui::SetCursorScreenPos(Min(zone));
    ImGui::PushStyleColor(ImGuiCol_Button, kClear);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kClear);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kClear);
    ImGui::PushStyleColor(ImGuiCol_Text, pal::kGoldBrightV);
    ImFont* heading = ui::HeadingFont();
    if (heading) ImGui::PushFont(heading, 15.f * layout.scale);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button("END TURN", Size(zone));
    ImGui::EndDisabled();
    if (heading) ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return pressed;
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

    TopBar(registry, *layout, run, snapshot);

    const bool playerTurn = !outcome->resolved && snapshot.currentTurn && IsAlive(registry, snapshot.currentTurn->actor) &&
        registry.all_of<FPartyMember, FActionPoints, FSkillLoadout>(snapshot.currentTurn->actor);
    TargetCard(registry, *layout, snapshot, *outcome, playerTurn, selectedSkill, queue, content);

    // The bottom clusters: party on the left, skill bar and acting unit in the middle, end turn on the right.
    if (!ui::BeginZone("Dock", Min(layout->dock), Size(layout->dock))) {
        ui::EndZone();
        return;
    }
    PartyPortraits(registry, *layout, snapshot);
    SkillSlots(registry, *layout, snapshot, *outcome, playerTurn, selectedSkill, content);
    UnitReadout(registry, *layout, snapshot, *outcome, playerTurn);
    Journal(registry, *layout);
    Hints(*layout);
    if (playerTurn && EndTurnButton(registry, *layout, queue.pending.empty())) {
        runtime.requests.push_back(FEndTurn{*snapshot.currentTurn});
    }
    ui::EndZone();
}

} // namespace game::ecs
