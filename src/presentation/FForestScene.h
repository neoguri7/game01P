#pragma once

#include <expected>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "core/FAssetDigest.h"
#include "gameplay/grid/FGridTypes.h"
#include "presentation/FDungeonArt.h"

namespace game {

/// A side of the grid. The back edges (lowest x, lowest y) meet behind the arena on screen, so tall scenery
/// stands there; the front edges face the camera, so scenery there must keep clear of the cells.
enum class ESceneEdge { BackLeft, BackRight, FrontLeft, FrontRight };

/// One line of props along an edge. Everything is relative to the edge, so a room of any size gets the scene.
struct FScenePropRun {
    std::vector<EDungeonSprite> sprites;  // cycled along the run
    ESceneEdge edge{ESceneEdge::BackLeft};
    int offset{0};      // cells beyond the edge; 0 is the row touching the grid
    float from{0.f};    // position along the edge, 0 to 1; beyond that reaches past the corners
    float to{0.f};
    int every{1};       // cells between two props
    bool flip{false};   // mirrored: the art faces the left back edge
};

/// A flat ground picture at a fraction of the grid; outside 0..1 lies beyond the grid.
struct FSceneDecal {
    EDungeonSprite sprite{EDungeonSprite::SceneDecalMoss};
    float u{0.f};
    float v{0.f};
};

struct FSceneLight {
    glm::vec3 color{0.f};  // 0..1, added to the ambient light at the centre of the pool
    float radius{1.f};     // cells; the arena light uses a fraction of the grid's longer side
    float glow{0.f};       // radius of the bloom around the source, in art pixels
    float height{0.f};     // how far above the feet the source sits, in art pixels
    float flicker{0.f};    // share of the strength that moves over time
    float spill{0.f};      // colour added to the ground at the source, 0 to 1; multiplying alone cannot pass the art's value
};

/// Presentation of the forest battlefield (assets/data/scene_forest.json). Gameplay never reads this.
struct FForestScene {
    glm::vec3 ambient{0.f};        // light of the ground far from every source
    glm::vec3 objectAmbient{0.f};  // lowest light on walls and props, so they stay above the ground in value
    FSceneLight arena;
    FSceneLight brazier;
    FSceneLight crystal;
    std::vector<FSceneDecal> decals;
    std::vector<FScenePropRun> props;
};

struct FPlacedProp {
    EDungeonSprite sprite{EDungeonSprite::ScenePillar};
    FGridPosition cell{};
    bool flip{false};
};

/// Reads and validates a scene document; the error names the first problem found.
[[nodiscard]] std::expected<FForestScene, std::string> ParseForestScene(const FAssetDigest& digest);

/// The scene's props on a grid of this size, all outside the grid.
[[nodiscard]] std::vector<FPlacedProp> PlaceSceneProps(const FForestScene& scene, const FGridMetrics& metrics);

/// Loads data/scene_forest into the EnTT context. False when the document is unusable (the reason is logged).
bool InitializeForestScene(entt::registry& registry);

} // namespace game
