#include "ecs/systems/EmberGridRenderSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <span>
#include <vector>
#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include "core/EngineState.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/systems/EmberCombatInfo.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTerrain.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FForestScene.h"
#include "presentation/FIsoView.h"
#include "presentation/FUiFrame.h"
#include "presentation/FUiTheme.h"
#include "presentation/FUnitFrame.h"

namespace game::ecs {
namespace {

constexpr float kTau = 6.2831853f;
// How far below the cell center an obstacle's or unit's feet rest; the same rule places every standing picture.
constexpr float kFeetOffset = 8.f;
// The light mesh is multiplied over the ground; light changes slowly enough for coarse cells.
constexpr int kLightColumns = 48;
constexpr int kLightRows = 30;
constexpr float kBrazierFps = 9.f;

// WP-12 board overlays: distinct from the existing reachable (cyan) and affected (orange-red) fills —
// danger is a magenta-crimson (an enemy promise), reach is violet (where a hovered enemy could strike).
constexpr SDL_FColor kIntentDangerFill{0.66f, 0.10f, 0.28f, 0.26f};
constexpr SDL_Color kIntentDangerLine{240, 64, 110, 200};
constexpr SDL_FColor kIntentReachFill{0.50f, 0.44f, 1.f, 0.16f};
constexpr SDL_Color kIntentReachLine{170, 150, 255, 140};
constexpr SDL_FColor kMovePathFill{1.f, 0.86f, 0.42f, 0.12f};
constexpr SDL_Color kMovePathLine{255, 224, 128, 115};
constexpr SDL_Color kMoveDestinationLine{255, 240, 180, 230};
constexpr SDL_FColor kMoveTrapFill{1.f, 0.32f, 0.10f, 0.20f};
constexpr SDL_Color kMoveTrapLine{255, 118, 40, 230};
constexpr float kMoveTrapOutlineInset = 0.f;
constexpr float kMovePathOutlineInset = 2.f;
constexpr float kReachOutlineInset = 0.f;
// The hovered cell's double stroke (pointer-cursor affordance, inset 0 + 2 — SelectionOutlineInsets).
constexpr SDL_Color kHoverOutline[]{{255, 224, 120, 230}, {255, 224, 120, 120}};
static_assert(std::size(kHoverOutline) == 2, "one stroke colour per hovered inset");

SDL_FColor Color(int r, int g, int b, int a) {
    return {static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f,
            static_cast<float>(a) / 255.f};
}

std::uint32_t Hash(int x, int y) {
    auto h = static_cast<std::uint32_t>(x + 97) * 73856093u ^ static_cast<std::uint32_t>(y + 131) * 19349663u;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    return h ^ (h >> 15);
}

void Quad(SDL_Renderer* renderer, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_FColor color) {
    const SDL_Vertex vertices[4]{{a, color, {}}, {b, color, {}}, {c, color, {}}, {d, color, {}}};
    constexpr int indices[6]{0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
}

void FillDiamond(SDL_Renderer* renderer, const FIsoView& view, int x, int y, SDL_FColor color) {
    const auto fx = static_cast<float>(x);
    const auto fy = static_cast<float>(y);
    const auto top = view.corner(fx, fy);
    const auto right = view.corner(fx + 1.f, fy);
    const auto bottom = view.corner(fx + 1.f, fy + 1.f);
    const auto left = view.corner(fx, fy + 1.f);
    Quad(renderer, {top.x, top.y}, {right.x, right.y}, {bottom.x, bottom.y}, {left.x, left.y}, color);
}

void OutlineDiamond(SDL_Renderer* renderer, const FIsoView& view, int x, int y, SDL_Color color, float inset) {
    const auto center = view.cellCenter(x, y);
    const float halfW = kIsoHalfWidth * view.scale - inset * 2.f;
    const float halfH = kIsoHalfHeight * view.scale - inset;
    const SDL_FPoint points[5]{{center.x, center.y - halfH}, {center.x + halfW, center.y}, {center.x, center.y + halfH},
                               {center.x - halfW, center.y}, {center.x, center.y - halfH}};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLines(renderer, points, 5);
}

void Ring(SDL_Renderer* renderer, glm::vec2 center, float radiusX, float radiusY, SDL_Color color) {
    std::array<SDL_FPoint, 33> points{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        const float angle = kTau * static_cast<float>(i) / static_cast<float>(points.size() - 1);
        points[i] = {center.x + std::cos(angle) * radiusX, center.y + std::sin(angle) * radiusY};
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLines(renderer, points.data(), static_cast<int>(points.size()));
}

void SoftGlow(SDL_Renderer* renderer, glm::vec2 center, float radiusX, float radiusY, SDL_FColor inner) {
    constexpr int kSegments = 28;
    std::array<SDL_Vertex, kSegments + 1> vertices{};
    std::array<int, kSegments * 3> indices{};
    vertices[0] = {{center.x, center.y}, inner, {}};
    SDL_FColor edge = inner;
    edge.a = 0.f;
    for (int i = 0; i < kSegments; ++i) {
        const float angle = kTau * static_cast<float>(i) / static_cast<float>(kSegments);
        vertices[static_cast<std::size_t>(i) + 1] = {{center.x + std::cos(angle) * radiusX, center.y + std::sin(angle) * radiusY}, edge, {}};
        indices[static_cast<std::size_t>(i) * 3] = 0;
        indices[static_cast<std::size_t>(i) * 3 + 1] = i + 1;
        indices[static_cast<std::size_t>(i) * 3 + 2] = (i + 1) % kSegments + 1;
    }
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                       static_cast<int>(indices.size()));
}

// A pool of light on the ground: round on the grid, so an ellipse on screen.
struct FLightPool {
    glm::vec2 center;
    float radius;  // cells
    glm::vec3 color;
};

float Flicker(const FSceneLight& light, float time, float phase) {
    return 1.f - light.flicker * (.5f + .5f * std::sin(time * 7.f + phase) * std::sin(time * 3.1f + phase * 2.f));
}

std::vector<FLightPool> BuildLightPools(const FForestScene& scene, std::span<const FPlacedProp> props, const FIsoView& view,
                                        const FGridMetrics& grid, float time) {
    std::vector<FLightPool> pools;
    const auto gridWidth = static_cast<float>(grid.width);
    const auto gridHeight = static_cast<float>(grid.height);
    pools.push_back({view.corner(gridWidth * .5f, gridHeight * .5f), scene.arena.radius * std::max(gridWidth, gridHeight),
                     scene.arena.color});
    for (const auto& prop : props) {
        if (prop.sprite != EDungeonSprite::SceneBrazier && prop.sprite != EDungeonSprite::SceneCrystal) continue;
        const auto& light = prop.sprite == EDungeonSprite::SceneBrazier ? scene.brazier : scene.crystal;
        const float strength = Flicker(light, time, static_cast<float>(Hash(prop.cell.x, prop.cell.y) % 628) * .01f);
        pools.push_back({view.cellCenter(prop.cell.x, prop.cell.y), light.radius, light.color * strength});
    }
    return pools;
}

glm::vec3 LightAt(std::span<const FLightPool> pools, glm::vec3 ambient, const FIsoView& view, glm::vec2 point) {
    glm::vec3 light = ambient;
    for (const auto& pool : pools) {
        const glm::vec2 offset = (point - pool.center) / (glm::vec2{kIsoHalfWidth, kIsoHalfHeight} * view.scale);
        // Half the squared diamond offset is the squared distance on the grid.
        const float share = 1.f - (offset.x * offset.x + offset.y * offset.y) * .5f / (pool.radius * pool.radius);
        if (share > 0.f) light += pool.color * share * share;
    }
    return glm::min(light, glm::vec3{1.f});
}

// Multiplies the light over what is already drawn: inside a pool the art keeps its value, outside it sinks.
void DrawLightMesh(SDL_Renderer* renderer, std::span<const FLightPool> pools, glm::vec3 ambient, const FIsoView& view,
                   float width, float height) {
    std::vector<SDL_FColor> colors;
    colors.reserve((kLightColumns + 1) * (kLightRows + 1));
    for (int row = 0; row <= kLightRows; ++row) {
        for (int column = 0; column <= kLightColumns; ++column) {
            const glm::vec2 point{width * static_cast<float>(column) / kLightColumns, height * static_cast<float>(row) / kLightRows};
            const glm::vec3 light = LightAt(pools, ambient, view, point);
            colors.push_back({light.r, light.g, light.b, 1.f});
        }
    }
    DrawModulateMesh(renderer, colors, kLightColumns, kLightRows, width, height);
}

// The ground picture repeats over the whole window, pinned to the grid so its pattern does not slide with the window.
void DrawGround(entt::registry& registry, const FIsoView& view, float width, float height) {
    const glm::vec2 tile = DungeonSpriteSize(registry, EDungeonSprite::SceneGround) * view.scale;
    if (tile.x <= 0.f || tile.y <= 0.f) return;
    const float startX = view.origin.x - std::ceil(view.origin.x / tile.x) * tile.x;
    const float startY = view.origin.y - std::ceil(view.origin.y / tile.y) * tile.y;
    for (float y = startY; y < height; y += tile.y) {
        for (float x = startX; x < width; x += tile.x) {
            DrawDungeonSprite(registry, EDungeonSprite::SceneGround, {x, y, tile.x, tile.y});
        }
    }
}

// A flat picture centred on a ground point.
void DrawFlat(entt::registry& registry, const FIsoView& view, EDungeonSprite sprite, glm::vec2 center) {
    const glm::vec2 size = DungeonSpriteSize(registry, sprite) * view.scale;
    DrawDungeonSprite(registry, sprite, {std::floor(center.x - size.x * .5f), std::floor(center.y - size.y * .5f), size.x, size.y});
}

SDL_FRect StandingRect(entt::registry& registry, const FIsoView& view, FGridPosition cell, EDungeonSprite sprite) {
    const auto center = view.cellCenter(cell.x, cell.y);
    const auto size = DungeonSpriteSize(registry, sprite);
    return {std::floor(center.x - size.x * .5f * view.scale), std::floor(center.y + kFeetOffset * view.scale - size.y * view.scale),
            size.x * view.scale, size.y * view.scale};
}

// A prop takes its colour from the light at its feet; objectAmbient keeps it above the ground in value.
void DrawProp(entt::registry& registry, const FIsoView& view, const FPlacedProp& prop, std::span<const FLightPool> pools,
              glm::vec3 objectAmbient, float time) {
    const auto rect = StandingRect(registry, view, prop.cell, prop.sprite);
    const glm::vec3 light = LightAt(pools, objectAmbient, view, view.cellCenter(prop.cell.x, prop.cell.y));
    FSpriteStyle style;
    style.tint = {static_cast<Uint8>(light.r * 255.f), static_cast<Uint8>(light.g * 255.f), static_cast<Uint8>(light.b * 255.f), 255};
    style.flip = prop.flip;
    if (prop.sprite != EDungeonSprite::SceneBrazier) {
        DrawDungeonSprite(registry, prop.sprite, rect, style);
        return;
    }
    // Each brazier burns on its own beat so a row of them does not pulse in step.
    style.frames = kBrazierFrames;
    style.frame = static_cast<int>(time * kBrazierFps + static_cast<float>(Hash(prop.cell.x, prop.cell.y) % kBrazierFrames)) % kBrazierFrames;
    DrawDungeonSprite(registry, EDungeonSprite::SceneBrazierAnim, rect, style);
}

// Coloured light thrown on the ground around each source, the part of a pool brighter than the art itself.
void DrawGroundSpill(SDL_Renderer* renderer, const FForestScene& scene, std::span<const FPlacedProp> props, const FIsoView& view,
                     float time) {
    constexpr float kSqrt2 = 1.41421356f;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    for (const auto& prop : props) {
        if (prop.sprite != EDungeonSprite::SceneBrazier && prop.sprite != EDungeonSprite::SceneCrystal) continue;
        const auto& light = prop.sprite == EDungeonSprite::SceneBrazier ? scene.brazier : scene.crystal;
        const float strength = Flicker(light, time, static_cast<float>(Hash(prop.cell.x, prop.cell.y) % 628) * .01f);
        const float reach = light.radius * kSqrt2 * view.scale;
        SoftGlow(renderer, view.cellCenter(prop.cell.x, prop.cell.y), reach * kIsoHalfWidth, reach * kIsoHalfHeight,
                 {light.color.r, light.color.g, light.color.b, light.spill * strength});
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

// Bloom around the flames and crystals, added over everything standing near them.
void DrawSourceGlows(SDL_Renderer* renderer, const FForestScene& scene, std::span<const FPlacedProp> props, const FIsoView& view,
                     float time) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    for (const auto& prop : props) {
        if (prop.sprite != EDungeonSprite::SceneBrazier && prop.sprite != EDungeonSprite::SceneCrystal) continue;
        const auto& light = prop.sprite == EDungeonSprite::SceneBrazier ? scene.brazier : scene.crystal;
        const float strength = Flicker(light, time, static_cast<float>(Hash(prop.cell.x, prop.cell.y) % 628) * .01f);
        const auto center = view.cellCenter(prop.cell.x, prop.cell.y) - glm::vec2{0.f, light.height * view.scale};
        const float radius = light.glow * view.scale;
        SoftGlow(renderer, center, radius, radius, {light.color.r, light.color.g, light.color.b, .55f * strength});
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

// Sparse drifting spores: enough to keep the air alive, not enough to read as noise.
void DrawMotes(SDL_Renderer* renderer, const FIsoView& view, const FGridMetrics& grid, float width, float height,
               float time) {
    const auto mid = view.corner(static_cast<float>(grid.width) * .5f, static_cast<float>(grid.height) * .5f);
    const float dot = std::max(1.f, std::floor(view.scale));
    for (int index = 0; index < 9; ++index) {
        const auto h = Hash(index, 7);
        const float phase = static_cast<float>(h % 628) * .01f;
        const float baseX = mid.x + (static_cast<float>(h % 1000) / 1000.f - .5f) * width * .85f;
        const float baseY = mid.y + (static_cast<float>((h >> 10) % 1000) / 1000.f - .5f) * height * .6f;
        const float x = baseX + std::sin(time * .4f + phase) * 14.f * view.scale;
        const float y = baseY - std::fmod(time * (3.f + static_cast<float>(h % 5)) + phase * 9.f, 40.f) * view.scale;
        const float pulse = .5f + .5f * std::sin(time * 1.3f + phase * 3.f);
        const SDL_FRect mote{std::floor(x), std::floor(y), dot, dot};
        SDL_SetRenderDrawColor(renderer, 130, 255, 205, static_cast<Uint8>(20.f + 70.f * pulse));
        SDL_RenderFillRect(renderer, &mote);
    }
}

// Each window edge sinks into the dark over a smooth band.
void DrawVignette(SDL_Renderer* renderer, float width, float height) {
    const SDL_FColor dark = Color(0, 0, 4, 120);
    const SDL_FColor clear = Color(0, 0, 4, 0);
    const auto shade = [&](SDL_FPoint edgeA, SDL_FPoint edgeB, SDL_FPoint innerB, SDL_FPoint innerA) {
        const SDL_Vertex vertices[4]{{edgeA, dark, {}}, {edgeB, dark, {}}, {innerB, clear, {}}, {innerA, clear, {}}};
        constexpr int indices[6]{0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
    };
    const float reachX = width * .1f;
    const float reachY = height * .14f;
    shade({0.f, 0.f}, {width, 0.f}, {width, reachY}, {0.f, reachY});
    shade({0.f, height}, {width, height}, {width, height - reachY}, {0.f, height - reachY});
    shade({0.f, 0.f}, {0.f, height}, {reachX, height}, {reachX, 0.f});
    shade({width, 0.f}, {width, height}, {width - reachX, height}, {width - reachX, 0.f});
}

void DrawUnit(entt::registry& registry, SDL_Renderer* renderer, const FIsoView& view, const FGridSelection* selection,
              entt::entity entity) {
    const float scale = view.scale;
    const auto cell = registry.get<FGridPosition>(entity);
    // A unit with battle animations is drawn where its walk has reached, and is only "down" once its fall plays.
    const auto* anim = registry.try_get<FUnitAnim>(entity);
    auto center = anim != nullptr ? view.corner(anim->visual.x + .5f, anim->visual.y + .5f)
                                  : view.cellCenter(cell.x, cell.y);
    if (anim != nullptr) center += UnitAnimOffset(*anim) * scale;
    const bool dead = registry.all_of<FDead>(entity);
    const bool shownDead = anim != nullptr ? anim->state == EUnitAnim::Death : dead;
    const bool party = registry.all_of<FPartyMember>(entity);
    const SDL_Color faction = party ? SDL_Color{90, 150, 255, 255} : SDL_Color{235, 85, 80, 255};
    const glm::vec2 feet{center.x, center.y + 2.f * scale};
    if (selection && selection->selected == entity && !dead) {
        SoftGlow(renderer, feet, 30.f * scale, 15.f * scale, Color(255, 220, 90, 70));
        Ring(renderer, feet, 26.f * scale, 13.f * scale, {255, 224, 100, 255});
        Ring(renderer, feet, 25.f * scale, 12.4f * scale, {255, 224, 100, 150});
    }
    // Faction colour ring at the feet (secondary cue; the readable shape cue is the badge above the head).
    Ring(renderer, feet, 21.f * scale, 10.5f * scale, {faction.r, faction.g, faction.b, static_cast<Uint8>(shownDead ? 70 : 210)});
    const auto sprite = registry.get<FUnitArt>(entity).sprite;
    const SDL_FRect rect{center.x - kUnitSpriteSize * .5f * scale, center.y - kUnitHeadAboveFeet * scale,
                         kUnitSpriteSize * scale, kUnitSpriteSize * scale};
    const float headTop = center.y - UnitHeadAboveFeet(registry, entity) * scale;
    // The animation frames are anchored on the unit's feet, the same point the still picture is placed by.
    const bool animated = anim != nullptr &&
        DrawAnimFrame(registry, anim->player.clip, anim->player.frame, center, scale,
                      shownDead ? 215 : 255, UnitAnimTint(*anim));
    if (!animated) {
        FSpriteStyle style;
        style.alpha = dead ? 65 : 255;
        DrawDungeonSprite(registry, sprite, rect, style);
    }
    if (shownDead) return;
    const auto& health = registry.get<FHealth>(entity);
    const int shownHealth = anim != nullptr && anim->shownHealth >= 0 ? anim->shownHealth : health.current;
    SDL_FRect bar{center.x - kUnitHealthBarWidth * .5f * scale, headTop + kUnitHealthBarInset * scale,
                  kUnitHealthBarWidth * scale, kUnitHealthBarHeight * scale};
    SDL_SetRenderDrawColor(renderer, 6, 6, 10, 230);
    const SDL_FRect back{bar.x - 1.f, bar.y - 1.f, bar.w + 2.f, bar.h + 2.f};
    SDL_RenderFillRect(renderer, &back);
    bar.w *= std::clamp(static_cast<float>(shownHealth) / std::max(1, health.max), 0.f, 1.f);
    SDL_SetRenderDrawColor(renderer, faction.r, faction.g, faction.b, 255);
    SDL_RenderFillRect(renderer, &bar);

    // WP-24b: the faction shape cue is a small filled badge at the left end of the HP bar — above the head,
    // so the sprite can never cover it, and screen-axis-aligned so it can't be mistaken for a highlight
    // outline. Party = square, enemies = upward triangle; faction colour fill with a dark 1px outline.
    constexpr float kBadgeHeight = kUnitHealthBarHeight * 2.f;  // design px, tied to the bar
    constexpr float kBadgeGap = 2.f;
    const float badge = kBadgeHeight * scale;
    const glm::vec2 badgeAt{bar.x - kBadgeGap * scale - badge * .5f, bar.y + bar.h * .5f};
    const ui::FMarkerOutline shape =
        ui::FactionMarkerOutline(ui::FactionMarkerFor(party), badgeAt.x, badgeAt.y, badge * .5f, badge * .5f);
    SDL_FColor fill{static_cast<float>(faction.r) / 255.f, static_cast<float>(faction.g) / 255.f,
                    static_cast<float>(faction.b) / 255.f, 1.f};
    std::array<SDL_Vertex, 4> badgeVertices{};
    for (int index = 0; index < shape.count; ++index) {
        badgeVertices[static_cast<std::size_t>(index)] = {
            {shape.points[index].x, shape.points[index].y}, fill, {}};
    }
    constexpr int kSquareIndices[6]{0, 1, 2, 0, 2, 3};
    constexpr int kTriangleIndices[3]{0, 1, 2};
    SDL_RenderGeometry(renderer, nullptr, badgeVertices.data(), shape.count,
                       shape.count == 4 ? kSquareIndices : kTriangleIndices, shape.count == 4 ? 6 : 3);
    std::array<SDL_FPoint, 5> badgeOutline{};
    for (int index = 0; index < shape.count; ++index) {
        badgeOutline[static_cast<std::size_t>(index)] = {shape.points[index].x, shape.points[index].y};
    }
    badgeOutline[static_cast<std::size_t>(shape.count)] = badgeOutline[0];  // SDL_RenderLines is open
    SDL_SetRenderDrawColor(renderer, 8, 8, 12, 255);
    SDL_RenderLines(renderer, badgeOutline.data(), shape.count + 1);
}

// Everything that stands on the ground, in draw order: back to front, a higher x + y is nearer the camera.
struct FStanding {
    float depth{0.f};
    float column{0.f};
    entt::entity unit{entt::null};
    FPlacedProp prop;
};

} // namespace

// Every cell's upper-right triangle first, then every lower-left one: SDL's software renderer must never see two
// consecutive triangles that form a one-colour axis-aligned rectangle (it fills that with BLEND, not MOD).
std::vector<int> LightMeshIndices(int columns, int rows) {
    std::vector<int> indices;
    indices.reserve(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows) * 6);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int corner = row * (columns + 1) + column;
            const int below = corner + columns + 1;
            indices.insert(indices.end(), {corner, corner + 1, below + 1});
        }
    }
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int corner = row * (columns + 1) + column;
            const int below = corner + columns + 1;
            indices.insert(indices.end(), {corner, below + 1, below});
        }
    }
    return indices;
}

void DrawModulateMesh(SDL_Renderer* renderer, std::span<const SDL_FColor> colors, int columns, int rows, float width,
                      float height) {
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(colors.size());
    for (int row = 0; row <= rows; ++row) {
        for (int column = 0; column <= columns; ++column) {
            const SDL_FColor color = colors[static_cast<std::size_t>(row * (columns + 1) + column)];
            vertices.push_back({{width * static_cast<float>(column) / static_cast<float>(columns),
                                 height * static_cast<float>(row) / static_cast<float>(rows)},
                                color, {}});
        }
    }
    const std::vector<int> indices = LightMeshIndices(columns, rows);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD);
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                       static_cast<int>(indices.size()));
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

void EmberGridRenderSystem::render(entt::registry& registry) {
    const auto* rendererContext = registry.ctx().find<SDL_Renderer*>();
    const auto* grid = registry.ctx().find<FGridMetrics>();
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    if (!rendererContext || !*rendererContext || !grid || !terrain) return;
    auto* renderer = *rendererContext;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const auto width = static_cast<float>(window.width);
    const auto height = static_cast<float>(window.height);
    // The arena is fitted clear of the HUD clusters; without a layout there is nothing to fit it to.
    const auto isoView = ui::FindBattleIsoView(registry, *grid);
    if (!isoView) return;
    const FIsoView& view = *isoView;
    const float time = SceneSeconds(registry);  // simulated time, so a fixed-step recording flickers like real play

    const auto* scene = registry.ctx().find<FForestScene>();
    const auto props = scene ? PlaceSceneProps(*scene, *grid) : std::vector<FPlacedProp>{};
    const auto pools = scene ? BuildLightPools(*scene, props, view, *grid, time) : std::vector<FLightPool>{};
    const glm::vec3 objectAmbient = scene ? scene->objectAmbient : glm::vec3{1.f};

    DrawGround(registry, view, width, height);
    if (scene) {
        for (const auto& decal : scene->decals) {
            DrawFlat(registry, view, decal.sprite, view.corner(decal.u * static_cast<float>(grid->width),
                                                               decal.v * static_cast<float>(grid->height)));
        }
    }
    std::vector<FStanding> standing;
    standing.reserve(props.size() + 32);
    for (int y = 0; y < grid->height; ++y) {
        for (int x = 0; x < grid->width; ++x) {
            const auto tile = terrain->at(x, y);
            if (tile == ETile::Floor) continue;
            const glm::vec2 center = view.cellCenter(x, y);
            DrawFlat(registry, view, tile == ETile::Trap ? EDungeonSprite::SceneTileTrap : EDungeonSprite::SceneTileBlocked, center);
            if (tile == ETile::Wall) {
                standing.push_back({static_cast<float>(x + y), static_cast<float>(x), entt::null,
                                    {EDungeonSprite::ScenePillar, {x, y}, false}});
            }
        }
    }
    for (const auto& prop : props) {
        standing.push_back({static_cast<float>(prop.cell.x + prop.cell.y), static_cast<float>(prop.cell.x), entt::null, prop});
    }

    if (scene) {
        DrawLightMesh(renderer, pools, scene->ambient, view, width, height);
        DrawGroundSpill(renderer, *scene, props, view, time);
    }

    // Faint cell lines keep the grid readable without turning the ground into a board.
    for (int y = 0; y < grid->height; ++y) {
        for (int x = 0; x < grid->width; ++x) {
            if (terrain->at(x, y) != ETile::Wall) OutlineDiamond(renderer, view, x, y, {170, 200, 190, 22}, 0.f);
        }
    }

    const auto* selection = registry.ctx().find<FGridSelection>();
    if (selection) {
        for (const auto cell : selection->reachable) {
            FillDiamond(renderer, view, cell.x, cell.y, Color(70, 220, 255, 46));
            for (const float inset : ui::SelectionOutlineInsets(ui::ESelectionCue::Reachable)) {
                OutlineDiamond(renderer, view, cell.x, cell.y, {90, 225, 255, 150}, inset);
            }
        }
        for (const auto cell : selection->affected) {
            if (!IsInsideGrid(*grid, cell.x, cell.y)) continue;
            FillDiamond(renderer, view, cell.x, cell.y, Color(230, 70, 60, 84));
            // WP-12c: the hit forecast is a DOUBLE OUTLINE (inset 1 + 3) so the cue survives without colour;
            // the move range keeps its single cyan outline (SelectionOutlineInsets is the shared table).
            for (const float inset : ui::SelectionOutlineInsets(ui::ESelectionCue::Affected)) {
                OutlineDiamond(renderer, view, cell.x, cell.y, {255, 110, 90, 190}, inset);
            }
        }
        if (selection->hovered && IsInsideGrid(*grid, selection->hovered->x, selection->hovered->y)) {
            // The hovered cell keeps its double outline as a pointer-cursor affordance, not as a range cue:
            // it is the cell under the mouse, always on top, and no range verdict is drawn from it.
            const auto insets = ui::SelectionOutlineInsets(ui::ESelectionCue::Hovered);
            for (std::size_t stroke = 0; stroke < insets.size() && stroke < std::size(kHoverOutline); ++stroke) {
                OutlineDiamond(renderer, view, selection->hovered->x, selection->hovered->y, kHoverOutline[stroke],
                               insets[stroke]);
            }
        }
    }

    // WP-12: enemy-intent danger cells, the hovered unit's reachable strike cells and the move preview.
    // Everything is snapshot data; this renderer derives nothing (lead decision 1).
    if (const auto* info = FindCombatInfoSnapshot(registry); info != nullptr) {
        for (const FIntentBadge& badge : info->intents) {
            for (const FGridPosition& cell : badge.dangerCells) {
                if (!IsInsideGrid(*grid, cell.x, cell.y)) continue;
                FillDiamond(renderer, view, cell.x, cell.y, kIntentDangerFill);
                OutlineDiamond(renderer, view, cell.x, cell.y, kIntentDangerLine, kReachOutlineInset);
            }
        }
        for (const FIntentBadge& badge : info->intents) {
            for (const FGridPosition& cell : badge.reachCells) {
                if (!IsInsideGrid(*grid, cell.x, cell.y)) continue;
                FillDiamond(renderer, view, cell.x, cell.y, kIntentReachFill);
                OutlineDiamond(renderer, view, cell.x, cell.y, kIntentReachLine, kReachOutlineInset);
            }
        }
        for (const FGridPosition& cell : info->movePath) {
            if (!IsInsideGrid(*grid, cell.x, cell.y)) continue;
            FillDiamond(renderer, view, cell.x, cell.y, kMovePathFill);
            OutlineDiamond(renderer, view, cell.x, cell.y, kMovePathLine, kMovePathOutlineInset);
        }
        for (const FGridPosition& cell : info->trapCells) {
            if (!IsInsideGrid(*grid, cell.x, cell.y)) continue;
            FillDiamond(renderer, view, cell.x, cell.y, kMoveTrapFill);
            OutlineDiamond(renderer, view, cell.x, cell.y, kMoveTrapLine, kMoveTrapOutlineInset);
        }
        if (info->moveDestination && IsInsideGrid(*grid, info->moveDestination->x, info->moveDestination->y)) {
            OutlineDiamond(renderer, view, info->moveDestination->x, info->moveDestination->y, kMoveDestinationLine,
                           kReachOutlineInset);
        }
    }

    DrawBattleEffects(registry, view, true);

    // Contact shadows go down first so a unit in front of a prop is never dimmed by the prop's shadow.
    for (const auto& item : standing) {
        const auto feet = view.cellCenter(item.prop.cell.x, item.prop.cell.y);
        const float radius = DungeonSpriteSize(registry, item.prop.sprite).x * .42f * view.scale;
        SoftGlow(renderer, {feet.x, feet.y + 3.f * view.scale}, radius, radius * .5f, Color(0, 0, 0, 120));
    }

    for (const auto entity : registry.view<FGridPosition, FHealth, FUnitArt>()) {
        const auto cell = registry.get<FGridPosition>(entity);
        const auto* anim = registry.try_get<FUnitAnim>(entity);
        const glm::vec2 spot = anim != nullptr ? anim->visual : glm::vec2{static_cast<float>(cell.x), static_cast<float>(cell.y)};
        standing.push_back({spot.x + spot.y, spot.x, entity, {}});
    }
    std::ranges::sort(standing, [](const FStanding& a, const FStanding& b) {
        return a.depth != b.depth ? a.depth < b.depth : a.column < b.column;
    });
    for (const auto& item : standing) {
        if (item.unit != entt::null) DrawUnit(registry, renderer, view, selection, item.unit);
        else DrawProp(registry, view, item.prop, pools, objectAmbient, time);
    }

    if (scene) DrawSourceGlows(renderer, *scene, props, view, time);
    DrawBattleEffects(registry, view, false);
    DrawMotes(renderer, view, *grid, width, height, time);
    DrawVignette(renderer, width, height);
}

} // namespace game::ecs
