#include "ecs/systems/EmberGridRenderSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "core/EngineState.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTerrain.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FIsoView.h"
#include "presentation/FUiFrame.h"

namespace game::ecs {
namespace {

constexpr float kTau = 6.2831853f;
// Where the ground tile's diamond face sits inside its 64x64 image, and how far below the cell center
// an obstacle's or unit's feet rest.
constexpr float kFaceCenterRow = 26.f;
constexpr float kFeetOffset = 8.f;
constexpr int kForestMargin = 5;

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

void Quad(SDL_Renderer* renderer, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_FColor ca, SDL_FColor cb,
          SDL_FColor cc, SDL_FColor cd) {
    const SDL_Vertex vertices[4]{{a, ca, {}}, {b, cb, {}}, {c, cc, {}}, {d, cd, {}}};
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
    Quad(renderer, {top.x, top.y}, {right.x, right.y}, {bottom.x, bottom.y}, {left.x, left.y}, color, color, color, color);
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

// Dark teal forest night: vertical gradient and a pool of cold light over the arena.
void DrawForestBackdrop(SDL_Renderer* renderer, const FIsoView& view, const FGridMetrics& grid, float width, float height) {
    Quad(renderer, {0.f, 0.f}, {width, 0.f}, {width, height}, {0.f, height}, Color(3, 8, 9, 255), Color(3, 8, 9, 255),
         Color(10, 18, 15, 255), Color(10, 18, 15, 255));
    const auto mid = view.corner(static_cast<float>(grid.width) * .5f, static_cast<float>(grid.height) * .5f);
    const float span = static_cast<float>(grid.width + grid.height) * kIsoHalfWidth * view.scale;
    SoftGlow(renderer, mid, span * .75f, span * .42f, Color(40, 92, 78, 70));
    SoftGlow(renderer, mid, span * .38f, span * .22f, Color(120, 150, 90, 28));
}

// Drifting spores above the arena.
void DrawMotes(SDL_Renderer* renderer, const FIsoView& view, const FGridMetrics& grid, float width, float height) {
    const float time = static_cast<float>(SDL_GetTicks()) * .001f;
    const auto mid = view.corner(static_cast<float>(grid.width) * .5f, static_cast<float>(grid.height) * .5f);
    const float dot = std::max(1.f, std::floor(view.scale));
    for (int index = 0; index < 28; ++index) {
        const auto h = Hash(index, 7);
        const float phase = static_cast<float>(h % 628) * .01f;
        const float baseX = mid.x + (static_cast<float>(h % 1000) / 1000.f - .5f) * width * .85f;
        const float baseY = mid.y + (static_cast<float>((h >> 10) % 1000) / 1000.f - .5f) * height * .6f;
        const float x = baseX + std::sin(time * .4f + phase) * 14.f * view.scale;
        const float y = baseY - std::fmod(time * (3.f + static_cast<float>(h % 5)) + phase * 9.f, 40.f) * view.scale;
        const float pulse = .5f + .5f * std::sin(time * 1.3f + phase * 3.f);
        const SDL_FRect mote{std::floor(x), std::floor(y), dot, dot};
        SDL_SetRenderDrawColor(renderer, 130, 255, 205, static_cast<Uint8>(40.f + 120.f * pulse));
        SDL_RenderFillRect(renderer, &mote);
    }
}

void DrawVignette(SDL_Renderer* renderer, float width, float height) {
    for (int ring = 0; ring < 16; ++ring) {
        const float inset = static_cast<float>(ring) * 6.f;
        const SDL_FRect edge{inset, inset, width - inset * 2.f, height - inset * 2.f};
        SDL_SetRenderDrawColor(renderer, 0, 0, 4, static_cast<Uint8>(110 - ring * 6));
        SDL_RenderRect(renderer, &edge);
        const SDL_FRect thick{inset + 1.f, inset + 1.f, edge.w - 2.f, edge.h - 2.f};
        SDL_RenderRect(renderer, &thick);
    }
}

SDL_FRect FloorRect(const FIsoView& view, int x, int y) {
    const auto center = view.cellCenter(x, y);
    return {center.x - 32.f * view.scale, center.y - kFaceCenterRow * view.scale, 64.f * view.scale, 64.f * view.scale};
}

SDL_FRect StandingRect(entt::registry& registry, const FIsoView& view, int x, int y, EDungeonSprite sprite) {
    const auto center = view.cellCenter(x, y);
    const auto size = DungeonSpriteSize(registry, sprite);
    return {center.x - size.x * .5f * view.scale, center.y + kFeetOffset * view.scale - size.y * view.scale,
            size.x * view.scale, size.y * view.scale};
}

struct FUnitDraw {
    entt::entity entity;
    FGridPosition cell;
};

} // namespace

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
    // The arena lives in the window the HUD frame leaves open; without the frame there is no layout to draw into.
    const auto isoView = ui::FindBattleIsoView(registry, *grid);
    if (!isoView) return;
    const FIsoView& view = *isoView;
    const float scale = view.scale;

    DrawForestBackdrop(renderer, view, *grid, width, height);

    // Ground: the arena sits in a wider patch of forest floor that fades into the dark.
    const int lowest = -kForestMargin;
    const int highest = grid->width + kForestMargin;
    for (int sum = lowest * 2; sum <= highest * 2; ++sum) {
        for (int x = lowest; x < highest; ++x) {
            const int y = sum - x;
            if (y < lowest || y >= highest) continue;
            const bool inside = IsInsideGrid(*grid, x, y);
            const auto rect = FloorRect(view, x, y);
            if (rect.x > width || rect.x + rect.w < 0.f || rect.y > height || rect.y + rect.h < 0.f) continue;
            if (!inside) {
                const int distance = std::max({-x, -y, x - grid->width + 1, y - grid->height + 1});
                const int alpha = 150 - distance * 28;
                if (alpha > 0) DrawDungeonSprite(registry, IsoFloorVariant(x, y), rect, static_cast<Uint8>(alpha));
                continue;
            }
            const auto tile = terrain->at(x, y);
            DrawDungeonSprite(registry, tile == ETile::Trap ? EDungeonSprite::IsoTrap :
                              tile == ETile::Wall ? EDungeonSprite::IsoRoot : IsoFloorVariant(x, y), rect);
        }
    }

    // Cold light and faint cell lines so the playable clearing reads against the surrounding forest.
    for (int y = 0; y < grid->height; ++y) {
        for (int x = 0; x < grid->width; ++x) {
            if (terrain->at(x, y) == ETile::Wall) continue;
            FillDiamond(renderer, view, x, y, Color(120, 210, 180, 16));
            OutlineDiamond(renderer, view, x, y, {20, 40, 38, 110}, 0.f);
        }
    }

    const auto* selection = registry.ctx().find<FGridSelection>();
    if (selection) {
        for (const auto cell : selection->reachable) {
            FillDiamond(renderer, view, cell.x, cell.y, Color(70, 220, 255, 46));
            OutlineDiamond(renderer, view, cell.x, cell.y, {90, 225, 255, 150}, 1.f);
        }
        for (const auto cell : selection->affected) {
            if (!IsInsideGrid(*grid, cell.x, cell.y)) continue;
            FillDiamond(renderer, view, cell.x, cell.y, Color(230, 70, 60, 84));
            OutlineDiamond(renderer, view, cell.x, cell.y, {255, 110, 90, 190}, 1.f);
        }
        if (selection->hovered && IsInsideGrid(*grid, selection->hovered->x, selection->hovered->y)) {
            OutlineDiamond(renderer, view, selection->hovered->x, selection->hovered->y, {255, 224, 120, 230}, 0.f);
            OutlineDiamond(renderer, view, selection->hovered->x, selection->hovered->y, {255, 224, 120, 120}, 2.f);
        }
    }

    std::vector<FUnitDraw> units;
    for (const auto entity : registry.view<FGridPosition, FHealth, FUnitArt>()) {
        units.push_back({entity, registry.get<FGridPosition>(entity)});
    }
    std::ranges::sort(units, [](const FUnitDraw& a, const FUnitDraw& b) {
        return a.cell.x + a.cell.y != b.cell.x + b.cell.y ? a.cell.x + a.cell.y < b.cell.x + b.cell.y : a.cell.x < b.cell.x;
    });

    // Standing things, back to front: forest scenery beyond the arena, obstacles, then units.
    std::size_t nextUnit = 0;
    for (int sum = lowest * 2; sum <= highest * 2; ++sum) {
        for (int x = lowest; x < highest; ++x) {
            const int y = sum - x;
            if (y < lowest || y >= highest) continue;
            if (IsInsideGrid(*grid, x, y)) {
                if (terrain->at(x, y) == ETile::Wall) {
                    auto sprite = IsoObstacleVariant(x, y);
                    // Tall trees on the near edges would hide the clearing, so those become low rocks and thorns.
                    const bool nearEdge = x == grid->width - 1 || y == grid->height - 1;
                    if (nearEdge && sprite == EDungeonSprite::IsoTree) {
                        sprite = Hash(x, y) % 2 == 0 ? EDungeonSprite::IsoBoulders : EDungeonSprite::IsoThorns;
                    }
                    DrawDungeonSprite(registry, sprite, StandingRect(registry, view, x, y, sprite));
                }
                continue;
            }
            const int distance = std::max({-x, -y, x - grid->width + 1, y - grid->height + 1});
            if (distance < 2 || distance > 4 || Hash(x, y) % 5 != 0) continue;
            const int alpha = 190 - distance * 40;
            const bool inFront = x >= grid->width || y >= grid->height;
            const auto sprite = inFront || Hash(x, y) % 7 == 0 ? EDungeonSprite::IsoBoulders : EDungeonSprite::IsoTree;
            DrawDungeonSprite(registry, sprite, StandingRect(registry, view, x, y, sprite), static_cast<Uint8>(alpha));
        }
        for (; nextUnit < units.size() && units[nextUnit].cell.x + units[nextUnit].cell.y <= sum; ++nextUnit) {
            const auto entity = units[nextUnit].entity;
            const auto cell = units[nextUnit].cell;
            const auto center = view.cellCenter(cell.x, cell.y);
            const bool dead = registry.all_of<FDead>(entity);
            const bool party = registry.all_of<FPartyMember>(entity);
            const SDL_Color faction = party ? SDL_Color{90, 150, 255, 255} : SDL_Color{235, 85, 80, 255};
            const glm::vec2 feet{center.x, center.y + 2.f * scale};
            if (selection && selection->selected == entity && !dead) {
                SoftGlow(renderer, feet, 30.f * scale, 15.f * scale, Color(255, 220, 90, 70));
                Ring(renderer, feet, 26.f * scale, 13.f * scale, {255, 224, 100, 255});
                Ring(renderer, feet, 25.f * scale, 12.4f * scale, {255, 224, 100, 150});
            }
            Ring(renderer, feet, 21.f * scale, 10.5f * scale, {faction.r, faction.g, faction.b, static_cast<Uint8>(dead ? 70 : 210)});
            const auto sprite = registry.get<FUnitArt>(entity).sprite;
            const SDL_FRect rect{center.x - 32.f * scale, center.y - 52.f * scale, 64.f * scale, 64.f * scale};
            DrawDungeonSprite(registry, sprite, rect, dead ? 65 : 255);
            if (!dead) {
                const auto& health = registry.get<FHealth>(entity);
                SDL_FRect bar{center.x - 17.f * scale, rect.y + 2.f * scale, 34.f * scale, 4.f * scale};
                SDL_SetRenderDrawColor(renderer, 6, 6, 10, 230);
                const SDL_FRect back{bar.x - 1.f, bar.y - 1.f, bar.w + 2.f, bar.h + 2.f};
                SDL_RenderFillRect(renderer, &back);
                bar.w *= std::clamp(static_cast<float>(health.current) / std::max(1, health.max), 0.f, 1.f);
                SDL_SetRenderDrawColor(renderer, faction.r, faction.g, faction.b, 255);
                SDL_RenderFillRect(renderer, &bar);
            }
        }
    }

    DrawMotes(renderer, view, *grid, width, height);
    DrawVignette(renderer, width, height);
}

} // namespace game::ecs
