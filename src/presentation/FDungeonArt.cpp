#include "presentation/FDungeonArt.h"

#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "core/Logger.h"
#include "presentation/FUiTheme.h"
#include <cstdint>
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

EDungeonSprite IsoFloorVariant(int x, int y) {
    // Mostly dark soil, with mossy patches and glowing fern clumps as accents.
    constexpr std::array<std::size_t, 12> variants{0, 1, 2, 3, 4, 5, 4, 5, 6, 0, 1, 7};
    return static_cast<EDungeonSprite>(static_cast<std::size_t>(EDungeonSprite::IsoFloor0) +
                                       variants[CellHash(x, y) % variants.size()]);
}

EDungeonSprite IsoObstacleVariant(int x, int y) {
    constexpr std::array variants{EDungeonSprite::IsoTree, EDungeonSprite::IsoBoulders, EDungeonSprite::IsoThorns,
                                  EDungeonSprite::IsoTree, EDungeonSprite::IsoBoulders};
    return variants[CellHash(x, y) % variants.size()];
}

glm::vec2 DungeonSpriteSize(entt::registry& registry, EDungeonSprite sprite) {
    const auto* art = registry.ctx().find<FDungeonArt>();
    const auto* resources = registry.ctx().find<FResourceManager>();
    if (!art || !resources) return {0.f, 0.f};
    const auto size = resources->findSize(art->textures[static_cast<std::size_t>(sprite)]);
    return size ? glm::vec2{static_cast<float>(size->width), static_cast<float>(size->height)} : glm::vec2{0.f, 0.f};
}

bool InitializeDungeonArt(entt::registry& registry) {
    struct FArtEntry {
        std::string_view id;
        bool native64;  // the original 64x64 set is size-checked; tall obstacles and UI pieces vary
    };
    constexpr std::array<FArtEntry, static_cast<std::size_t>(EDungeonSprite::Count)> entries{{
        {"art/floor", true}, {"art/wall", true}, {"art/spikes", true}, {"art/chest", true}, {"art/stairs", true},
        {"art/shrine", true}, {"art/warrior", true}, {"art/archer", true}, {"art/mage", true}, {"art/healer", true},
        {"art/imp", true}, {"art/imp_archer", true}, {"art/imp_shaman", true}, {"art/guardian", true},
        {"art/floor_b", true}, {"art/floor_c", true}, {"art/wall_b", true}, {"art/backdrop", true},
        {"art/icon_sword", true}, {"art/icon_bow", true}, {"art/icon_spell", true}, {"art/icon_boots", true},
        {"art/iso_floor_0", true}, {"art/iso_floor_1", true}, {"art/iso_floor_2", true}, {"art/iso_floor_3", true},
        {"art/iso_floor_4", true}, {"art/iso_floor_5", true}, {"art/iso_floor_6", true}, {"art/iso_floor_7", true},
        {"art/iso_trap", true}, {"art/iso_root", true}, {"art/iso_tree", false}, {"art/iso_boulders", true},
        {"art/iso_thorns", true}, {"art/ui_panel", false}, {"art/ui_plaque", false}, {"art/ui_slot", false},
    }};
    const auto& assets = registry.ctx().get<FAssetManager>();
    auto& resources = registry.ctx().get<FResourceManager>();
    FDungeonArt art;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto texture = resources.loadTexture(assets, entries[index].id);
        if (!texture) {
            LOG_ERROR("Dungeon art missing: {}", entries[index].id);
            return false;
        }
        const auto size = resources.findSize(*texture);
        if (!size || (entries[index].native64 && (size->width != 64 || size->height != 64))) {
            LOG_ERROR("Dungeon art must be native 64x64: {}", entries[index].id);
            return false;
        }
        art.textures[index] = *texture;
    }
    registry.ctx().emplace<FDungeonArt>(art);
    InstallDungeonUiSkin(registry);
    return true;
}

void InstallDungeonUiSkin(entt::registry& registry) {
    const auto* art = registry.ctx().find<FDungeonArt>();
    const auto* resources = registry.ctx().find<FResourceManager>();
    if (!art || !resources) return;
    const auto piece = [&](EDungeonSprite sprite) {
        const auto* texture = resources->find(art->textures[static_cast<std::size_t>(sprite)]);
        if (!texture || !texture->texture) return ui::FSkinPiece{};
        return ui::FSkinPiece{ImTextureRef{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->texture))},
                              {static_cast<float>(texture->width), static_cast<float>(texture->height)}};
    };
    ui::InstallSkin({piece(EDungeonSprite::UiPanel), piece(EDungeonSprite::UiPlaque), piece(EDungeonSprite::UiSlot)});
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

void DrawDungeonImage(entt::registry& registry, EDungeonSprite sprite, ImDrawList* draw,
                      ImVec2 min, ImVec2 max, ImVec2 uvMin, ImVec2 uvMax, ImU32 tint) {
    const auto* art = registry.ctx().find<FDungeonArt>();
    const auto* resources = registry.ctx().find<FResourceManager>();
    if (!art || !resources || !draw) return;
    const auto* texture = resources->find(art->textures[static_cast<std::size_t>(sprite)]);
    if (!texture) return;
    draw->AddImage(ImTextureRef{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->texture))},
                   min, max, uvMin, uvMax, tint);
}

} // namespace game
