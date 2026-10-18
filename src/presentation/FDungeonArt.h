#pragma once

#include <array>
#include <span>
#include <glm/vec2.hpp>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <entt/entt.hpp>

#include "core/AssetHandles.h"
#include "gameplay/grid/FBattlefieldData.h"

namespace game {

enum class EDungeonSprite : std::size_t {
    Floor, Wall, Spikes, Chest, Stairs, Shrine,
    Warrior, Archer, Mage, Healer, Imp, ImpArcher, ImpShaman, Guardian,
    FloorB, FloorC, WallB, Backdrop,
    IconSword, IconBow, IconSpell, IconBoots,
    // Isometric dark-forest battlefield (64x64 ground blocks, taller obstacles) and UI skin pieces.
    IsoFloor0, IsoFloor1, IsoFloor2, IsoFloor3, IsoFloor4, IsoFloor5, IsoFloor6, IsoFloor7,
    IsoTrap, IsoRoot, IsoTree, IsoBoulders, IsoThorns,
    UiPanel, UiPlaque, UiSlot, Count
};

inline constexpr std::size_t kIsoFloorVariants = 8;

struct FDungeonArt {
    std::array<FTextureId, static_cast<std::size_t>(EDungeonSprite::Count)> textures{};
};

struct FUnitArt {
    EDungeonSprite sprite{EDungeonSprite::Warrior};
};

/// Deterministic per-cell tile variants so rooms do not read as a repeated stamp.
[[nodiscard]] EDungeonSprite FloorVariant(int x, int y);
[[nodiscard]] EDungeonSprite WallVariant(int x, int y);
[[nodiscard]] EDungeonSprite IsoFloorVariant(int x, int y);
/// Obstacle drawn on a Wall cell of the isometric battlefield (tree, boulders or thorn thicket).
[[nodiscard]] EDungeonSprite IsoObstacleVariant(int x, int y);
/// Texture size in pixels for a loaded sprite; {0, 0} when the art is unavailable.
[[nodiscard]] glm::vec2 DungeonSpriteSize(entt::registry& registry, EDungeonSprite sprite);

bool InitializeDungeonArt(entt::registry& registry);
/// Hands the panel / plaque / slot textures to the ImGui theme so every window gets the carved-metal skin.
void InstallDungeonUiSkin(entt::registry& registry);
void AssignDungeonSprites(entt::registry& registry, std::span<const FUnitPlacement> units);
void DrawDungeonSprite(entt::registry& registry, EDungeonSprite sprite, SDL_FRect destination, Uint8 alpha = 255);
void DrawDungeonIcon(entt::registry& registry, EDungeonSprite sprite);
/// Draws part of a sprite into an ImGui draw list (portraits, skill icons). No-op when art is unavailable.
void DrawDungeonImage(entt::registry& registry, EDungeonSprite sprite, ImDrawList* draw,
                      ImVec2 min, ImVec2 max, ImVec2 uvMin = {0, 0}, ImVec2 uvMax = {1, 1},
                      ImU32 tint = IM_COL32_WHITE);

} // namespace game
