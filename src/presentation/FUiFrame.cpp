#include "presentation/FUiFrame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include <imgui.h>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"

namespace game::ui {
namespace {

constexpr ImU32 kTrim = IM_COL32(112, 82, 40, 255);
constexpr ImU32 kTrimShadow = IM_COL32(4, 2, 3, 255);

ImVec2 Min(const FRect& rect) { return {rect.x, rect.y}; }
ImVec2 Max(const FRect& rect) { return {rect.right(), rect.bottom()}; }

// The frame image cut into nine pieces. The corner blocks keep their size and the strips between them
// stretch; the middle piece is the viewport and is left out, so the world shows through it.
void DrawFrameArt(ImDrawList* draw, const FSkinPiece& art, const FUiLayoutSpec& spec, const FUiLayout& layout) {
    const float scale = layout.scale;
    const float width = layout.screen.w;
    const float height = layout.screen.h;
    const float xs[4]{0.f, std::round(spec.slice.left * scale), std::round(width - spec.slice.right * scale), width};
    const float ys[4]{0.f, std::round(spec.slice.top * scale), std::round(height - spec.slice.bottom * scale), height};
    // Slices are authored in design pixels; the art may be drawn at any resolution, so convert to texture UVs.
    const float us[4]{0.f, spec.slice.left / spec.designWidth, 1.f - spec.slice.right / spec.designWidth, 1.f};
    const float vs[4]{0.f, spec.slice.top / spec.designHeight, 1.f - spec.slice.bottom / spec.designHeight, 1.f};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (row == 1 && col == 1) continue;
            draw->AddImage(art.texture, {xs[col], ys[row]}, {xs[col + 1], ys[row + 1]}, {us[col], vs[row]},
                           {us[col + 1], vs[row + 1]});
        }
    }
}

// Without frame art the four bands are stone panels; trim along the viewport edge ties them together.
void DrawBands(ImDrawList* draw, const FUiLayout& layout) {
    DrawPanel(draw, Min(layout.leftRail), Max(layout.leftRail));
    DrawPanel(draw, Min(layout.rightRail), Max(layout.rightRail));
    DrawPanel(draw, Min(layout.topBar), Max(layout.topBar));
    DrawPanel(draw, Min(layout.dock), Max(layout.dock));
    const FRect& view = layout.viewport;
    draw->AddRect({view.x - 1.f, view.y - 1.f}, {view.right() + 1.f, view.bottom() + 1.f}, kTrimShadow, 0.f, 0, 3.f);
    draw->AddRect({view.x, view.y}, {view.right(), view.bottom()}, kTrim, 0.f, 0, 2.f);
}

} // namespace

bool InitializeUiFrame(entt::registry& registry) {
    const auto* assets = registry.ctx().find<FAssetManager>();
    if (assets == nullptr) {
        LOG_ERROR("The UI frame needs the asset manager.");
        return false;
    }
    const auto digest = assets->digest("data/ui_layout");
    if (!digest) return false;
    auto spec = ParseUiLayoutSpec(*digest);
    if (!spec) {
        LOG_ERROR("data/ui_layout.json is unusable: {}", spec.error());
        return false;
    }

    FUiFrame frame;
    frame.spec = std::move(*spec);
    auto* resources = registry.ctx().find<FResourceManager>();
    if (resources != nullptr && assets->resolvePath(frame.spec.texture, ".png")) {
        if (const auto id = resources->loadTexture(*assets, frame.spec.texture)) {
            if (const auto* texture = resources->find(*id); texture != nullptr && texture->texture != nullptr) {
                frame.art = {ImTextureRef{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->texture))},
                             {static_cast<float>(texture->width), static_cast<float>(texture->height)}};
            }
        }
    } else {
        LOG_INFO("UI frame art '{}' not found; drawing the stone bands instead.", frame.spec.texture);
    }
    registry.ctx().erase<FUiFrame>();
    registry.ctx().emplace<FUiFrame>(std::move(frame));
    return true;
}

const FUiLayout* FindUiLayout(entt::registry& registry) {
    auto* frame = registry.ctx().find<FUiFrame>();
    const auto* window = registry.ctx().find<FEngineWindowState>();
    if (frame == nullptr || window == nullptr || window->width <= 0 || window->height <= 0) return nullptr;
    const auto width = static_cast<float>(window->width);
    const auto height = static_cast<float>(window->height);
    if (frame->layoutWidth != width || frame->layoutHeight != height) {
        frame->layout = ComputeUiLayout(frame->spec, width, height);
        frame->layoutWidth = width;
        frame->layoutHeight = height;
    }
    return &frame->layout;
}

std::optional<FIsoView> FindBattleIsoView(entt::registry& registry, const FGridMetrics& metrics) {
    const FUiLayout* layout = FindUiLayout(registry);
    if (layout == nullptr) return std::nullopt;
    const FRect& viewport = layout->viewport;
    return MakeIsoView(metrics, FIsoViewport{viewport.x, viewport.y, viewport.w, viewport.h});
}

void DrawUiFrame(entt::registry& registry) {
    const FUiLayout* layout = FindUiLayout(registry);
    if (layout == nullptr) return;
    const auto& frame = registry.ctx().get<FUiFrame>();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (frame.art.valid()) DrawFrameArt(draw, frame.art, frame.spec, *layout);
    else DrawBands(draw, *layout);
}

} // namespace game::ui
