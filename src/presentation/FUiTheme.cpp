#include "presentation/FUiTheme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <SDL3/SDL.h>

namespace game::ui {
namespace {

// WP-24b info-cue checklist. The order and ids are the acceptance table's ㉠…㉥ rows; the FollowUp rows
// are the gaps the audit keeps visible instead of pretending they are covered.
constexpr std::array<FInfoCueRow, 6> kInfoCueChecklist{{
    {"world.faction", "Unit faction in the world",
     "filled square (party) / upward triangle (enemy) badge at the HP bar's left end above the head; the "
     "faction-coloured feet ring and trim stay secondary",
     EInfoCueStatus::Covered},
    {"board.ranges", "Move and hit range cells on the board",
     "filled-cell colour only; the ? tooltip names cyan/red (gap - follow-up: outline style, out of scope)",
     EInfoCueStatus::FollowUp},
    {"hud.faction", "Unit faction in the HUD",
     "unit name and attack/support wording; faction trim is secondary", EInfoCueStatus::Covered},
    {"hud.action_points", "Action-point gems and skill slot gems",
     "M / S letters, filled vs hollow gem count, AP numbers on the slots", EInfoCueStatus::Covered},
    {"result.banner", "Battle result banner and journal",
     "text: VICTORY / DEFEAT, room label and log lines", EInfoCueStatus::Covered},
    {"unit.statuses", "Statuses on a unit",
     "count/duration/dot name are text; per-status kind gems are colour-only -> follow-up: glyph/letter per "
     "kind in the HUD view, not this lane",
     EInfoCueStatus::FollowUp},
}};

struct FFontSet {
    ImGuiContext* context{nullptr};
    ImFont* heading{nullptr};
};
FFontSet g_fonts;
FSkin g_skin;

std::filesystem::path FindFont(const char* file) {
    std::error_code error;
    if (const char* base = SDL_GetBasePath()) {
        const auto path = std::filesystem::path(base) / "assets" / "fonts" / file;
        if (std::filesystem::exists(path, error)) return path;
    }
    const auto local = std::filesystem::path("assets") / "fonts" / file;
    if (std::filesystem::exists(local, error)) return local;
    return {};
}

ImVec2 Offset(ImVec2 point, float dx, float dy) { return {point.x + dx, point.y + dy}; }

void Corner(ImDrawList* draw, ImVec2 center) {
    draw->AddRectFilled(Offset(center, -4, -4), Offset(center, 4, 4), Fade(palette::kOutline));
    draw->AddRectFilled(Offset(center, -3, -3), Offset(center, 3, 3), Fade(palette::kGold));
    draw->AddRectFilled(Offset(center, -1, -1), Offset(center, 1, 1), Fade(palette::kOutline));
}

void Diamond(ImDrawList* draw, ImVec2 center, float radius, ImU32 color) {
    draw->AddQuadFilled(Offset(center, 0, -radius), Offset(center, radius, 0), Offset(center, 0, radius),
                        Offset(center, -radius, 0), Fade(color));
}

} // namespace

std::span<const FInfoCueRow> InfoCueChecklist() { return kInfoCueChecklist; }

EFactionMarker FactionMarkerFor(bool party) noexcept {
    return party ? EFactionMarker::Square : EFactionMarker::Triangle;
}

FMarkerOutline FactionMarkerOutline(EFactionMarker marker, float cx, float cy, float rx, float ry) noexcept {
    FMarkerOutline outline;
    // Screen-axis shapes (the filled badge next to the HP bar): a party square and an upward enemy triangle.
    // The feet keep the faction ring — an iso diamond would read as a highlighted cell, not as a faction cue.
    if (marker == EFactionMarker::Square) {
        outline.points = {{{cx - rx, cy - ry}, {cx + rx, cy - ry}, {cx + rx, cy + ry}, {cx - rx, cy + ry}}};
        outline.count = 4;
        return outline;
    }
    outline.points = {{{cx, cy - ry}, {cx + rx, cy + ry}, {cx - rx, cy + ry}}};
    outline.count = 3;
    return outline;
}

void ApplyTextScale(float scale) {
    ImGui::GetStyle().FontScaleMain = scale > 0.f ? scale : 1.f;
}

void InstallSkin(const FSkin& skin) { g_skin = skin; }

void DrawNineSlice(ImDrawList* draw, const FSkinPiece& piece, ImVec2 min, ImVec2 max, float corner, ImU32 tint) {
    if (!draw || !piece.valid()) return;
    tint = Fade(tint);
    // Keep corners from overlapping when the target is smaller than two corners.
    const float cx = std::min(corner, std::floor((max.x - min.x) * .5f));
    const float cy = std::min(corner, std::floor((max.y - min.y) * .5f));
    const float xs[4]{min.x, min.x + cx, max.x - cx, max.x};
    const float ys[4]{min.y, min.y + cy, max.y - cy, max.y};
    const float us[4]{0.f, cx / piece.size.x, 1.f - cx / piece.size.x, 1.f};
    const float vs[4]{0.f, cy / piece.size.y, 1.f - cy / piece.size.y, 1.f};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            draw->AddImage(piece.texture, {xs[col], ys[row]}, {xs[col + 1], ys[row + 1]}, {us[col], vs[row]},
                           {us[col + 1], vs[row + 1]}, tint);
        }
    }
}

ImU32 Fade(ImU32 color) {
    const float alpha = ImGui::GetStyle().Alpha;
    const auto a = static_cast<ImU32>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImFont* HeadingFont() {
    return g_fonts.context == ImGui::GetCurrentContext() ? g_fonts.heading : nullptr;
}

void ApplyTheme() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    g_fonts = {ImGui::GetCurrentContext(), nullptr};
    io.FontGlobalScale = 1.2f;
    const auto body = FindFont("Alegreya.ttf");
    const auto heading = FindFont("Cinzel.ttf");
    // Dynamic font atlases need a renderer backend; headless contexts (tests) keep the built-in font.
    const bool canLoadFonts = (io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
    if (canLoadFonts && !body.empty() && !heading.empty()) {
        if (ImFont* bodyFont = io.Fonts->AddFontFromFileTTF(body.string().c_str(), 17.f)) {
            io.FontDefault = bodyFont;
            g_fonts.heading = io.Fonts->AddFontFromFileTTF(heading.string().c_str(), 20.f);
            io.FontGlobalScale = 1.f;
        }
    }

    style.WindowRounding = 0.f;
    style.PopupRounding = 0.f;
    style.FrameRounding = 1.f;
    style.GrabRounding = 1.f;
    style.ChildRounding = 0.f;
    style.WindowBorderSize = 0.f;
    style.PopupBorderSize = 0.f;
    style.FrameBorderSize = 0.f;
    style.WindowPadding = {18.f, 14.f};
    style.FramePadding = {10.f, 6.f};
    style.ItemSpacing = {8.f, 7.f};
    style.SeparatorTextBorderSize = 1.f;
    style.ScrollbarSize = 10.f;
    style.ScrollbarRounding = 0.f;

    auto* c = style.Colors;
    c[ImGuiCol_Text] = palette::kTextV;
    c[ImGuiCol_TextDisabled] = palette::kMutedV;
    c[ImGuiCol_WindowBg] = {0.055f, 0.042f, 0.045f, 0.98f};
    c[ImGuiCol_PopupBg] = {0.065f, 0.048f, 0.05f, 0.99f};
    c[ImGuiCol_ChildBg] = {0.f, 0.f, 0.f, 0.f};
    c[ImGuiCol_Border] = {0.44f, 0.32f, 0.16f, 1.f};
    c[ImGuiCol_ModalWindowDimBg] = {0.01f, 0.008f, 0.012f, 0.72f};
    c[ImGuiCol_Button] = {0.20f, 0.095f, 0.085f, 1.f};
    c[ImGuiCol_ButtonHovered] = {0.34f, 0.15f, 0.11f, 1.f};
    c[ImGuiCol_ButtonActive] = {0.48f, 0.21f, 0.12f, 1.f};
    c[ImGuiCol_Header] = {0.25f, 0.11f, 0.09f, 1.f};
    c[ImGuiCol_HeaderHovered] = {0.36f, 0.16f, 0.11f, 1.f};
    c[ImGuiCol_HeaderActive] = {0.48f, 0.21f, 0.12f, 1.f};
    c[ImGuiCol_FrameBg] = {0.09f, 0.065f, 0.065f, 1.f};
    c[ImGuiCol_FrameBgHovered] = {0.15f, 0.10f, 0.09f, 1.f};
    c[ImGuiCol_FrameBgActive] = {0.20f, 0.12f, 0.10f, 1.f};
    c[ImGuiCol_Separator] = {0.44f, 0.32f, 0.16f, 0.7f};
    c[ImGuiCol_PlotHistogram] = {0.72f, 0.15f, 0.14f, 1.f};
    c[ImGuiCol_ScrollbarBg] = {0.f, 0.f, 0.f, 0.25f};
    c[ImGuiCol_ScrollbarGrab] = {0.44f, 0.32f, 0.16f, 1.f};
    c[ImGuiCol_TitleBg] = {0.12f, 0.07f, 0.06f, 1.f};
    c[ImGuiCol_TitleBgActive] = {0.2f, 0.1f, 0.07f, 1.f};
    c[ImGuiCol_CheckMark] = palette::kGoldV;
    // WP-24b compendium tabs: the selected tab is a raised, gold-overline panel — shape/position, not just
    // colour, marks which tab is open.
    c[ImGuiCol_Tab] = {0.14f, 0.085f, 0.075f, 1.f};
    c[ImGuiCol_TabHovered] = {0.34f, 0.15f, 0.11f, 1.f};
    c[ImGuiCol_TabSelected] = {0.30f, 0.14f, 0.10f, 1.f};
    c[ImGuiCol_TabSelectedOverline] = palette::kGoldV;
    c[ImGuiCol_TabDimmed] = {0.11f, 0.07f, 0.06f, 1.f};
    c[ImGuiCol_TabDimmedSelected] = {0.22f, 0.11f, 0.09f, 1.f};
    c[ImGuiCol_TabDimmedSelectedOverline] = palette::kGoldV;
}

void DrawPanel(ImDrawList* draw, ImVec2 min, ImVec2 max, bool fill) {
    draw->PushClipRectFullScreen();
    if (fill && g_skin.panel.valid()) {
        DrawNineSlice(draw, g_skin.panel, min, max, 16.f);
        draw->PopClipRect();
        return;
    }
    if (fill) {
        draw->AddRectFilledMultiColor(min, max, Fade(palette::kPanelTop), Fade(palette::kPanelTop),
                                      Fade(palette::kPanelBottom), Fade(palette::kPanelBottom));
        draw->AddRectFilledMultiColor(Offset(min, 4, 4), Offset(max, -4, -4), IM_COL32(255, 235, 200, 6),
                                      IM_COL32(255, 235, 200, 6), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    }
    draw->AddRect(Offset(min, -1, -1), Offset(max, 1, 1), Fade(palette::kOutline), 0.f, 0, 2.f);
    draw->AddRect(min, max, Fade(palette::kBronze), 0.f, 0, 2.f);
    draw->AddRect(Offset(min, 4, 4), Offset(max, -4, -4), Fade(IM_COL32(201, 160, 82, 70)), 0.f, 0, 1.f);
    Corner(draw, min);
    Corner(draw, {max.x, min.y});
    Corner(draw, {min.x, max.y});
    Corner(draw, max);
    const float midX = (min.x + max.x) * 0.5f;
    Diamond(draw, {midX, min.y}, 5.f, palette::kOutline);
    Diamond(draw, {midX, min.y}, 4.f, palette::kGold);
    Diamond(draw, {midX, max.y}, 5.f, palette::kOutline);
    Diamond(draw, {midX, max.y}, 4.f, palette::kGold);
    draw->PopClipRect();
}

void DrawLifeBar(ImDrawList* draw, ImVec2 min, ImVec2 max, float fraction) {
    fraction = std::clamp(fraction, 0.f, 1.f);
    draw->AddRectFilled(Offset(min, -1, -1), Offset(max, 1, 1), Fade(palette::kOutline));
    draw->AddRectFilled(min, max, Fade(IM_COL32(26, 7, 8, 255)));
    if (fraction > 0.f) {
        const ImVec2 edge{min.x + (max.x - min.x) * fraction, max.y};
        draw->AddRectFilledMultiColor(min, edge, Fade(palette::kBloodTop), Fade(palette::kBloodTop),
                                      Fade(palette::kBloodBottom), Fade(palette::kBloodBottom));
        draw->AddLine(Offset(min, 0, 1), {edge.x, min.y + 1}, Fade(IM_COL32(255, 150, 130, 110)));
    }
    draw->AddRect(min, max, Fade(palette::kBronze));
}

void DrawGem(ImDrawList* draw, ImVec2 center, float radius, ImU32 color, bool filled) {
    Diamond(draw, center, radius + 1.5f, palette::kOutline);
    if (filled) {
        Diamond(draw, center, radius, color);
        Diamond(draw, Offset(center, -radius * 0.2f, -radius * 0.25f), radius * 0.35f, IM_COL32(255, 255, 255, 120));
    } else {
        Diamond(draw, center, radius, IM_COL32(24, 20, 20, 255));
    }
}

void DrawSlotFrame(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 trim, bool glow) {
    if (g_skin.slot.valid()) {
        if (glow) {
            draw->AddRectFilled(Offset(min, -4, -4), Offset(max, 4, 4), Fade(IM_COL32(240, 206, 128, 46)), 3.f);
            draw->AddRect(Offset(min, -2, -2), Offset(max, 2, 2), Fade(IM_COL32(240, 206, 128, 200)), 3.f, 0, 2.f);
        }
        // The slot art is bronze already; only faction colors (party blue / enemy red) tint it.
        const bool plain = trim == palette::kBronze || trim == palette::kGold || trim == palette::kGoldBright;
        DrawNineSlice(draw, g_skin.slot, min, max, 12.f, plain ? IM_COL32_WHITE : trim);
        return;
    }
    draw->AddRectFilled(min, max, Fade(IM_COL32(10, 7, 8, 255)));
    if (glow) {
        draw->AddRectFilled(Offset(min, -3, -3), Offset(max, 3, 3), Fade(IM_COL32(240, 206, 128, 40)));
    }
    draw->AddRect(Offset(min, -1, -1), Offset(max, 1, 1), Fade(palette::kOutline), 0.f, 0, 1.f);
    draw->AddRect(min, max, Fade(trim), 0.f, 0, 2.f);
    draw->AddRect(Offset(min, 3, 3), Offset(max, -3, -3), Fade(IM_COL32(0, 0, 0, 160)));
}

bool BeginPanel(const char* name, ImVec2 pos, ImVec2 size, ImGuiWindowFlags extraFlags) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowScroll({0, 0});
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | extraFlags;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22.f, 18.f});
    const bool open = ImGui::Begin(name, nullptr, flags);
    if (open) DrawPanel(ImGui::GetWindowDrawList(), pos, {pos.x + size.x, pos.y + size.y});
    return open;
}

void EndPanel() {
    ImGui::End();
    ImGui::PopStyleVar();
}

bool BeginZone(const char* name, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowScroll({0, 0});
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    return ImGui::Begin(name, nullptr, flags);
}

void EndZone() {
    ImGui::End();
    ImGui::PopStyleVar();
}

void DrawRecess(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    draw->AddRectFilled(min, max, Fade(IM_COL32(6, 4, 5, 150)));
    draw->AddRect(min, max, Fade(IM_COL32(4, 2, 3, 210)));
    draw->AddLine({min.x, max.y + 1.f}, {max.x, max.y + 1.f}, Fade(IM_COL32(201, 160, 82, 40)));
}

FBodyFont::FBodyFont(float size) : pushed_(HeadingFont() != nullptr) {
    if (pushed_) ImGui::PushFont(nullptr, size);
}

FBodyFont::~FBodyFont() {
    if (pushed_) ImGui::PopFont();
}

void DecorateCurrentWindow() {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    DrawPanel(ImGui::GetWindowDrawList(), pos, {pos.x + size.x, pos.y + size.y}, false);
}

void HeadingText(const char* text, float size, ImVec4 color) {
    ImFont* font = HeadingFont();
    if (font) ImGui::PushFont(font, size);
    ImGui::TextColored(color, "%s", text);
    if (font) ImGui::PopFont();
}

void SectionTitle(const char* text, float size) {
    ImFont* font = HeadingFont();
    if (font) ImGui::PushFont(font, size);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float textX = origin.x + (width - textSize.x) * 0.5f;
    const float lineY = std::floor(origin.y + textSize.y * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 dim = Fade(IM_COL32(201, 160, 82, 120));
    draw->AddLine({origin.x, lineY}, {textX - 16.f, lineY}, dim);
    draw->AddLine({origin.x + 10.f, lineY + 3.f}, {textX - 16.f, lineY + 3.f}, Fade(IM_COL32(112, 82, 40, 120)));
    draw->AddLine({textX + textSize.x + 16.f, lineY}, {origin.x + width, lineY}, dim);
    draw->AddLine({textX + textSize.x + 16.f, lineY + 3.f}, {origin.x + width - 10.f, lineY + 3.f},
                  Fade(IM_COL32(112, 82, 40, 120)));
    Diamond(draw, {textX - 9.f, lineY}, 3.f, palette::kGold);
    Diamond(draw, {textX + textSize.x + 9.f, lineY}, 3.f, palette::kGold);
    ImGui::SetCursorScreenPos({textX, origin.y});
    ImGui::TextColored(palette::kGoldV, "%s", text);
    if (font) ImGui::PopFont();
    ImGui::Dummy({0.f, 2.f});
}

bool Button(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette::kGoldBrightV);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool hot = ImGui::IsItemHovered();
    // The plaque's corner rivets swallow the label on small buttons, which keep the procedural trim.
    if (g_skin.plaque.valid() && max.y - min.y >= 40.f && max.x - min.x >= 80.f) {
        DrawNineSlice(draw, g_skin.plaque, min, max, 12.f, hot ? IM_COL32(255, 236, 190, 255) : IM_COL32_WHITE);
        return pressed;
    }
    draw->AddRect(Offset(min, -1, -1), Offset(max, 1, 1), Fade(palette::kOutline));
    draw->AddRect(min, max, Fade(hot ? palette::kGold : palette::kBronze), 0.f, 0, 2.f);
    draw->AddLine(Offset(min, 3, 3), {max.x - 3.f, min.y + 3.f}, Fade(IM_COL32(255, 220, 160, hot ? 90 : 45)));
    draw->AddLine({min.x + 3.f, max.y - 3.f}, Offset(max, -3, -3), Fade(IM_COL32(0, 0, 0, 120)));
    return pressed;
}

} // namespace game::ui
