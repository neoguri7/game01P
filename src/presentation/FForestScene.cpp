#include "presentation/FForestScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/Logger.h"

namespace game {
namespace {

using FError = std::unexpected<std::string>;

template <class T>
using FNamed = std::pair<std::string_view, T>;

constexpr std::array<FNamed<EDungeonSprite>, 9> kProps{{
    {"wall_a", EDungeonSprite::SceneWallA}, {"wall_b", EDungeonSprite::SceneWallB},
    {"wall_c", EDungeonSprite::SceneWallC}, {"arch", EDungeonSprite::SceneArch},
    {"cliff", EDungeonSprite::SceneCliff}, {"pillar", EDungeonSprite::ScenePillar},
    {"brazier", EDungeonSprite::SceneBrazier}, {"crystal", EDungeonSprite::SceneCrystal},
    {"tree_dead", EDungeonSprite::SceneTree},
}};

constexpr std::array<FNamed<EDungeonSprite>, 2> kDecals{{
    {"decal_moss", EDungeonSprite::SceneDecalMoss}, {"decal_rubble", EDungeonSprite::SceneDecalRubble},
}};

constexpr std::array<FNamed<ESceneEdge>, 4> kEdges{{
    {"back_left", ESceneEdge::BackLeft}, {"back_right", ESceneEdge::BackRight},
    {"front_left", ESceneEdge::FrontLeft}, {"front_right", ESceneEdge::FrontRight},
}};

template <class T, std::size_t Count>
std::optional<T> FindNamed(const std::array<FNamed<T>, Count>& table, std::string_view name) {
    const auto found = std::ranges::find(table, name, &FNamed<T>::first);
    if (found == table.end()) return std::nullopt;
    return found->second;
}

std::expected<glm::vec3, std::string> ParseColor(const nlohmann::json& value, const std::string& where) {
    if (!value.is_array() || value.size() != 3) return FError(where + " must be [r, g, b]");
    glm::vec3 color{};
    for (int channel = 0; channel < 3; ++channel) {
        const int level = value[static_cast<std::size_t>(channel)].get<int>();
        if (level < 0 || level > 255) return FError(where + " channels must be 0 to 255");
        color[channel] = static_cast<float>(level) / 255.f;
    }
    return color;
}

std::expected<FSceneLight, std::string> ParseLight(const nlohmann::json& value, const std::string& where) {
    const auto color = ParseColor(value.at("color"), where + ".color");
    if (!color) return FError(color.error());
    FSceneLight light;
    light.color = *color;
    light.radius = value.at("radius").get<float>();
    light.glow = value.value("glow", 0.f);
    light.height = value.value("height", 0.f);
    light.flicker = value.value("flicker", 0.f);
    light.spill = value.value("spill", 0.f);
    if (light.radius <= 0.f) return FError(where + ".radius must be positive");
    if (light.glow < 0.f || light.height < 0.f) return FError(where + " glow and height must not be negative");
    if (light.flicker < 0.f || light.flicker >= 1.f) return FError(where + ".flicker must be from 0 up to 1");
    if (light.spill < 0.f || light.spill > 1.f) return FError(where + ".spill must be from 0 to 1");
    return light;
}

std::expected<FScenePropRun, std::string> ParsePropRun(const nlohmann::json& value, const std::string& where) {
    FScenePropRun run;
    const auto& sprites = value.at("sprites");
    if (!sprites.is_array() || sprites.empty()) return FError(where + ".sprites must list at least one prop");
    for (const auto& name : sprites) {
        const auto sprite = FindNamed(kProps, name.get<std::string>());
        if (!sprite) return FError(where + ".sprites names unknown prop '" + name.get<std::string>() + "'");
        run.sprites.push_back(*sprite);
    }
    const auto edgeName = value.at("edge").get<std::string>();
    const auto edge = FindNamed(kEdges, edgeName);
    if (!edge) return FError(where + ".edge '" + edgeName + "' is not a known edge");
    run.edge = *edge;
    run.offset = value.at("offset").get<int>();
    if (run.offset < 0) return FError(where + ".offset must not be negative");
    if (value.contains("at") == (value.contains("from") || value.contains("to"))) {
        return FError(where + " needs either at, or from and to");
    }
    run.from = value.contains("at") ? value.at("at").get<float>() : value.at("from").get<float>();
    run.to = value.contains("at") ? run.from : value.at("to").get<float>();
    if (run.from > run.to) return FError(where + ".from must not exceed to");
    run.every = value.value("every", 1);
    if (run.every <= 0) return FError(where + ".every must be positive");
    run.flip = value.value("flip", false);
    return run;
}

std::expected<FForestScene, std::string> Parse(const nlohmann::json& document) {
    if (document.at("schema_version").get<int>() != 1) return FError("schema_version must be 1");

    FForestScene scene;
    const auto ambient = ParseColor(document.at("ambient"), "ambient");
    if (!ambient) return FError(ambient.error());
    scene.ambient = *ambient;
    const auto objectAmbient = ParseColor(document.at("object_ambient"), "object_ambient");
    if (!objectAmbient) return FError(objectAmbient.error());
    scene.objectAmbient = *objectAmbient;

    const auto& lights = document.at("lights");
    const auto arena = ParseLight(lights.at("arena"), "lights.arena");
    if (!arena) return FError(arena.error());
    const auto brazier = ParseLight(lights.at("brazier"), "lights.brazier");
    if (!brazier) return FError(brazier.error());
    const auto crystal = ParseLight(lights.at("crystal"), "lights.crystal");
    if (!crystal) return FError(crystal.error());
    scene.arena = *arena;
    scene.brazier = *brazier;
    scene.crystal = *crystal;

    for (const auto& value : document.at("decals")) {
        const auto name = value.at("sprite").get<std::string>();
        const auto sprite = FindNamed(kDecals, name);
        if (!sprite) return FError("decals names unknown decal '" + name + "'");
        scene.decals.push_back({*sprite, value.at("u").get<float>(), value.at("v").get<float>()});
    }
    const auto& props = document.at("props");
    for (std::size_t index = 0; index < props.size(); ++index) {
        auto run = ParsePropRun(props[index], "props[" + std::to_string(index) + "]");
        if (!run) return FError(run.error());
        scene.props.push_back(std::move(*run));
    }
    return scene;
}

FGridPosition EdgeCell(ESceneEdge edge, const FGridMetrics& metrics, int along, int offset) {
    switch (edge) {
    case ESceneEdge::BackLeft: return {-1 - offset, along};
    case ESceneEdge::BackRight: return {along, -1 - offset};
    case ESceneEdge::FrontLeft: return {along, metrics.height + offset};
    case ESceneEdge::FrontRight: return {metrics.width + offset, along};
    }
    return {};
}

} // namespace

std::expected<FForestScene, std::string> ParseForestScene(const FAssetDigest& digest) {
    try {
        return Parse(digest.json());
    } catch (const nlohmann::json::exception& error) {
        return FError(std::string("forest scene is malformed: ") + error.what());
    }
}

std::vector<FPlacedProp> PlaceSceneProps(const FForestScene& scene, const FGridMetrics& metrics) {
    std::vector<FPlacedProp> placed;
    for (const auto& run : scene.props) {
        const bool alongHeight = run.edge == ESceneEdge::BackLeft || run.edge == ESceneEdge::FrontRight;
        const auto last = static_cast<float>((alongHeight ? metrics.height : metrics.width) - 1);
        const int first = static_cast<int>(std::lround(run.from * last));
        const int final = static_cast<int>(std::lround(run.to * last));
        std::size_t index = 0;
        for (int along = first; along <= final; along += run.every, ++index) {
            placed.push_back({run.sprites[index % run.sprites.size()], EdgeCell(run.edge, metrics, along, run.offset), run.flip});
        }
    }
    return placed;
}

bool InitializeForestScene(entt::registry& registry) {
    const auto* assets = registry.ctx().find<FAssetManager>();
    if (assets == nullptr) {
        LOG_ERROR("The forest scene needs the asset manager.");
        return false;
    }
    const auto digest = assets->digest("data/scene_forest");
    if (!digest) return false;
    auto scene = ParseForestScene(*digest);
    if (!scene) {
        LOG_ERROR("data/scene_forest.json is unusable: {}", scene.error());
        return false;
    }
    registry.ctx().erase<FForestScene>();
    registry.ctx().emplace<FForestScene>(std::move(*scene));
    return true;
}

} // namespace game
