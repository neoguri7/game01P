#include "ecs/systems/EmberCombatHudSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
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
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FStatusContent.h"
#include "gameplay/dungeon/FBattleObjective.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTerrain.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FSettings.h"
#include "presentation/FUiFrame.h"
#include "presentation/FUiTheme.h"

namespace game::ecs {
namespace {

namespace pal = ui::palette;
namespace hud = ui::hud;
using ui::EHudText;
using ui::FRect;
using ui::FUiLayout;
using ui::HudTextPx;

constexpr float kBodySize = 15.f;
constexpr ImVec4 kClear{0.f, 0.f, 0.f, 0.f};

// WP-12 overlay metrics (design px). Board plates that relate to a cell use the WORLD scale (view->scale)
// for their offsets — the sprite and its HP bar are world art — while their font sizes use the layout
// scale. WP-12c text sizes live in the single source of truth game::ui::hud (FUiTheme.h).
constexpr float kIntentGlyphPx = 22.f;
constexpr float kIntentPlatePad = 3.f;
constexpr float kIntentPlateGap = 3.f;
constexpr float kIntentLethalBorder = 2.f;
constexpr float kIntentStackStep = 30.f;     // one plate height between same-cell plates (valid + invalid)
constexpr float kUnitPlateClearance = 4.f;   // gap between a unit's HP bar and a board plate
constexpr float kEmptyCellPlateLift = 6.f;   // plate bottom above an empty cell's centre
constexpr float kRibbonPortraitMax = 20.f;
constexpr float kThreatPlatePad = 3.f;
constexpr float kThreatPlateInset = 2.f;
constexpr float kDotPlatePad = 2.f;
constexpr float kDotPlateInset = 2.f;
constexpr float kStatusGemRadius = 3.f;
constexpr float kStatusGemStep = 8.f;
constexpr int kStatusGemMax = 5;
constexpr float kStatusTopInset = 7.f;
constexpr float kDamagePopRise = 26.f;

// WP-12c board draw order (F1): the background draw list is split into two channels while the three board
// overlays run. Channel 0 carries every plate caret, channel 1 every plate and its text/icon (DrawHoverText
// included); the merge appends channel 1 over channel 0, so a caret can never cross a plate it was drawn
// after — not inside one stack, and not between an intent plate and a higher preview/pop plate.
constexpr int kBoardCaretChannel = 0;
constexpr int kBoardPlateChannel = 1;

constexpr ImU32 kIntentDim = IM_COL32(115, 115, 115, 255);
constexpr ImU32 kIntentNumber = IM_COL32(255, 226, 190, 255);
constexpr ImU32 kIntentLethal = IM_COL32(255, 70, 60, 255);
constexpr ImU32 kIntentBack = IM_COL32(8, 8, 12, 220);
constexpr ImU32 kThreatText = IM_COL32(255, 130, 95, 255);
constexpr ImU32 kLethalText = IM_COL32(255, 70, 60, 255);
constexpr ImU32 kHealText = IM_COL32(110, 230, 130, 255);
constexpr ImU32 kDamagePopText = IM_COL32(255, 96, 80, 255);
constexpr ImU32 kStatusDot = IM_COL32(240, 130, 50, 255);
constexpr ImU32 kStatusControl = IM_COL32(180, 100, 240, 255);
constexpr ImU32 kStatusStat = IM_COL32(100, 200, 120, 255);
constexpr ImU32 kStatusMoveLock = IM_COL32(90, 150, 240, 255);
constexpr ImU32 kStatusOther = IM_COL32(160, 160, 160, 255);

EDungeonSprite IntentGlyphSprite(const std::string& glyph) {
    if (glyph == "boots") return EDungeonSprite::IconBoots;
    if (glyph == "bow") return EDungeonSprite::IconBow;
    if (glyph == "spell") return EDungeonSprite::IconSpell;
    return EDungeonSprite::IconSword;
}

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
    // The slice size lives in ui::hud beside the frame-band constants the readout budgets use (WP-27h).
    ui::DrawNineSlice(draw, DungeonSkinPiece(registry, EDungeonSprite::HudPanel), Min(rect), Max(rect),
                      hud::kPanelCorner);
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

/// Bottom line of a board plate over `cell`: clear of the occupant's HP bar when a living unit stands
/// there, a low line beside the diamond otherwise. World offsets use the iso scale, mirroring DrawUnit.
float PlateBottom(entt::registry& registry, const FIsoView& view, int x, int y) {
    const glm::vec2 center = view.cellCenter(x, y);
    const entt::entity occupant = FindGridOccupant(registry, x, y);
    if (occupant != entt::null && IsAlive(registry, occupant)) {
        return center.y - (kUnitHeadAboveFeet + kUnitHealthBarInset + kUnitHealthBarHeight + kUnitPlateClearance) *
                              view.scale;
    }
    return center.y - kEmptyCellPlateLift * view.scale;
}

/// Where a board plate's caret points (WP-12c): the occupant's HP-bar top line — the bar DrawUnit draws at
/// sprite top + inset — or the cell's centre line on an empty cell. Same occupancy rule as PlateBottom.
ImVec2 CellAnchor(entt::registry& registry, const FIsoView& view, int x, int y) {
    const glm::vec2 center = view.cellCenter(x, y);
    const entt::entity occupant = FindGridOccupant(registry, x, y);
    if (occupant != entt::null && IsAlive(registry, occupant)) {
        return {center.x, center.y - (kUnitHeadAboveFeet - kUnitHealthBarInset) * view.scale};
    }
    return {center.x, center.y};
}

/// A dark rounded plate (alpha >= 210) behind one line of text, so a number never sits bare over sprite art.
void DrawPlateBehindText(ImDrawList* draw, ImVec2 at, float width, float height, float pad, float radius,
                         ImU32 color = kIntentBack) {
    draw->AddRectFilled({at.x - pad, at.y - pad}, {at.x + width + pad, at.y + height + pad}, color, radius);
}

/// Same RGB, replaced alpha — for the fading damage pops.
ImU32 WithAlpha(ImU32 color, int alpha) {
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT);
}

/// Board tooltip drawn on the background list: the intent overlay runs outside any ImGui window, and this
/// keeps the hover text independent of the window stack.
void DrawHoverText(ImDrawList* draw, const char* text, float size) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, 1000.f, 0.f, text);
    const ImVec2 min{mouse.x + 14.f, mouse.y + 18.f};
    draw->AddRectFilled({min.x - 4.f, min.y - 2.f}, {min.x + extent.x + 4.f, min.y + extent.y + 2.f},
                        IM_COL32(10, 10, 14, 220), 3.f);
    Label(draw, min, size, pal::kText, text);
}

/// WP-12 status badge colour by kind (existing vocabulary, no new art).
ImU32 StatusColor(const gameplay::FStatusContent* definition) {
    if (definition == nullptr) return kStatusOther;
    if (definition->kind == gameplay::kStatusKindDot) return kStatusDot;
    if (definition->kind == gameplay::kStatusKindControl) return kStatusControl;
    if (definition->kind == gameplay::kStatusKindStat) return kStatusStat;
    if (definition->kind == gameplay::kStatusKindMoveLock) return kStatusMoveLock;
    return kStatusOther;
}

/// Readable status summary for the always-visible line. `headline` is "2 status 3t" (count + longest
/// remaining duration); `dot` is "Burn 4" when exactly one dot is active, "dot 4" otherwise. The party card
/// draws them on two short lines (72px card); the unit readout joins them.
struct FStatusSummary {
    std::string headline;
    std::string dot;

    [[nodiscard]] bool empty() const noexcept { return headline.empty(); }
    [[nodiscard]] std::string joined() const { return dot.empty() ? headline : headline + ", " + dot; }
};

FStatusSummary SummarizeStatuses(entt::registry& registry, entt::entity unit) {
    const auto* active = registry.try_get<FActiveStatuses>(unit);
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (active == nullptr || active->effects.empty() || content == nullptr) return {};
    int nextTick = 0;
    int longest = 0;
    std::string singleDot;
    int dotCount = 0;
    for (const FStatusEffect& effect : active->effects) {
        longest = std::max(longest, effect.turnsRemaining);
        const auto* definition = content->statuses.find(effect.statusId);
        if (definition != nullptr && definition->kind == gameplay::kStatusKindDot) {
            nextTick += std::max(0, effect.magnitude);
            ++dotCount;
            singleDot = definition->displayName;
        }
    }
    FStatusSummary summary;
    summary.headline = std::to_string(active->effects.size()) + " status " + std::to_string(longest) + "t";
    if (nextTick > 0) {
        summary.dot = (dotCount == 1 ? singleDot : std::string("dot")) + " " + std::to_string(nextTick);
    }
    return summary;
}

/// The one turn-skip decision (WP-27h): the same IsTurnSkipped/CollectStatuses pair the gems use, so the
/// readout's SKIP marker and the gem badge can never drift apart. The rule itself lives in FStatusRules.
bool TurnSkipped(entt::registry& registry, entt::entity unit) {
    const auto* active = registry.try_get<FActiveStatuses>(unit);
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (active == nullptr || content == nullptr) return false;
    return IsTurnSkipped(*active, CollectStatuses(*active, *content));
}

/// One gem per status (kind shape + colour, capped) plus the decided turn-loss marker. `bottom` puts the row
/// at the card's lower edge (the portrait well's top-right is taken by the threat plate); `drawSkip` lets the
/// unit readout place SKIP at the head of its summary row instead (WP-27h), clear of the name.
void DrawStatusGems(entt::registry& registry, ImDrawList* draw, entt::entity unit, ImVec2 min, ImVec2 max, float u,
                    bool bottom = false, bool drawSkip = true) {
    const auto* active = registry.try_get<FActiveStatuses>(unit);
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (active == nullptr || active->effects.empty() || content == nullptr) return;
    const std::vector<const gameplay::FStatusContent*> definitions = CollectStatuses(*active, *content);
    if (drawSkip && IsTurnSkipped(*active, definitions)) {
        Label(draw, {min.x + 4.f * u, min.y + 4.f * u}, HudTextPx(hud::kStatusSummaryPx, u, EHudText::Zone), kLethalText,
              "SKIP");
    }
    const float rowY = bottom ? max.y - 6.f * u : min.y + kStatusTopInset * u;
    float x = max.x - 6.f * u;
    const int gems = std::min(static_cast<int>(active->effects.size()), kStatusGemMax);
    for (int index = 0; index < gems; ++index) {
        const auto* definition = content->statuses.find(active->effects[static_cast<std::size_t>(index)].statusId);
        // WP-12c: shape per kind next to the kind colour (XAG 103 — never colour alone).
        const auto kind = definition != nullptr ? std::string_view(definition->kind) : std::string_view{};
        ui::DrawStatusGem(draw, {x, rowY}, kStatusGemRadius * u, ui::StatusGemShapeFor(kind), StatusColor(definition));
        x -= kStatusGemStep * u;
    }
}

/// Hover detail: duration/magnitude per status, the tick chance and the decided skip. An `on_apply`
/// control's roll happened when it landed, so only its decided outcome (skipPending) is shown — never an
/// "apply chance" (D-12).
std::string StatusTooltip(entt::registry& registry, entt::entity unit) {
    const auto* active = registry.try_get<FActiveStatuses>(unit);
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (active == nullptr || active->effects.empty() || content == nullptr) return {};
    const std::vector<const gameplay::FStatusContent*> definitions = CollectStatuses(*active, *content);
    std::string text;
    for (const FStatusEffect& effect : active->effects) {
        const auto* definition = content->statuses.find(effect.statusId);
        if (definition == nullptr) continue;
        if (!text.empty()) text += '\n';
        text += definition->displayName + " (" + std::to_string(effect.turnsRemaining) + "t)";
        if (definition->kind == gameplay::kStatusKindDot) {
            text += " / " + std::to_string(effect.magnitude) + " dmg/turn";
        } else if (definition->kind == gameplay::kStatusKindControl) {
            if (!IsOnApplyControl(*definition)) {
                // A turn_start control's roll still lies ahead: show the tick chance it will use.
                text += " / " + std::to_string(ControlTickChance(effect.magnitude, definition->chanceCap)) + "%";
            }
        } else if (definition->kind == gameplay::kStatusKindStat) {
            text += " / " + std::to_string(effect.magnitude);
        }
        if (effect.skipPending) text += " / turn loss decided";
    }
    if (IsTurnSkipped(*active, definitions)) text += "\nTurn skipped";
    return text;
}

/// D-12 apply chance belongs to the skill that is about to be used, not to an applied status: the resolver
/// supplies the real rune `magnitudeDelta`, so nothing is re-derived from stored state.
void AppendControlChances(std::string& text, const gameplay::FContentRegistry& content,
                          const FResolvedSkill& skill) {
    for (const FResolvedStatus& status : skill.statuses) {
        const auto* definition = content.statuses.find(status.statusId);
        if (definition == nullptr || !IsOnApplyControl(*definition)) continue;
        text += "\n" + definition->displayName + ": " +
                std::to_string(ControlApplyChance(*definition, status.magnitudeDelta)) + "% to apply";
    }
}

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
    ui::HeadingText("EMBER DUNGEON", hud::kTopBarTitlePx * u);
    {
        ui::FBodyFont body(12.f * u);
        ImGui::SetCursorScreenPos({zones.title.x + 8.f * u, zones.title.y + 16.f * u});
        // WP-39: while the battle carries an objective the line shows it (room name and seed stay in the
        // run log); without one it keeps the room name and seed.
        const auto* objective = registry.ctx().find<FBattleObjective>();
        if (objective == nullptr) {
            ImGui::TextDisabled("%s  /  seed %u", run.roomName.c_str(), run.seed);
        } else {
            char line[96];
            std::snprintf(line, sizeof line, "%s +%d: %s", objective->def.displayName.c_str(),
                          objective->def.rewardMaterials, objective->def.text.c_str());
            bool lost = objective->settled && !objective->met;
            if (!objective->settled) {
                if (const auto* stats = registry.ctx().find<FRoomBattleStats>(); stats != nullptr && stats->active) {
                    lost = IsBattleObjectiveFailed(objective->def, stats->rounds, stats->ko);
                }
            }
            if (objective->settled && objective->met) {
                std::snprintf(line + std::strlen(line), sizeof line - std::strlen(line), " (met)");
                ImGui::TextColored(pal::kGoldV, "%s", line);
            } else if (lost) {
                std::snprintf(line + std::strlen(line), sizeof line - std::strlen(line), " (lost)");
                ImGui::TextDisabled("%s", line);
            } else {
                ImGui::TextColored(pal::kGoldV, "%s", line);
            }
        }
    }

    const FRect& order = zones.turnOrder;
    DrawDungeonImage(registry, EDungeonSprite::HudTurnBar, draw, Min(order), Max(order));
    // WP-12: two compressed rows inside the same turn_order zone — the current round (settled order,
    // unchanged) and the marker that the next round is decided when it starts. No next-round preview.
    // WP-12c: the portrait cap and the ROUND step follow the 15px row (zone h 36).
    const float portrait = std::min(kRibbonPortraitMax * u, order.h * .62f);
    float x = order.x + 12.f * u;
    const float y = order.y + 1.f * u;
    char round[32];
    std::snprintf(round, sizeof round, "ROUND %llu", static_cast<unsigned long long>(snapshot.roundNumber));
    const float roundPx = HudTextPx(hud::kRibbonRoundPx, u, EHudText::Zone);
    Label(draw, {x, y + (portrait - roundPx) * .5f}, roundPx, pal::kMuted, round);
    x += std::max(54.f * u, LabelWidth(roundPx, round) + 10.f * u);
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
    // Row 2: the speculative marker, in light parchment and clipped to the zone (never spills out). It must
    // fit the authored 320px bar at this size; the assert catches a font or string change early.
    const float markerPx = HudTextPx(hud::kRibbonMarkerPx, u, EHudText::Zone);
    IM_ASSERT(LabelWidth(markerPx, kNextRoundMarker) <= order.w - 24.f * u);
    const ImVec2 markerAt{order.x + 12.f * u, order.bottom() - markerPx - 1.f * u};
    const ImVec4 markerClip{order.x + 12.f * u, markerAt.y, order.right() - 12.f * u, order.bottom()};
    draw->AddText(ImGui::GetFont(), markerPx, markerAt, pal::kText, kNextRoundMarker, nullptr, 0.f, &markerClip);

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
                // 관례 a: the button submits immediately; a refusal is recorded and logged inside the API.
                [[maybe_unused]] const bool submitted = SubmitHudAction(
                    registry,
                    FActionRequest{actor, target, slots[selectedSkill].skillId, *snapshot.currentTurn,
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
    ui::HeadingText(first.c_str(), hud::kUnitNamePx * u, party ? pal::kGoldV : pal::kEnemyV);
    if (ui::HeadingFont() != nullptr) {
        // WP-12c: measure the name at the EFFECTIVE size — HeadingText scales through ImGui's FontScaleMain,
        // so the role must clear the width the player actually sees (the "ROWANArcher" overlap at 1.25).
        const float nameWidth = ui::HeadingTextWidth(first.c_str(), hud::kUnitNamePx * u);
        Label(draw, {left + nameWidth + 6.f * u, zone.y + 16.f * u}, HudTextPx(hud::kUnitRolePx, u, EHudText::Zone),
              pal::kMuted, RoleName(full).c_str());
    }

    const auto& hp = registry.get<FHealth>(actor);
    const ImVec2 barMin{left, zone.y + 32.f * u};
    const ImVec2 barMax{left + width, barMin.y + 16.f * u};
    ui::DrawLifeBar(draw, barMin, barMax, static_cast<float>(hp.current) / std::max(1, hp.max));
    char numbers[32];
    std::snprintf(numbers, sizeof numbers, "%d / %d", hp.current, hp.max);
    // WP-12c: the 18px number does not fit the 16u bar as a line box, so a dark backing strip removes the
    // "lowest-contrast area" (XAG 102) while the digits' ink (+3u..+13.5u) stays inside the strip.
    const float numberPx = HudTextPx(hud::kHpNumberPx, u, EHudText::Zone);
    const float numberW = LabelWidth(numberPx, numbers);
    const ImVec2 numberAt{barMin.x + (width - numberW) * .5f, barMin.y + 1.f * u};
    draw->AddRectFilled({numberAt.x - 2.f * u, numberAt.y}, {numberAt.x + numberW + 2.f * u, numberAt.y + numberPx + 1.f * u},
                        IM_COL32(8, 8, 12, 150), 2.f * u);
    Label(draw, numberAt, numberPx, IM_COL32(245, 232, 205, 255), numbers);

    const float rowY = zone.y + 52.f * u;
    // WP-12: status badges on the readout (count / next tick; details and the tick chance in the tooltip).
    // WP-27h: the readout draws the decided SKIP itself, at the head of the summary row, so it can never
    // cover the name (the old gems-row marker did at every scale).
    DrawStatusGems(registry, draw, actor, {left, zone.y + 8.f * u}, {left + width, zone.y + 20.f * u}, u, false,
                   false);
    const FStatusSummary statusSummary = SummarizeStatuses(registry, actor);
    const bool skipped = TurnSkipped(registry, actor);
    if (!statusSummary.empty() || skipped) {
        // WP-27h: the row sits at +63u so its ink stays inside the panel picture's inner rect; the frame
        // band is fixed SCREEN px (kPanelFrameXPx/YPx), never scaled by u. The clip keeps a long joined
        // summary off the right frame at both text scales.
        draw->PushClipRect({zone.x + hud::kPanelFrameXPx, zone.y + hud::kPanelFrameYPx},
                           {zone.right() - hud::kPanelFrameXPx, zone.bottom() - hud::kPanelFrameYPx}, true);
        const float summaryPx = HudTextPx(hud::kStatusSummaryPx, u, EHudText::Zone);
        float summaryX = left;
        if (skipped) {
            Label(draw, {summaryX, zone.y + 63.f * u}, summaryPx, kLethalText, "SKIP");
            summaryX += LabelWidth(summaryPx, "SKIP") + 6.f * u;
        }
        if (!statusSummary.empty()) {
            const std::string joined = statusSummary.joined();
            Label(draw, {summaryX, zone.y + 63.f * u}, summaryPx, pal::kMuted, joined.c_str());
        }
        draw->PopClipRect();
    }
    if (!playerTurn) {
        if (!statusSummary.empty() &&
            ImGui::IsMouseHoveringRect({left, zone.y + 63.f * u}, {left + width, zone.bottom()})) {
            ImGui::SetTooltip("%s", StatusTooltip(registry, actor).c_str());
        }
        Label(draw, {left, rowY}, HudTextPx(hud::kPlanningPx, u, EHudText::Zone), pal::kEnemy, "Planning...");
        return;
    }
    const auto& points = registry.get<FActionPoints>(actor);
    const float moveSkillPx = HudTextPx(hud::kMoveSkillPx, u, EHudText::Zone);
    const auto gems = [&](const char* label, float x, int filled, int total, ImU32 color) {
        Label(draw, {x, rowY}, moveSkillPx, pal::kMuted, label);
        const float start = x + LabelWidth(moveSkillPx, label) + 8.f * u;
        for (int index = 0; index < total; ++index) {
            ui::DrawGem(draw, {start + static_cast<float>(index) * 11.f * u, rowY + 7.f * u}, 4.f * u, color, index < filled);
        }
    };
    gems("M", left, points.move, points.moveMax, pal::kMove);
    gems("S", left + width * .5f, points.skill, points.skillMax, pal::kSkill);
    if (!statusSummary.empty() &&
        ImGui::IsMouseHoveringRect({left, zone.y + 63.f * u}, {left + width, zone.bottom()})) {
        ImGui::SetTooltip("%s", StatusTooltip(registry, actor).c_str());
    }
}

void PartyPortraits(entt::registry& registry, const FUiLayout& layout, const FTurnSnapshot& snapshot) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.party;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float well = 72.f * u;
    const FCombatInfoSnapshot* info = FindCombatInfoSnapshot(registry);
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

        // WP-12: this round's promised damage is always visible (no hover) on a plate inside the portrait well;
        // lethal is red. The old bottom-right corner sat on the frame's rivet and was half covered.
        int incoming = 0;
        bool lethal = false;
        if (info != nullptr) {
            for (const FThreatTotal& threat : info->threats) {
                if (threat.unit != entity) continue;
                incoming = threat.incomingThisRound;
                lethal = threat.lethalThisRound;
                break;
            }
        }
        if (incoming > 0) {
            char value[16];
            std::snprintf(value, sizeof value, "-%d", incoming);
            const float size = HudTextPx(hud::kThreatNumberPx, u, EHudText::Zone);
            const float pad = kThreatPlatePad * u;
            const float width = LabelWidth(size, value);
            const ImVec2 plateMax{innerMax.x - kThreatPlateInset * u,
                                  innerMin.y + kThreatPlateInset * u + size + 2.f * pad};
            const ImVec2 plateMin{plateMax.x - width - 2.f * pad, plateMax.y - size - 2.f * pad};
            draw->AddRectFilled(plateMin, plateMax, kIntentBack, 2.f * u);
            if (lethal) draw->AddRect(plateMin, plateMax, kIntentLethal, 2.f * u, 0, kIntentLethalBorder * u);
            Label(draw, {plateMin.x + pad, plateMin.y + pad}, size, lethal ? kLethalText : kThreatText, value);
        }
        // The card keeps only what fits legibly: gems + SKIP (drawn below/above) and the dot's next tick on
        // its own plate. The "N status Nt" headline lives in the card tooltip with the per-status detail.
        const FStatusSummary statusSummary = SummarizeStatuses(registry, entity);
        if (!statusSummary.dot.empty()) {
            const float size = HudTextPx(hud::kStatusSummaryPx, u, EHudText::Zone);
            const float pad = kDotPlatePad * u;
            const float width = LabelWidth(size, statusSummary.dot.c_str());
            const ImVec2 at{innerMin.x + kDotPlateInset * u + pad, innerMax.y - kDotPlateInset * u - pad - size};
            DrawPlateBehindText(draw, at, width, size, pad, 2.f * u);
            Label(draw, at, size, kStatusDot, statusSummary.dot.c_str());
        }
        DrawStatusGems(registry, draw, entity, min, max, u, true);
        if (lethal) draw->AddRect({min.x - 2.f, min.y - 2.f}, {max.x + 2.f, max.y + 2.f}, kIntentLethal, 2.f, 0, 2.f);

        if (ImGui::IsMouseHoveringRect(min, max)) {
            const std::string statusTip = StatusTooltip(registry, entity);
            std::string tip = UnitDisplayName(registry, entity) + "\nHP " + std::to_string(hp.current) + " / " +
                              std::to_string(hp.max);
            if (!statusSummary.empty()) tip += "\n" + statusSummary.headline;
            if (!statusTip.empty()) tip += "\n" + statusTip;
            ImGui::SetTooltip("%s", tip.c_str());
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
    const float slotSize = 56.f * u;
    const float keyPx = HudTextPx(hud::kSkillKeyPx, u, EHudText::Zone);
    const float costPx = HudTextPx(hud::kSkillCostPx, u, EHudText::Zone);
    for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
        const FRect rect = ui::SkillSlotRect(zone, u, index);
        const ImVec2 min{rect.x, rect.y};
        const ImVec2 max{rect.right(), rect.bottom()};
        const auto resolved = ResolveSkillSlot(slots[index], content);
        const auto* row = content.skills.find(slots[index].skillId);
        const bool affordable = resolved && points.skill >= resolved->apCost;
        ImGui::PushID(index);
        ImGui::SetCursorScreenPos(min);
        if (ImGui::InvisibleButton("slot", {slotSize, slotSize})) selectedSkill = index;
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
            Label(draw, {max.x - 14.f * u, max.y - 18.f * u}, costPx, IM_COL32(255, 255, 255, 255), cost);
        }
        char key[4];
        std::snprintf(key, sizeof key, "%d", index + 1);
        Label(draw, {min.x + 6.f * u, min.y + 3.f * u}, keyPx, pal::kGoldBright, key);
        if (hot && resolved) {
            std::string tip = std::string(row ? row->displayName : slots[index].skillId) + "\nAP " +
                std::to_string(resolved->apCost) + " | range " + std::to_string(resolved->range) + " | power " +
                std::to_string(resolved->power) + " | " + resolved->shape + "\nRunes: " +
                std::to_string(slots[index].runeIds.size()) + " | statuses: " +
                std::to_string(resolved->statuses.size());
            AppendControlChances(tip, content, *resolved);
            ImGui::SetTooltip("%s", tip.c_str());
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
    const float size = HudTextPx(hud::kJournalPx, u, EHudText::Zone);
    const ImVec4 clip{zone.x + 6.f * u, zone.y, zone.right() - 6.f * u, zone.bottom()};
    const std::size_t shown = std::min<std::size_t>(log->lines.size(), 2);
    const std::size_t first = log->lines.size() - shown;
    for (std::size_t index = first; index < log->lines.size(); ++index) {
        const float y = zone.y + 2.f * u + static_cast<float>(index - first) * 17.f * u;
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

// The controls stay out of the way behind a small help mark; the right half is the battle-speed toggle
// (WP-12, no new zone: the hints zone is split in place).
void Hints(entt::registry& registry, const FUiLayout& layout) {
    const float u = layout.scale;
    const FRect& zone = layout.zones.hints;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ui::DrawRecess(draw, Min(zone), Max(zone));
    const FRect ask{zone.x, zone.y, zone.w * .5f, zone.h};
    const FRect speed{zone.x + zone.w * .5f, zone.y, zone.w * .5f, zone.h};
    const float size = 22.f * u;
    Label(draw, {ask.x + (ask.w - LabelWidth(size, "?")) * .5f, ask.y + (ask.h - size) * .5f}, size, pal::kMuted, "?");
    if (ImGui::IsMouseHoveringRect(Min(ask), Max(ask))) {
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

    // 관례 a: an ImGui button click may call the preference setter immediately; only the snapshot and the
    // drawing are const. Presentation dt changes, the rules clock does not.
    const char* label = CurrentSettings(registry).animSpeed >= kAnimSpeedFast ? "2x" : "1x";
    ImGui::SetCursorScreenPos(Min(speed));
    ImGui::PushStyleColor(ImGuiCol_Button, kClear);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kClear);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kClear);
    ImGui::PushStyleColor(ImGuiCol_Text, pal::kGoldBrightV);
    if (ImGui::Button(label, Size(speed))) {
        ToggleAnimSpeed(registry);
    }
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Battle speed: %s", CurrentSettings(registry).animSpeed >= kAnimSpeedFast ? "2x" : "1x");
    }
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

// WP-12 board text/icons: intent plates (glyph + always-on number), the selected-skill preview numbers and
// the damage pops. Projection is the same battle isometric view the grid renderer uses (FindBattleIsoView).
// Every plate over a standing unit sits above its HP bar (world-scale offsets, see PlateBottom); the caret
// points back to that line so a lifted plate still reads as belonging to the cell (WP-12c).
//
// Stacking / caret order rule (one place; F1):
//   1. Same-cell plates stack upward: the i-th plate on a cell lifts by PlateLiftSteps(i) steps, so the 3rd
//      and later plates stop rising and the caret length carries the affiliation (PlateLiftSteps caps at 2).
//   2. Plate order on a cell: intent plates (snapshot order), then the skill preview, then the damage pop —
//      each later overlay starts above the count of everything already on that cell.
//   3. Carets never cross a plate: every caret goes to channel 0 and every plate/text to channel 1, merged
//      caret-first by EmberCombatHudSystem::render. A caret genuinely runs from its plate's bottom edge to
//      the cell anchor; where it passes another plate of the same cell, that plate covers it.
void IntentOverlays(entt::registry& registry, const FUiLayout& layout) {
    const auto* info = FindCombatInfoSnapshot(registry);
    const auto* grid = registry.ctx().find<FGridMetrics>();
    if (info == nullptr || info->intents.empty() || grid == nullptr) return;
    const auto view = ui::FindBattleIsoView(registry, *grid);
    if (!view) return;
    const float u = layout.scale;
    // WP-12c: the free-board plate metrics follow the text scale (a 1.25 plate is taller than 30u).
    const float stackStep = HudTextPx(kIntentStackStep, u, EHudText::Free);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    // Same-cell valid and invalid badges stay separate in the snapshot (they never hide each other), so the
    // renderer stacks them: the highest-priority plate keeps the HP-bar anchor, the rest sit one plate above
    // (beyond two steps the caret length carries the affiliation — PlateLiftSteps).
    std::map<std::pair<int, int>, int> cellStacks;
    for (const FIntentBadge& badge : info->intents) {
        const float icon = kIntentGlyphPx * u;
        const float numPx = HudTextPx(hud::kIntentNumberPx, u, EHudText::Free);
        const float plusPx = HudTextPx(hud::kIntentPlusPx, u, EHudText::Free);
        const float pad = kIntentPlatePad * u;
        const float gap = kIntentPlateGap * u;

        char number[16]{};
        const bool hasNumber = badge.valid && badge.attack && badge.expectedDamage > 0;
        if (hasNumber) std::snprintf(number, sizeof number, "%d", badge.expectedDamage);
        char more[8]{};
        const bool hasMore = !info->compact && badge.stacked > 1;  // compact: numbers stay, badges collapse
        if (hasMore) std::snprintf(more, sizeof more, "+%d", badge.stacked - 1);
        const bool showIcon = !info->compact;
        if (!showIcon && !hasNumber) continue;  // compact support/move intent: no number, nothing to draw

        const float numberW = hasNumber ? LabelWidth(numPx, number) : 0.f;
        const float moreW = hasMore ? LabelWidth(plusPx, more) : 0.f;
        const float contentH = std::max({showIcon ? icon : 0.f, numPx, plusPx});
        float contentW = showIcon ? icon : 0.f;
        if (showIcon && (numberW > 0.f || moreW > 0.f)) contentW += gap;
        contentW += numberW;
        if (moreW > 0.f) contentW += (numberW > 0.f ? gap : 0.f) + moreW;

        const glm::vec2 center = view->cellCenter(badge.cell.x, badge.cell.y);
        const int stack = cellStacks[{badge.cell.x, badge.cell.y}]++;
        const float bottom = PlateBottom(registry, *view, badge.cell.x, badge.cell.y) - ui::PlateLiftSteps(stack) * stackStep;
        const ImVec2 min{center.x - (contentW + 2.f * pad) * .5f, bottom - (contentH + 2.f * pad)};
        const ImVec2 max{min.x + contentW + 2.f * pad, bottom};
        draw->ChannelsSetCurrent(kBoardPlateChannel);
        draw->AddRectFilled(min, max, kIntentBack, 3.f * u);
        if (badge.lethal) {
            draw->AddRect(min, max, kIntentLethal, 3.f * u, 0, kIntentLethalBorder * u);
        } else {
            ui::DrawPlateOutline(draw, min, max, 3.f * u, pal::kBronze);
        }
        draw->ChannelsSetCurrent(kBoardCaretChannel);
        ui::DrawPlateCaret(draw, {center.x, bottom}, CellAnchor(registry, *view, badge.cell.x, badge.cell.y), u,
                           badge.lethal ? kLethalText : kIntentNumber);
        draw->ChannelsSetCurrent(kBoardPlateChannel);
        float x = min.x + pad;
        if (showIcon) {
            const ImVec2 imin{x, min.y + pad + (contentH - icon) * .5f};
            const ImVec2 imax{imin.x + icon, imin.y + icon};
            DrawDungeonImage(registry, IntentGlyphSprite(badge.glyph), draw, imin, imax, {0, 0}, {1, 1},
                             badge.valid ? IM_COL32_WHITE : kIntentDim);
            x = imax.x + gap;
        }
        if (hasNumber) {
            Label(draw, {x, min.y + pad + (contentH - numPx) * .5f}, numPx, badge.lethal ? kLethalText : kIntentNumber,
                  number);
            x += numberW;
            if (hasMore) x += gap;
        }
        if (hasMore) {
            Label(draw, {x, min.y + pad + (contentH - plusPx) * .5f}, plusPx, pal::kGoldBright, more);
        }
        if (!badge.tooltip.empty() && ImGui::IsMouseHoveringRect(min, max)) {
            DrawHoverText(draw, badge.tooltip.c_str(), HudTextPx(hud::kHoverTextPx, u, EHudText::Free));
        }
    }
}

void PreviewOverlays(entt::registry& registry, const FUiLayout& layout) {
    const auto* info = FindCombatInfoSnapshot(registry);
    const auto* grid = registry.ctx().find<FGridMetrics>();
    if (info == nullptr || info->preview.empty() || grid == nullptr) return;
    const auto view = ui::FindBattleIsoView(registry, *grid);
    if (!view) return;
    const float u = layout.scale;
    const float size = HudTextPx(hud::kPreviewNumberPx, u, EHudText::Free);
    const float stackStep = HudTextPx(kIntentStackStep, u, EHudText::Free);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    // A preview on a cell that also carries enemy-intent plates sits above them (same stacking rule).
    std::map<std::pair<int, int>, int> intentStacks;
    for (const FIntentBadge& badge : info->intents) ++intentStacks[{badge.cell.x, badge.cell.y}];
    for (const FPreviewBadge& badge : info->preview) {
        if (badge.damage <= 0 && badge.heal <= 0) continue;
        char text[32];
        if (badge.damage > 0 && badge.critDamage > 0) {
            std::snprintf(text, sizeof text, "-%d  %d%% -%d", badge.damage, badge.critChancePercent, badge.critDamage);
        } else if (badge.damage > 0) {
            std::snprintf(text, sizeof text, "-%d", badge.damage);
        } else {
            std::snprintf(text, sizeof text, "+%d", badge.heal);
        }
        const float width = LabelWidth(size, text);
        const glm::vec2 center = view->cellCenter(badge.cell.x, badge.cell.y);
        const auto stack = intentStacks.find({badge.cell.x, badge.cell.y});
        const int lift = ui::PlateLiftSteps(stack != intentStacks.end() ? stack->second : 0);
        const float bottom = PlateBottom(registry, *view, badge.cell.x, badge.cell.y) - lift * stackStep;
        const ImVec2 at{center.x - width * .5f, bottom - size};
        const float pad = kIntentPlatePad * u;
        draw->ChannelsSetCurrent(kBoardPlateChannel);
        DrawPlateBehindText(draw, at, width, size, pad, 3.f * u);
        const ImU32 meaning = badge.damage > 0 ? (badge.lethal ? kLethalText : kIntentNumber) : kHealText;
        ui::DrawPlateOutline(draw, {at.x - pad, at.y - pad}, {at.x + width + pad, at.y + size + pad}, 3.f * u,
                             badge.lethal ? kIntentLethal : pal::kBronze);
        draw->ChannelsSetCurrent(kBoardCaretChannel);
        ui::DrawPlateCaret(draw, {center.x, bottom}, CellAnchor(registry, *view, badge.cell.x, badge.cell.y), u, meaning);
        draw->ChannelsSetCurrent(kBoardPlateChannel);
        Label(draw, at, size, meaning, text);
    }
}

void DamagePopOverlays(entt::registry& registry, const FUiLayout& layout) {
    const auto* effects = registry.ctx().find<FBattleEffects>();
    const auto* grid = registry.ctx().find<FGridMetrics>();
    if (effects == nullptr || effects->pops.empty() || grid == nullptr) return;
    const auto view = ui::FindBattleIsoView(registry, *grid);
    if (!view) return;
    const float u = layout.scale;
    const float size = HudTextPx(hud::kDamagePopPx, u, EHudText::Free);
    const float stackStep = HudTextPx(kIntentStackStep, u, EHudText::Free);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    // F1: a pop on a cell that already carries intent plates / a preview starts above that stack (the same
    // count the plate overlays lift by) and rises from there, so it can never land on the plate below.
    std::map<std::pair<int, int>, int> cellStacks;
    if (const auto* info = FindCombatInfoSnapshot(registry); info != nullptr) {
        for (const FIntentBadge& badge : info->intents) ++cellStacks[{badge.cell.x, badge.cell.y}];
        for (const FPreviewBadge& badge : info->preview) ++cellStacks[{badge.cell.x, badge.cell.y}];
    }
    for (const FDamagePop& pop : effects->pops) {
        const float life = std::max(pop.lifetime, 1e-3f);
        const float phase = std::clamp(pop.age / life, 0.f, 1.f);
        const glm::vec2 center = view->cellCenter(pop.at.x, pop.at.y);
        char text[24];
        const float popSize = pop.crit ? size * 1.35f : size;
        if (pop.crit) std::snprintf(text, sizeof text, "CRIT -%d", pop.damage);
        else std::snprintf(text, sizeof text, "-%d", pop.damage);
        const float width = LabelWidth(popSize, text);
        const int cellX = static_cast<int>(std::floor(pop.at.x));
        const int cellY = static_cast<int>(std::floor(pop.at.y));
        const auto stack = cellStacks.find({cellX, cellY});
        const int lift = ui::PlateLiftSteps(stack != cellStacks.end() ? stack->second : 0);
        const float bottom =
            PlateBottom(registry, *view, cellX, cellY) - lift * stackStep - kDamagePopRise * phase * view->scale;
        const ImVec2 at{center.x - width * .5f, bottom - popSize};
        const auto alpha = static_cast<int>(255.f * (1.f - phase * phase));
        const float pad = kIntentPlatePad * u;
        draw->ChannelsSetCurrent(kBoardPlateChannel);
        DrawPlateBehindText(draw, at, width, popSize, pad, 3.f * u, WithAlpha(kIntentBack, alpha));
        ui::DrawPlateOutline(draw, {at.x - pad, at.y - pad}, {at.x + width + pad, at.y + popSize + pad}, 3.f * u,
                             WithAlpha(pal::kBronze, alpha));
        draw->ChannelsSetCurrent(kBoardCaretChannel);
        ui::DrawPlateCaret(draw, {center.x, bottom}, CellAnchor(registry, *view, cellX, cellY), u,
                           WithAlpha(kDamagePopText, alpha));
        draw->ChannelsSetCurrent(kBoardPlateChannel);
        Label(draw, at, popSize, WithAlpha(kDamagePopText, alpha), text);
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

    TopBar(registry, *layout, run, snapshot);
    // WP-12c/F1: the three board overlays share one two-channel split of the background list — channel 0
    // takes every plate caret, channel 1 every plate/icon/text. Merge is caret-first, so a caret passes
    // beneath any plate (its own stack included) instead of crossing it.
    ImDrawList* board = ImGui::GetBackgroundDrawList();
    board->ChannelsSplit(2);
    board->ChannelsSetCurrent(kBoardCaretChannel);
    IntentOverlays(registry, *layout);
    PreviewOverlays(registry, *layout);
    DamagePopOverlays(registry, *layout);
    board->ChannelsMerge();

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
    Hints(registry, *layout);
    if (playerTurn && EndTurnButton(registry, *layout, queue.pending.empty())) {
        runtime.requests.push_back(FEndTurn{*snapshot.currentTurn});
    }
    ui::EndZone();
}

} // namespace game::ecs
