#include "presentation/FModalViewRegistry.h"

#include "core/Logger.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonViews.h"

namespace game {

void RegisterModalView(entt::registry& registry, std::string_view id, void (*render)(entt::registry&)) {
    if (id.empty() || render == nullptr) {
        LOG_ERROR("RegisterModalView rejected an empty id or a null render function.");
        return;
    }
    if (!registry.ctx().contains<FModalViewRegistry>()) {
        registry.ctx().emplace<FModalViewRegistry>();
    }
    auto& views = registry.ctx().get<FModalViewRegistry>().views;
    for (const auto& view : views) {
        if (view.id == id) {
            LOG_ERROR("RegisterModalView rejected duplicate id '{}'; the first registration wins.", id);
            return;
        }
    }
    views.push_back(FModalView{std::string(id), render});
}

void RegisterModalViews(entt::registry& registry) {
    RegisterModalView(registry, kRunModalReward, RenderDungeonRewardView);
    RegisterModalView(registry, kRunModalEvent, RenderDungeonEventView);
    RegisterModalView(registry, kRunModalCamp, RenderDungeonCampView);
    RegisterModalView(registry, kRunModalRunEnd, RenderDungeonDefeatView);
}

const FModalView* FindModalView(const entt::registry& registry, std::string_view id) {
    const auto* views = registry.ctx().find<FModalViewRegistry>();
    if (views == nullptr) return nullptr;
    for (const auto& view : views->views) {
        if (view.id == id) return &view;
    }
    return nullptr;
}

void RequestModalViewClose(entt::registry& registry) {
    if (!registry.ctx().contains<FModalViewCloseRequest>()) {
        registry.ctx().emplace<FModalViewCloseRequest>();
    }
    registry.ctx().get<FModalViewCloseRequest>().requested = true;
}

bool TakeModalViewCloseRequest(entt::registry& registry) {
    auto* request = registry.ctx().find<FModalViewCloseRequest>();
    if (request == nullptr) return false;
    const bool requested = request->requested;
    registry.ctx().erase<FModalViewCloseRequest>();
    return requested;
}

} // namespace game
