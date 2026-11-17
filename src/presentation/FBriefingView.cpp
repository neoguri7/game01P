#include "presentation/FBriefingView.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>

#include "core/EngineState.h"
#include "presentation/FFlowArt.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

using ui::FRect;

constexpr const char* kMiddleDot = "\xC2\xB7";

[[nodiscard]] ImVec2 V(float x, float y) { return ImVec2{x, y}; }

/// Text placed at an absolute screen position in the body font at `size`; wrapped when `wrap` > 0.
void PutBody(float size, float x, float y, ImVec4 colour, const std::string& text, float wrap = 0.f)
{
    ui::FBodyFont scope(size);
    ImGui::SetCursorScreenPos(V(x, y));
    if (wrap > 0.f) ImGui::PushTextWrapPos(x + wrap);
    ImGui::TextColored(colour, "%s", text.c_str());
    if (wrap > 0.f) ImGui::PopTextWrapPos();
}

[[nodiscard]] float BodyWidth(float size, const std::string& text)
{
    ui::FBodyFont scope(size);
    return ImGui::CalcTextSize(text.c_str()).x;
}

/// Clips everything drawn while alive to `band` (ImGui text and draw-list calls alike).
class FBandClip {
public:
    explicit FBandClip(const FRect& band)
    {
        ImGui::PushClipRect(V(band.x, band.y), V(band.right(), band.bottom()), true);
    }
    ~FBandClip() { ImGui::PopClipRect(); }
    FBandClip(const FBandClip&) = delete;
    FBandClip& operator=(const FBandClip&) = delete;
};

/// A framed square portrait. The picture is the left square of its strip; a missing picture leaves the frame.
void DrawPortrait(entt::registry& registry, ImDrawList* draw, const std::string& sprite, float x, float y, float size,
                  ImU32 trim)
{
    ui::DrawSlotFrame(draw, V(x, y), V(x + size, y + size), trim, false);
    const ui::FSkinPiece piece = FlowPicture(registry, sprite);
    if (!piece.valid()) return;
    const float uvRight = std::min(1.f, piece.size.y / piece.size.x);
    draw->AddImage(piece.texture, V(x + 2.f, y + 2.f), V(x + size - 2.f, y + size - 2.f), V(0.f, 0.f), V(uvRight, 1.f));
}

void DrawIcon(entt::registry& registry, ImDrawList* draw, EBriefingNodeKind kind, float x, float y, float size)
{
    const ui::FSkinPiece piece = FlowPicture(registry, NodeIconId(kind));
    if (piece.valid()) {
        draw->AddImage(piece.texture, V(x, y), V(x + size, y + size), V(0.f, 0.f), V(1.f, 1.f));
    } else {
        ui::DrawRecess(draw, V(x, y), V(x + size, y + size));
    }
}

void DrawHeader(entt::registry& registry, ImDrawList* draw, const FBriefingLayout& l, const FRunBriefing& b)
{
    const float u = l.u;
    const float t = l.t;
    {
        FBandClip clip(l.banner);
        const ui::FSkinPiece banner = FlowPicture(registry, ThemeBannerId(b.themeId));
        const ImVec2 min = V(l.banner.x, l.banner.y);
        const ImVec2 max = V(l.banner.right(), l.banner.bottom());
        if (banner.valid()) {
            draw->AddImage(banner.texture, min, max, V(0.f, 0.f), V(1.f, 1.f));
        } else {
            ui::DrawRecess(draw, min, max);
        }
    }
    {
        FBandClip clip(l.headerText);
        ImGui::SetCursorScreenPos(V(l.headerText.x, l.headerText.y));
        ui::HeadingText(b.themeName.c_str(), 16.f * u, ui::palette::kMutedV);
        ImGui::SetCursorScreenPos(V(l.headerText.x, l.headerText.y + 20.f * t));
        ui::HeadingText(b.dungeonName.c_str(), 30.f * u, ui::palette::kGoldBrightV);
        PutBody(13.f * u, l.headerText.x, l.headerText.y + 56.f * t, ui::palette::kTextV, b.infoLine);
    }
    {
        FBandClip clip(l.expedition);
        const float cw = (l.expedition.w - 24.f * u) / 4.f;
        const std::size_t count = std::min<std::size_t>(4, b.expedition.size());
        for (std::size_t i = 0; i < count; ++i) {
            const FBriefingDungeon& d = b.expedition[i];
            const float cx = l.expedition.x + static_cast<float>(i) * (cw + 8.f * u);
            const float cy = l.expedition.y;
            ui::DrawSlotFrame(draw, V(cx, cy), V(cx + cw, cy + 50.f * t),
                              d.current ? ui::palette::kGoldBright : ui::palette::kBronze, d.current);
            PutBody(12.f * u, cx + 6.f * u, cy + 4.f * t, d.cleared ? ui::palette::kMutedV : ui::palette::kTextV, d.name,
                    cw - 12.f * u);
            std::string status;
            ImVec4 colour = ui::palette::kEnemyV;
            if (d.current) {
                status = "You are here";
                if (!d.bossMark.empty()) status += std::string("  ") + kMiddleDot + "  " + d.bossMark;
                colour = ui::palette::kGoldBrightV;
            } else if (d.cleared) {
                status = "Cleared";
                colour = ui::palette::kMutedV;
            } else {
                status = d.bossMark;
            }
            if (!status.empty()) PutBody(12.f * u, cx + 6.f * u, cy + 34.f * t, colour, status);
        }
    }
}

void DrawObjective(const FBriefingLayout& l, const FRunBriefing& b)
{
    FBandClip clip(l.objective);
    const float t = l.t;
    ImGui::SetCursorScreenPos(V(l.objective.x, l.objective.y));
    ui::HeadingText(b.objective.c_str(), 18.f * l.u, ui::palette::kGoldV);
    PutBody(13.f * l.u, l.objective.x, l.objective.y + 24.f * t, ui::palette::kTextV, b.danger);
    if (!b.eliteLine.empty()) {
        PutBody(13.f * l.u, l.objective.x, l.objective.y + 42.f * t, ui::palette::kGoldBrightV, b.eliteLine);
    }
    if (!b.eliteStakes.empty()) {
        PutBody(13.f * l.u, l.objective.x, l.objective.y + 60.f * t, ui::palette::kMutedV, b.eliteStakes);
    }
}

void DrawRoute(entt::registry& registry, ImDrawList* draw, const FBriefingLayout& l, const FRunBriefing& b)
{
    FBandClip clip(l.route);
    const float u = l.u;
    const float t = l.t;
    const float k = static_cast<float>(l.k);
    ImGui::SetCursorScreenPos(V(l.route.x, l.route.y));
    ui::SectionTitle("ROUTE", 16.f * u);
    if (b.route.empty()) return;
    const float cw = l.route.w / static_cast<float>(b.route.size());
    const float iconY = l.route.y + 22.f * t;
    float previousRight = 0.f;
    for (std::size_t i = 0; i < b.route.size(); ++i) {
        const FBriefingNode& node = b.route[i];
        const float cx = l.route.x + static_cast<float>(i) * cw;
        const bool fork = node.altKind.has_value();
        const float groupW = fork ? 100.f * k : 48.f * k;
        const float left = cx + (cw - groupW) / 2.f;
        const float right = left + groupW;
        if (node.kind == EBriefingNodeKind::Entrance) {
            ui::DrawSlotFrame(draw, V(left - 2.f * u, iconY - 2.f * u), V(left + 48.f * k + 2.f * u, iconY + 48.f * k + 2.f * u),
                              ui::palette::kGoldBright, true);
        }
        DrawIcon(registry, draw, node.kind, left, iconY, 48.f * k);
        if (fork) DrawIcon(registry, draw, *node.altKind, left + 52.f * k, iconY, 48.f * k);
        if (i > 0) {
            const float y = iconY + 24.f * k;
            draw->AddLine(V(previousRight + 4.f * u, y), V(left - 4.f * u, y), ui::palette::kBronze, 2.f * u);
        }
        previousRight = right;
        const std::string label = fork ? node.label + " or " + node.altLabel : node.label;
        const float labelY = iconY + 48.f * k + 4.f * u;
        const float width = BodyWidth(12.f * u, label);
        if (width <= cw - 8.f * u) {
            PutBody(12.f * u, cx + (cw - width) / 2.f, labelY, ui::palette::kTextV, label);
        } else {
            PutBody(12.f * u, cx + 4.f * u, labelY, ui::palette::kTextV, label, cw - 8.f * u);
        }
    }
}

void DrawThreats(entt::registry& registry, ImDrawList* draw, const FBriefingLayout& l, const FRunBriefing& b)
{
    FBandClip clip(l.threats);
    const float u = l.u;
    const float t = l.t;
    const float k = static_cast<float>(l.k);
    const float bx = l.threats.x;
    const float by = l.threats.y;
    ImGui::SetCursorScreenPos(V(bx, by));
    ui::SectionTitle("THREATS", 16.f * u);
    const float bossRow = std::max(64.f * k, 62.f * t);
    if (b.boss.has_value()) {
        const FBriefingBoss& boss = *b.boss;
        DrawPortrait(registry, draw, boss.sprite, bx, by + 22.f * t, 64.f * k, ui::palette::kGoldBright);
        const float tx = bx + 64.f * k + 12.f * u;
        ImGui::SetCursorScreenPos(V(tx, by + 22.f * t));
        ui::HeadingText(boss.name.c_str(), 18.f * u, ui::palette::kGoldBrightV);
        {
            ui::FBodyFont scope(12.f * u);
            ImGui::SameLine(0.f, 8.f * u);
            ImGui::TextColored(ui::palette::kEnemyV, "%s", boss.mark.c_str());
        }
        if (!boss.note.empty()) PutBody(13.f * u, tx, by + 22.f * t + 24.f * t, ui::palette::kTextV, boss.note);
        if (!boss.gateLine.empty()) PutBody(13.f * u, tx, by + 22.f * t + 42.f * t, ui::palette::kTextV, boss.gateLine);
    }
    const float ty = by + 22.f * t + bossRow + 8.f * u;
    if (b.threats.empty()) {
        PutBody(13.f * u, bx, ty, ui::palette::kMutedV, "No scouting report for this dungeon.");
        return;
    }
    const int tiles = std::max(1, static_cast<int>(std::floor(l.threats.w / (76.f * u))));
    const int count = static_cast<int>(b.threats.size());
    const int shown = count > tiles ? tiles - 1 : count;
    for (int i = 0; i < shown; ++i) {
        const FBriefingUnit& unit = b.threats[static_cast<std::size_t>(i)];
        const float tx = bx + static_cast<float>(i) * 76.f * u;
        DrawPortrait(registry, draw, unit.sprite, tx + (76.f * u - 64.f * k) / 2.f, ty, 64.f * k, ui::palette::kBronze);
        const float width = BodyWidth(12.f * u, unit.name);
        const float nameY = ty + 64.f * k + 2.f * u;
        if (width <= 72.f * u) {
            PutBody(12.f * u, tx + (76.f * u - width) / 2.f, nameY, ui::palette::kTextV, unit.name);
        } else {
            PutBody(12.f * u, tx + 2.f * u, nameY, ui::palette::kTextV, unit.name, 72.f * u);
        }
    }
    if (count > tiles) {
        const float tx = bx + static_cast<float>(tiles - 1) * 76.f * u;
        PutBody(13.f * u, tx, ty + 24.f * k, ui::palette::kMutedV, "+" + std::to_string(count - tiles + 1) + " more");
    }
}

void DrawParty(entt::registry& registry, ImDrawList* draw, const FBriefingLayout& l, const FRunBriefing& b)
{
    FBandClip clip(l.party);
    const float u = l.u;
    const float t = l.t;
    const float k = static_cast<float>(l.k);
    ImGui::SetCursorScreenPos(V(l.party.x, l.party.y));
    ui::SectionTitle("PARTY", 16.f * u);
    const float cw = (l.party.w - 24.f * u) / 4.f;
    const std::size_t count = std::min<std::size_t>(4, b.party.size());
    for (std::size_t i = 0; i < count; ++i) {
        const FBriefingMember& m = b.party[i];
        const float cx = l.party.x + static_cast<float>(i) * (cw + 8.f * u);
        const float cy = l.party.y + 22.f * t;
        ImGui::PushClipRect(V(cx, cy), V(cx + cw, l.party.bottom()), true);
        DrawPortrait(registry, draw, m.sprite, cx + (cw - 64.f * k) / 2.f, cy, 64.f * k, ui::palette::kBronze);
        const float textY = cy + 64.f * k + 4.f * u;
        PutBody(13.f * u, cx + 4.f * u, textY, ui::palette::kTextV, m.name);
        std::string role = "Lv " + std::to_string(m.level);
        if (!m.role.empty() && m.name.find(m.role) == std::string::npos) role = m.role + "  " + role;
        PutBody(12.f * u, cx + 4.f * u, textY + 15.f * t, ui::palette::kMutedV, role);
        PutBody(12.f * u, cx + 4.f * u, textY + 30.f * t, ui::palette::kTextV,
                "HP " + std::to_string(m.health) + "/" + std::to_string(m.maxHealth));
        const float barY = textY + 45.f * t;
        ui::DrawLifeBar(draw, V(cx + 4.f * u, barY), V(cx + cw - 4.f * u, barY + 6.f * u),
                        static_cast<float>(m.health) / static_cast<float>(std::max(1, m.maxHealth)));
        ImGui::PopClipRect();
    }
}

} // namespace

FBriefingLayout ComputeBriefingLayout(float windowWidth, float windowHeight, float fontScale)
{
    FBriefingLayout l;
    const float u = std::min(windowHeight / 800.f, windowWidth / 1160.f);
    const int k = std::max(1, static_cast<int>(std::floor(u)));
    const float kf = static_cast<float>(k);
    const float t = u * fontScale;
    const float g = 8.f * u;
    l.u = u;
    l.k = k;
    l.t = t;

    const float h0 = std::max(160.f * kf, 142.f * t);
    const float objectiveH = 78.f * t;
    const float routeH = 22.f * t + 48.f * kf + 34.f * t;
    const float bossRow = std::max(64.f * kf, 62.f * t);
    const float lowerH = 22.f * t + bossRow + 8.f * u + 64.f * kf + 30.f * t;
    const float rewardH = 20.f * t;
    const float buttonH = 40.f * t;
    const float content = h0 + objectiveH + routeH + lowerH + rewardH + buttonH + 5.f * g;

    l.panel.w = std::min(windowWidth - 48.f, 1100.f * u);
    l.panel.h = content + 36.f;
    l.panel.x = std::floor((windowWidth - l.panel.w) / 2.f);
    l.panel.y = std::floor((windowHeight - l.panel.h) / 2.f);
    const float x0 = l.panel.x + 22.f;
    const float y0 = l.panel.y + 18.f;
    const float w = l.panel.w - 44.f;

    l.banner = {x0, y0, 384.f * kf, 160.f * kf};
    l.headerText = {x0 + 384.f * kf + 16.f * u, y0, w - 384.f * kf - 16.f * u, h0 - 50.f * t - g};
    l.expedition = {l.headerText.x, y0 + h0 - 50.f * t, l.headerText.w, 50.f * t};

    float y = y0 + h0 + g;
    l.objective = {x0, y, w, objectiveH};
    y = l.objective.bottom() + g;
    l.route = {x0, y, w, routeH};
    y = l.route.bottom() + g;
    l.threats = {x0, y, std::floor(w * 0.6f), lowerH};
    l.party = {x0 + l.threats.w + 16.f * u, y, w - l.threats.w - 16.f * u, lowerH};
    y = l.threats.bottom() + g;
    l.reward = {x0, y, w, rewardH};
    y = l.reward.bottom() + g;
    l.button = {x0 + (w - 360.f * u) / 2.f, y, 360.f * u, buttonH};
    return l;
}

void DrawBriefingScreen(entt::registry& registry, const FRunBriefing& briefing)
{
    const auto* window = registry.ctx().find<FEngineWindowState>();
    if (window == nullptr) return;
    const float fontScale = ImGui::GetStyle().FontScaleMain > 0.f ? ImGui::GetStyle().FontScaleMain : 1.f;
    const FBriefingLayout l =
        ComputeBriefingLayout(static_cast<float>(window->width), static_cast<float>(window->height), fontScale);

    if (!ui::BeginPanel("##briefing", V(l.panel.x, l.panel.y), V(l.panel.w, l.panel.h),
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ui::EndPanel();
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    DrawHeader(registry, draw, l, briefing);
    DrawObjective(l, briefing);
    DrawRoute(registry, draw, l, briefing);
    DrawThreats(registry, draw, l, briefing);
    DrawParty(registry, draw, l, briefing);
    {
        FBandClip clip(l.reward);
        const float width = BodyWidth(13.f * l.u, briefing.rewardLine);
        PutBody(13.f * l.u, l.reward.x + std::max(0.f, (l.reward.w - width) / 2.f), l.reward.y, ui::palette::kMutedV,
                briefing.rewardLine);
    }
    ImGui::SetCursorScreenPos(V(l.button.x, l.button.y));
    if (ui::Button(briefing.enterLabel.c_str(), V(l.button.w, l.button.h))) {
        registry.ctx().insert_or_assign<FBriefingCommand>(FBriefingCommand{EBriefingCommand::Enter});
    }
    ui::EndPanel();
}

} // namespace game
