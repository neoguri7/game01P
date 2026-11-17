#include "presentation/FFlowArt.h"

#include <cstdint>

#include <imgui.h>

#include "core/AssetManager.h"
#include "core/ResourceManager.h"

namespace game {

std::string_view NodeIconId(EBriefingNodeKind kind)
{
    switch (kind) {
    case EBriefingNodeKind::Entrance: return "art/ui/node_start";
    case EBriefingNodeKind::Battle: return "art/ui/node_battle";
    case EBriefingNodeKind::Elite: return "art/ui/node_elite";
    case EBriefingNodeKind::Event: return "art/ui/node_event";
    case EBriefingNodeKind::Mystery: return "art/ui/node_unknown";
    case EBriefingNodeKind::Boss: return "art/ui/node_boss";
    case EBriefingNodeKind::Camp: return "art/ui/node_camp";
    }
    return {};
}

std::string ThemeBannerId(std::string_view themeId)
{
    constexpr std::string_view prefix = "theme.";
    if (themeId.starts_with(prefix)) themeId.remove_prefix(prefix.size());
    return "art/ui/theme_" + std::string(themeId);
}

ui::FSkinPiece FlowPicture(entt::registry& registry, std::string_view assetId)
{
    if (assetId.empty()) return {};
    const auto* assets = registry.ctx().find<FAssetManager>();
    auto* resources = registry.ctx().find<FResourceManager>();
    if (assets == nullptr || resources == nullptr) return {};
    const auto id = resources->loadTexture(*assets, assetId);
    if (!id) return {};
    const FTextureInfo* info = resources->find(*id);
    if (info == nullptr || info->texture == nullptr) return {};
    return {ImTextureRef{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(info->texture))},
            {static_cast<float>(info->width), static_cast<float>(info->height)}};
}

} // namespace game
