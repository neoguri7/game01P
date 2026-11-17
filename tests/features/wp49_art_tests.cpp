// WP-49 enemy identity: every unit has its own sprite, every skill its own icon and effect, the ten new units
// their five clips, and a tall animated unit lifts its health bar. Shipped content and shipped art are used on
// purpose (ShipAssetRoots): these tests guard what ships, not the frozen fixture.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "core/AnimationLibrary.h"
#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "gameplay/data/FContentRegistry.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FIsoView.h"
#include "presentation/FUnitFrame.h"

namespace {

constexpr std::array<const char*, 10> kNewUnits{"brute", "bone_guard", "slinger", "chanter", "mortar",
                                                "ash_priest", "guardian_heart", "cinder_warden", "chain_warden",
                                                "ash_hierophant"};
constexpr std::array<const char*, 5> kActions{"idle", "walk", "attack", "hit", "death"};

nlohmann::json ReadJson(const char* path) {
    std::ifstream file(path);
    CHECK(file.good());
    return nlohmann::json::parse(file);
}

std::vector<std::string> IdsOf(const char* path, const char* key) {
    std::vector<std::string> ids;
    for (const auto& row : ReadJson(path).at(key)) ids.push_back(row.at("id").get<std::string>());
    return ids;
}

/// Shipped content in the registry (no renderer).
void LoadShippedContent(entt::registry& registry) {
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    auto content = game::gameplay::FContentRegistry::load(registry.ctx().get<game::FAssetManager>());
    CHECK(content.has_value());
    registry.ctx().emplace<game::gameplay::FContentRegistry>(std::move(*content));
}

/// Shipped content plus a software renderer, as dungeon_tests::BattleAnimations builds it.
struct FRenderedWorld {
    entt::registry registry;
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{
        SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888), &SDL_DestroySurface};
    FRendererPtr renderer;

    FRenderedWorld() {
        CHECK(surface != nullptr);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        CHECK(renderer != nullptr);
        LoadShippedContent(registry);
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        registry.ctx().emplace<SDL_Renderer*>(renderer.get());
    }
};

void EveryUnitHasItsOwnSprite() {
    entt::registry registry;
    LoadShippedContent(registry);
    const auto ids = IdsOf("assets/data/units.json", "units");
    CHECK(ids.size() == 24);
    std::vector<game::FUnitPlacement> placements;
    for (const auto& id : ids) {
        game::FUnitPlacement placement;
        placement.id = id;
        placement.unitTemplateId = id;
        placement.entity = registry.create();
        placements.push_back(std::move(placement));
    }
    game::AssignDungeonSprites(registry, placements);
    std::set<std::size_t> seen;
    for (const auto& placement : placements) {
        const auto* art = registry.try_get<game::FUnitArt>(placement.entity);
        CHECK(art != nullptr);
        seen.insert(static_cast<std::size_t>(art->sprite));
    }
    CHECK(seen.size() == 24);

    const std::array<game::EDungeonSprite, 10> expected{
        game::EDungeonSprite::Brute, game::EDungeonSprite::BoneGuard, game::EDungeonSprite::Slinger,
        game::EDungeonSprite::Chanter, game::EDungeonSprite::Mortar, game::EDungeonSprite::AshPriest,
        game::EDungeonSprite::GuardianHeart, game::EDungeonSprite::CinderWarden,
        game::EDungeonSprite::ChainWarden, game::EDungeonSprite::AshHierophant};
    for (std::size_t i = 0; i < kNewUnits.size(); ++i) {
        const std::string id = std::string("enemy.") + kNewUnits[i];
        const auto found = std::ranges::find(placements, id, &game::FUnitPlacement::id);
        CHECK(found != placements.end());
        CHECK(registry.get<game::FUnitArt>(found->entity).sprite == expected[i]);
    }
}

void ShippedBattleAnimCoversEveryUnitAndSkill() {
    FRenderedWorld world;
    CHECK(game::FAnimationLibrary::Initialize(world.registry));
    const auto* library = game::FAnimationLibrary::Find(world.registry);
    CHECK(library != nullptr);
    const auto& content = world.registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto parsed = game::ParseBattleAnim(ReadJson("assets/data/battle_anim.json"), *library, content);
    CHECK(parsed.has_value());
    CHECK(parsed->sets.size() == 24);
    CHECK(parsed->skills.size() == 37);
    for (const auto& id : IdsOf("assets/data/units.json", "units")) CHECK(parsed->setByUnit.contains(id));
    for (const auto& id : IdsOf("assets/data/skills.json", "skills")) CHECK(parsed->skills.contains(id));
}

void EverySkillHasAnIcon() {
    const auto ids = IdsOf("assets/data/skills.json", "skills");
    CHECK(ids.size() == 37);
    std::set<std::size_t> seen;
    for (const auto& id : ids) {
        const auto icon = game::SkillIconSprite(id);
        CHECK(icon.has_value());
        seen.insert(static_cast<std::size_t>(*icon));
    }
    CHECK(seen.size() == 37);
}

void ShippedArtLoadsAtDeclaredSizes() {
    FRenderedWorld world;
    CHECK(game::InitializeDungeonArt(world.registry));
}

void NewUnitClipsAreInTheLibrary() {
    FRenderedWorld world;
    CHECK(game::FAnimationLibrary::Initialize(world.registry));
    const auto* library = game::FAnimationLibrary::Find(world.registry);
    CHECK(library != nullptr);
    for (const char* unit : kNewUnits) {
        for (const char* action : kActions) {
            const auto id = library->findClip(std::string("unit.") + unit + "." + action);
            CHECK(id.has_value());
            const auto* clip = library->get(*id);
            CHECK(clip != nullptr && !clip->frames.empty());
            const std::string name = action;
            const bool looping = name == "idle" || name == "walk";
            CHECK(clip->loop == looping);
        }
    }
}

void TallUnitsLiftTheirBars() {
    FRenderedWorld world;
    auto& registry = world.registry;
    CHECK(game::FAnimationLibrary::Initialize(registry));
    CHECK(game::InitializeBattleAnim(registry));
    const auto& data = registry.ctx().get<game::FBattleAnimData>();

    const auto bare = registry.create();
    CHECK(game::UnitHeadAboveFeet(registry, bare) == 52.f);
    const auto warrior = registry.create();
    registry.emplace<game::FUnitAnim>(warrior).set = data.setByUnit.at("char.warrior");
    CHECK(game::UnitHeadAboveFeet(registry, warrior) == 52.f);

    const auto clips = ReadJson("assets/anim/clips.json").at("clips");
    bool anyTall = false;
    for (const char* unit : kNewUnits) {
        const std::string clipId = std::string("unit.") + unit + ".idle";
        float anchorY = -1.f;
        for (const auto& row : clips)
            if (row.at("id") == clipId) anchorY = row.at("anchor_y").get<float>();
        CHECK(anchorY >= 0.f);
        const auto entity = registry.create();
        registry.emplace<game::FUnitAnim>(entity).set = data.setByUnit.at(std::string("enemy.") + unit);
        const float expected = std::max(52.f, anchorY);
        CHECK(game::UnitHeadAboveFeet(registry, entity) == expected);
        anyTall = anyTall || expected > 52.f;
    }
    CHECK(anyTall);
}

void ZoneAndImpactEffectsEnd() {
    const auto clips = ReadJson("assets/anim/clips.json").at("clips");
    const auto loops = [&](const std::string& id) {
        for (const auto& row : clips)
            if (row.at("id") == id) return row.at("loop").get<bool>();
        CHECK(false);  // the named clip is missing
        return true;
    };
    const auto check = [&](const nlohmann::json& effect) {
        const std::string kind = effect.at("kind").get<std::string>();
        if (kind == "impact" || kind == "zone") CHECK(!loops(effect.at("clip").get<std::string>()));
        if (effect.contains("impact")) CHECK(!loops(effect.at("impact").get<std::string>()));
    };
    const auto document = ReadJson("assets/data/battle_anim.json");
    for (const auto& [id, effect] : document.at("skills").items()) check(effect);
    check(document.at("default_effect"));
}

}  // namespace

int main() {
    return game_test::RunTests("wp49_art", {
        {"EveryUnitHasItsOwnSprite", EveryUnitHasItsOwnSprite},
        {"ShippedBattleAnimCoversEveryUnitAndSkill", ShippedBattleAnimCoversEveryUnitAndSkill},
        {"EverySkillHasAnIcon", EverySkillHasAnIcon},
        {"ShippedArtLoadsAtDeclaredSizes", ShippedArtLoadsAtDeclaredSizes},
        {"NewUnitClipsAreInTheLibrary", NewUnitClipsAreInTheLibrary},
        {"TallUnitsLiftTheirBars", TallUnitsLiftTheirBars},
        {"ZoneAndImpactEffectsEnd", ZoneAndImpactEffectsEnd},
    });
}
