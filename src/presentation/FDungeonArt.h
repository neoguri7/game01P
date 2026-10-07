#pragma once

#include <array>
#include <span>
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
    IconSword, IconBow, IconSpell, IconBoots, Count
};

struct FDungeonArt {
    std::array<FTextureId, static_cast<std::size_t>(EDungeonSprite::Count)> textures{};
};

struct FUnitArt {
    EDungeonSprite sprite{EDungeonSprite::Warrior};
};

/// Deterministic per-cell tile variants so rooms do not read as a repeated stamp.
[[nodiscard]] EDungeonSprite FloorVariant(int x, int y);
[[nodiscard]] EDungeonSprite WallVariant(int x, int y);

bool InitializeDungeonArt(entt::registry& registry);
void AssignDungeonSprites(entt::registry& registry, std::span<const FUnitPlacement> units);
void DrawDungeonSprite(entt::registry& registry, EDungeonSprite sprite, SDL_FRect destination, Uint8 alpha = 255);
void DrawDungeonIcon(entt::registry& registry, EDungeonSprite sprite);
/// Draws part of a sprite into an ImGui draw list (portraits, skill icons). No-op when art is unavailable.
void DrawDungeonImage(entt::registry& registry, EDungeonSprite sprite, ImDrawList* draw,
                      ImVec2 min, ImVec2 max, ImVec2 uvMin = {0, 0}, ImVec2 uvMax = {1, 1},
                      ImU32 tint = IM_COL32_WHITE);

} // namespace game
