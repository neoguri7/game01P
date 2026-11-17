#include "presentation/FUiFrame.h"

#include <utility>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/Logger.h"

namespace game::ui {

bool InitializeUiFrame(entt::registry& registry) {
    const auto* assets = registry.ctx().find<FAssetManager>();
    if (assets == nullptr) {
        LOG_ERROR("The UI layout needs the asset manager.");
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
    const FRect& arena = layout->arena;
    return MakeIsoView(metrics, FIsoViewport{arena.x, arena.y, arena.w, arena.h});
}

} // namespace game::ui
