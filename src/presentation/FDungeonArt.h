#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <glm/vec2.hpp>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <entt/entt.hpp>

#include "core/AssetHandles.h"
#include "gameplay/grid/FBattlefieldData.h"
#include "presentation/FUiTheme.h"

namespace game {

enum class EDungeonSprite : std::size_t {
    Floor, Wall, Spikes, Chest, Stairs, Shrine,
    Warrior, Archer, Mage, Healer, Imp, ImpArcher, ImpShaman, Guardian,
    FloorB, FloorC, WallB,
    IconSword, IconBow, IconSpell, IconBoots,
    UiPanel, UiPlaque, UiSlot,
    // Pictures listed in assets/art/asset_contract.json: forest scene, floating HUD, title and skill icons.
    SceneGround, SceneDecalMoss, SceneDecalRubble, SceneTileTrap, SceneTileBlocked,
    SceneWallA, SceneWallB, SceneWallC, SceneArch, SceneCliff, ScenePillar, SceneBrazier, SceneCrystal, SceneTree,
    SceneBrazierAnim,
    HudBar, HudPanel, HudButton, HudPortrait, HudTurnBar, TitleLogo, TitleBg,
    SkillStrike, SkillCleave, SkillArrow, SkillVolley, SkillBolt, SkillFog, SkillHeal, SkillBuff, SkillDebuff,
    SkillLunge, SkillSnipe, SkillFirebolt,
    Count
};

/// Frames in the horizontal strip of EDungeonSprite::SceneBrazierAnim.
inline constexpr int kBrazierFrames = 6;

struct FDungeonArt {
    std::array<FTextureId, static_cast<std::size_t>(EDungeonSprite::Count)> textures{};
};

struct FUnitArt {
    EDungeonSprite sprite{EDungeonSprite::Warrior};
};

/// How one sprite is drawn. `frame` indexes a horizontal strip of `frames` equal pictures.
struct FSpriteStyle {
    Uint8 alpha{255};
    SDL_Color tint{255, 255, 255, 255};
    bool flip{false};
    int frame{0};
    int frames{1};
};

/// Deterministic per-cell tile variants so rooms do not read as a repeated stamp.
[[nodiscard]] EDungeonSprite FloorVariant(int x, int y);
[[nodiscard]] EDungeonSprite WallVariant(int x, int y);
/// Texture size in pixels for a loaded sprite; {0, 0} when the art is unavailable.
[[nodiscard]] glm::vec2 DungeonSpriteSize(entt::registry& registry, EDungeonSprite sprite);
/// The icon drawn for a skill id; empty for a skill the art has no icon for.
[[nodiscard]] std::optional<EDungeonSprite> SkillIconSprite(std::string_view skillId);

bool InitializeDungeonArt(entt::registry& registry);
/// Hands the panel / plaque / slot textures to the ImGui theme so every window gets the carved-metal skin.
void InstallDungeonUiSkin(entt::registry& registry);
/// A sprite as a UI skin piece; empty (and so skipped by DrawNineSlice) when the art is unavailable.
[[nodiscard]] ui::FSkinPiece DungeonSkinPiece(entt::registry& registry, EDungeonSprite sprite);
void AssignDungeonSprites(entt::registry& registry, std::span<const FUnitPlacement> units);
void DrawDungeonSprite(entt::registry& registry, EDungeonSprite sprite, SDL_FRect destination, const FSpriteStyle& style = {});
void DrawDungeonIcon(entt::registry& registry, EDungeonSprite sprite);
/// Draws part of a sprite into an ImGui draw list (portraits, skill icons). No-op when art is unavailable.
void DrawDungeonImage(entt::registry& registry, EDungeonSprite sprite, ImDrawList* draw,
                      ImVec2 min, ImVec2 max, ImVec2 uvMin = {0, 0}, ImVec2 uvMax = {1, 1},
                      ImU32 tint = IM_COL32_WHITE);

} // namespace game
