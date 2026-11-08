#include "presentation/FDungeonArt.h"

#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "core/Logger.h"
#include "gameplay/data/FContentRegistry.h"
#include <algorithm>
#include <cstdint>
#include <imgui.h>

namespace game {

namespace {

std::uint32_t CellHash(int x, int y) {
    auto h = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    return h ^ (h >> 15);
}

/// P14-8: the eight art ids `assets/data/units.json` authors. A unit's sprite is its content row's
/// `sprite` id resolved through this one table — never a hardcoded template-id chain.
struct FUnitSpriteEntry {
    std::string_view artId;
    EDungeonSprite sprite;
};

constexpr std::array<FUnitSpriteEntry, 8> kUnitSpriteTable{{
    {"art/warrior", EDungeonSprite::Warrior},
    {"art/archer", EDungeonSprite::Archer},
    {"art/mage", EDungeonSprite::Mage},
    {"art/healer", EDungeonSprite::Healer},
    {"art/imp", EDungeonSprite::Imp},
    {"art/imp_archer", EDungeonSprite::ImpArcher},
    {"art/imp_shaman", EDungeonSprite::ImpShaman},
    {"art/guardian", EDungeonSprite::Guardian},
}};

/// The fallback for a unit whose sprite cannot be resolved (P14-8): an unknown enemy never renders as a
/// party warrior — `enemy.*` reads as the imp; party/unknown ids keep the warrior default.
[[nodiscard]] EDungeonSprite FallbackUnitSprite(std::string_view templateId) {
    return templateId.starts_with("enemy.") ? EDungeonSprite::Imp : EDungeonSprite::Warrior;
}

[[nodiscard]] EDungeonSprite ResolveUnitSprite(const gameplay::FContentRegistry& content,
                                               std::string_view templateId) {
    const gameplay::FUnitContent* unit = content.units.find(templateId);
    if (unit == nullptr) {
        return FallbackUnitSprite(templateId);
    }
    const auto found = std::ranges::find(kUnitSpriteTable, unit->spriteId, &FUnitSpriteEntry::artId);
    return found != kUnitSpriteTable.end() ? found->sprite : FallbackUnitSprite(templateId);
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

std::optional<EDungeonSprite> SkillIconSprite(std::string_view skillId) {
    struct FSkillIcon {
        std::string_view skill;
        EDungeonSprite sprite;
    };
    constexpr std::array<FSkillIcon, 12> icons{{
        {"skill.strike", EDungeonSprite::SkillStrike}, {"skill.cleave", EDungeonSprite::SkillCleave},
        {"skill.arrow", EDungeonSprite::SkillArrow}, {"skill.volley", EDungeonSprite::SkillVolley},
        {"skill.bolt", EDungeonSprite::SkillBolt}, {"skill.fog", EDungeonSprite::SkillFog},
        {"skill.heal", EDungeonSprite::SkillHeal}, {"skill.buff", EDungeonSprite::SkillBuff},
        {"skill.debuff", EDungeonSprite::SkillDebuff}, {"skill.lunge", EDungeonSprite::SkillLunge},
        {"skill.snipe", EDungeonSprite::SkillSnipe}, {"skill.firebolt", EDungeonSprite::SkillFirebolt},
    }};
    const auto found = std::ranges::find(icons, skillId, &FSkillIcon::skill);
    if (found == icons.end()) return std::nullopt;
    return found->sprite;
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
        int width;   // exact size in pixels; 0 leaves the picture unchecked (the old UI pieces vary)
        int height;
    };
    // Sizes of art/env and art/ui are the values of assets/art/asset_contract.json; a strip is frames x width.
    constexpr std::array<FArtEntry, static_cast<std::size_t>(EDungeonSprite::Count)> entries{{
        {"art/floor", 64, 64}, {"art/wall", 64, 64}, {"art/spikes", 64, 64}, {"art/chest", 64, 64},
        {"art/stairs", 64, 64}, {"art/shrine", 64, 64}, {"art/warrior", 64, 64}, {"art/archer", 64, 64},
        {"art/mage", 64, 64}, {"art/healer", 64, 64}, {"art/imp", 64, 64}, {"art/imp_archer", 64, 64},
        {"art/imp_shaman", 64, 64}, {"art/guardian", 64, 64}, {"art/floor_b", 64, 64}, {"art/floor_c", 64, 64},
        {"art/wall_b", 64, 64}, {"art/icon_sword", 64, 64}, {"art/icon_bow", 64, 64}, {"art/icon_spell", 64, 64},
        {"art/icon_boots", 64, 64}, {"art/ui_panel", 0, 0}, {"art/ui_plaque", 0, 0}, {"art/ui_slot", 0, 0},
        {"art/env/ground_a", 512, 256}, {"art/env/decal_moss", 128, 64}, {"art/env/decal_rubble", 128, 64},
        {"art/env/tile_trap", 64, 32}, {"art/env/tile_blocked", 64, 32}, {"art/env/wall_a", 128, 128},
        {"art/env/wall_b", 128, 128}, {"art/env/wall_c", 128, 128}, {"art/env/arch", 128, 128},
        {"art/env/cliff", 128, 128}, {"art/env/pillar", 64, 96}, {"art/env/brazier", 64, 96},
        {"art/env/crystal", 64, 96}, {"art/env/tree_dead", 128, 160},
        {"art/env/brazier_anim", 64 * kBrazierFrames, 96},
        {"art/ui/hud_bar", 420, 84}, {"art/ui/hud_panel", 256, 128}, {"art/ui/hud_button", 136, 56},
        {"art/ui/hud_portrait", 72, 72}, {"art/ui/hud_turnbar", 320, 32}, {"art/ui/title_logo", 460, 120},
        {"art/ui/title_bg", 688, 384}, {"art/ui/skill_strike", 48, 48}, {"art/ui/skill_cleave", 48, 48},
        {"art/ui/skill_arrow", 48, 48}, {"art/ui/skill_volley", 48, 48}, {"art/ui/skill_bolt", 48, 48},
        {"art/ui/skill_fog", 48, 48}, {"art/ui/skill_heal", 48, 48}, {"art/ui/skill_buff", 48, 48},
        {"art/ui/skill_debuff", 48, 48}, {"art/ui/skill_lunge", 48, 48}, {"art/ui/skill_snipe", 48, 48},
        {"art/ui/skill_firebolt", 48, 48},
    }};
    const auto& assets = registry.ctx().get<FAssetManager>();
    auto& resources = registry.ctx().get<FResourceManager>();
    FDungeonArt art;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        const auto texture = resources.loadTexture(assets, entry.id);
        if (!texture) {
            LOG_ERROR("Dungeon art missing: {}", entry.id);
            return false;
        }
        const auto size = resources.findSize(*texture);
        if (!size || (entry.width > 0 && (size->width != entry.width || size->height != entry.height))) {
            LOG_ERROR("Dungeon art must be native {}x{}: {}", entry.width, entry.height, entry.id);
            return false;
        }
        art.textures[index] = *texture;
    }
    registry.ctx().emplace<FDungeonArt>(art);
    InstallDungeonUiSkin(registry);
    return true;
}

ui::FSkinPiece DungeonSkinPiece(entt::registry& registry, EDungeonSprite sprite) {
    const auto* art = registry.ctx().find<FDungeonArt>();
    const auto* resources = registry.ctx().find<FResourceManager>();
    if (!art || !resources) return {};
    const auto* texture = resources->find(art->textures[static_cast<std::size_t>(sprite)]);
    if (!texture || !texture->texture) return {};
    return {ImTextureRef{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->texture))},
            {static_cast<float>(texture->width), static_cast<float>(texture->height)}};
}

void InstallDungeonUiSkin(entt::registry& registry) {
    ui::InstallSkin({DungeonSkinPiece(registry, EDungeonSprite::UiPanel), DungeonSkinPiece(registry, EDungeonSprite::UiPlaque),
                     DungeonSkinPiece(registry, EDungeonSprite::UiSlot)});
}

void AssignDungeonSprites(entt::registry& registry, std::span<const FUnitPlacement> units) {
    // P14-8: the sprite is the unit's content `sprite` id mapped through kUnitSpriteTable, so a new enemy
    // needs data only. A registry without content (a bare test boot) keeps the per-side fallback.
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    for (const auto& unit : units) {
        const EDungeonSprite sprite = content != nullptr
            ? ResolveUnitSprite(*content, unit.unitTemplateId)
            : FallbackUnitSprite(unit.unitTemplateId);
        registry.emplace<FUnitArt>(unit.entity, sprite);
    }
}

void DrawDungeonSprite(entt::registry& registry, EDungeonSprite sprite, SDL_FRect destination, const FSpriteStyle& style) {
    const auto* art = registry.ctx().find<FDungeonArt>();
    const auto* resources = registry.ctx().find<FResourceManager>();
    const auto* renderer = registry.ctx().find<SDL_Renderer*>();
    if (!art || !resources || !renderer) return;
    const auto* texture = resources->find(art->textures[static_cast<std::size_t>(sprite)]);
    if (!texture) return;
    const float frameWidth = static_cast<float>(texture->width) / static_cast<float>(style.frames);
    const SDL_FRect source{frameWidth * static_cast<float>(style.frame), 0.f, frameWidth, static_cast<float>(texture->height)};
    SDL_SetTextureAlphaMod(texture->texture, style.alpha);
    SDL_SetTextureColorMod(texture->texture, style.tint.r, style.tint.g, style.tint.b);
    SDL_RenderTextureRotated(*renderer, texture->texture, &source, &destination, 0.0, nullptr,
                             style.flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(texture->texture, 255);
    SDL_SetTextureColorMod(texture->texture, 255, 255, 255);
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
