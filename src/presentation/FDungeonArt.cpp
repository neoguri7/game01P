#include "presentation/FDungeonArt.h"

#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "core/Logger.h"
#include <string_view>
#include <imgui.h>

namespace game {

namespace {

std::uint32_t CellHash(int x, int y) {
    auto h = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    return h ^ (h >> 15);
}

} // namespace

EDungeonSprite FloorVariant(int x, int y) {
    constexpr std::array variants{EDungeonSprite::Floor, EDungeonSprite::FloorB, EDungeonSprite::Floor,
                                  EDungeonSprite::FloorC, EDungeonSprite::FloorB, EDungeonSprite::Floor};
    return variants[CellHash(x, y) % variants.size()];
}

EDungeonSprite WallVariant(int x, int y) {
    return CellHash(x, y) % 3 == 0 ? EDungeonSprite::WallB : EDungeonSprite::Wall;
}

bool InitializeDungeonArt(entt::registry& registry) {
    constexpr std::array<std::string_view, static_cast<std::size_t>(EDungeonSprite::Count)> ids{
        "art/floor", "art/wall", "art/spikes", "art/chest", "art/stairs", "art/shrine",
        "art/warrior", "art/archer", "art/mage", "art/healer", "art/imp",
        "art/imp_archer", "art/imp_shaman", "art/guardian",
        "art/floor_b", "art/floor_c", "art/wall_b", "art/backdrop"
    };
    const auto& assets = registry.ctx().get<FAssetManager>();
    auto& resources = registry.ctx().get<FResourceManager>();
    FDungeonArt art;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto texture = resources.loadTexture(assets, ids[index]);
        if (!texture) {
            LOG_ERROR("Dungeon art missing: {}", ids[index]);
            return false;
        }
        const auto size = resources.findSize(*texture);
        if (!size || size->width != 64 || size->height != 64) {
            LOG_ERROR("Dungeon art must be native 64x64: {}", ids[index]);
            return false;
        }
        art.textures[index] = *texture;
    }
    registry.ctx().emplace<FDungeonArt>(art);
    return true;
}

void AssignDungeonSprites(entt::registry& registry, std::span<const FUnitPlacement> units) {
    for (const auto& unit : units) {
        EDungeonSprite sprite = EDungeonSprite::Warrior;
        if (unit.unitTemplateId == "char.archer") sprite = EDungeonSprite::Archer;
        else if (unit.unitTemplateId == "char.mage") sprite = EDungeonSprite::Mage;
        else if (unit.unitTemplateId == "char.healer") sprite = EDungeonSprite::Healer;
        else if (unit.unitTemplateId == "enemy.imp") sprite = EDungeonSprite::Imp;
        else if (unit.unitTemplateId == "enemy.imp_archer") sprite = EDungeonSprite::ImpArcher;
        else if (unit.unitTemplateId == "enemy.imp_shaman") sprite = EDungeonSprite::ImpShaman;
        else if (unit.unitTemplateId == "enemy.guardian") sprite = EDungeonSprite::Guardian;
        registry.emplace<FUnitArt>(unit.entity, sprite);
    }
}

void DrawDungeonSprite(entt::registry& registry, EDungeonSprite sprite, SDL_FRect destination, Uint8 alpha) {
    const auto* art = registry.ctx().find<FDungeonArt>();
    const auto* resources = registry.ctx().find<FResourceManager>();
    const auto* renderer = registry.ctx().find<SDL_Renderer*>();
    if (!art || !resources || !renderer) return;
    const auto* texture = resources->find(art->textures[static_cast<std::size_t>(sprite)]);
    if (!texture) return;
    SDL_SetTextureAlphaMod(texture->texture, alpha);
    SDL_RenderTexture(*renderer, texture->texture, nullptr, &destination);
    SDL_SetTextureAlphaMod(texture->texture, 255);
}

void DrawDungeonIcon(entt::registry& registry, EDungeonSprite sprite) {
    const auto& art = registry.ctx().get<FDungeonArt>();
    const auto& resources = registry.ctx().get<FResourceManager>();
    const auto* texture = resources.find(art.textures[static_cast<std::size_t>(sprite)]);
    if (!texture) {
        LOG_ERROR("Dungeon icon texture is unavailable.");
        return;
    }
    ImGui::Image(ImTextureRef{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->texture))}, {64, 64});
}

} // namespace game
