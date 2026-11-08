// WP-14a: the encounter composition table (`assets/data/encounters.json` — an OPTIONAL file), the role
// predicates (a role is a skill combination, never an AI profile), the single placement rule extension
// (`FRoomRequest.family`/extra enemies/leader prefix), the tile-commit artillery gate (`intent_commit_mix`
// by the FIntent mode rule), the deterministic depth/theme pool (`introduced_at_depth` + tags), the ctx
// `FRoomPlan` (only `BuildDungeonBattlefield` ever emplaces it), the dungeon-init coverage check, the
// shipped six-encounter table and the sprite mapping (`spriteId` -> EDungeonSprite, unknown `enemy.*` = Imp).
//
// Everything the tests expect comes from the frozen fixture the boot actually reads, from the shipped
// table itself, or from the documented rule under test. Temp roots are used when the fixture intentionally
// ships the EMPTY table (legacy generation path); the derived-units cases add only `skill.imp_firebomb`
// to the fixture `enemy.imp_shaman` so the artillery gate can be observed both ways.
//
// Sections: schema/parse (EncounterSchemaValidated), boot-time role checks (IntentCommitMixValidatedAtBoot,
// RolePredicatesValidated), generation (SlotsDetermineRoles, RequestExtensionFields), legacy path
// (LegacyGenerationWhenEncountersAbsent), boss/harness wiring (BossSlotsUseGenerationPool,
// RoomPlanEmplacedByBattlefieldOnly), the pool rules (IntroducedAtDepth, Deterministic), the boot gates
// (CompositionUnchanged, CoverageValidatedAtDungeonInit, ShippedEncountersBoot) and the art mapping
// (SpriteFieldResolves, UnknownEnemyFallsBackToImp).

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FEncounterContent.h"
#include "gameplay/dungeon/FRoomGenerator.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "presentation/FDungeonArt.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

// --- scratch files (the event_framework pattern) ------------------------------------------------

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string UniquePath(const char* stem) { return "out/" + UniqueStem(stem) + ".json"; }

json ReadJsonFile(const std::string& path) {
    std::ifstream file(path);
    CHECK(file.good());
    json document;
    file >> document;
    return document;
}

json FixtureDoc(const char* file) { return ReadJsonFile(FixtureDataPath(file)); }

// --- a temporary asset root layered over the fixture + ship roots -------------------------------
//
// Roots() = {temp, fixture, ship}: un-shadowed documents keep the frozen fixture, while the temp root
// carries the authored encounters.json (and the derived units.json where a test needs the firebomb).

struct FTempAssets {
    std::filesystem::path root;

    explicit FTempAssets(const char* stem) {
        root = std::filesystem::path("out") / UniqueStem(stem);
        std::filesystem::create_directories(root / "data");
    }

    ~FTempAssets() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    FTempAssets(const FTempAssets&) = delete;
    FTempAssets& operator=(const FTempAssets&) = delete;

    void Copy(const std::string& file) {
        std::error_code error;
        std::filesystem::copy_file(
            FixtureDataPath(file), root / "data" / file, std::filesystem::copy_options::overwrite_existing, error);
        CHECK(!error);
    }

    void Write(const std::string& file, const json& document) {
        std::ofstream out(root / "data" / file, std::ios::trunc);
        CHECK(out.good());
        out << document.dump(2);
    }

    std::vector<std::string> Roots() const { return {root.string(), kTestFixtureRoot, kShipAssetRoot}; }
};

// --- a booted registry with its own checkpoint and save slot (camp_tests pattern) ---------------

struct FBoot {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;
    bool content{false};
    bool dungeon{false};

    explicit FBoot(std::string path, std::vector<std::string> roots) : checkpoint(std::move(path)) {
        game::FAssetManager::Initialize(registry, std::move(roots));
        content = game::gameplay::FGameplayServices::Initialize(registry);
        if (content) dungeon = game::InitializeDungeon(registry, checkpoint);
        if (dungeon) runSave = registry.ctx().get<game::FDungeonSave>().path;
    }

    ~FBoot() {
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave + ".tmp", error);
    }

    FBoot(const FBoot&) = delete;
    FBoot& operator=(const FBoot&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
};

/// The content boot alone (no dungeon): false = a loader refused something.
bool BootsGameplay(const std::vector<std::string>& roots) {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, roots);
    return game::gameplay::FGameplayServices::Initialize(registry);
}

// --- encounter documents ------------------------------------------------------------------------

json UnitRow(const std::string& unit, int weight = 0) {
    json row;
    row["unit"] = unit;
    if (weight > 0) row["weight"] = weight;
    return row;
}

json SlotRow(const std::string& role, json units) {
    json row;
    row["role"] = role;
    row["units"] = std::move(units);
    return row;
}

/// The structural baseline: three role slots over the FIXTURE units. Its artillery pool is the fixture
/// imp_shaman (`skill.bolt` only), so it is deliberately NOT bootable with the fixture content — the
/// schema tests only parse it and `IntentCommitMixValidatedAtBoot` observes the role gate on it.
json BaseRow(const std::string& id) {
    json row;
    row["id"] = id;
    row["introduced_at_depth"] = 1;
    row["count"] = json::array({3, 3});
    row["slots"] = json::array({SlotRow("melee", json::array({UnitRow("enemy.imp", 4)})),
                                SlotRow("ranged", json::array({UnitRow("enemy.imp_archer", 3)})),
                                SlotRow("artillery", json::array({UnitRow("enemy.imp_shaman")}))});
    row["intent_commit_mix"] = 1;
    return row;
}

json BaseDoc() {
    json document;
    document["schema_version"] = 1;
    document["encounters"] = json::array({BaseRow("enc.base")});
    return document;
}

/// A bootable encounter over the FIXTURE units: role-valid pools (char.warrior strike / char.archer arrow /
/// char.mage fog — a `target: cell, shape: zone` tile-commit skill), no tags, depth 1. One such encounter
/// covers every non-boss battle slot of the sequence, so temp boots with it pass the coverage check.
json ActiveEncounterRow(const std::string& id) {
    json row;
    row["id"] = id;
    row["introduced_at_depth"] = 1;
    row["count"] = json::array({3, 3});
    row["slots"] = json::array({SlotRow("melee", json::array({UnitRow("char.warrior", 1)})),
                                SlotRow("ranged", json::array({UnitRow("char.archer", 1)})),
                                SlotRow("artillery", json::array({UnitRow("char.mage", 1)}))});
    row["intent_commit_mix"] = 1;
    return row;
}

json ActiveDoc() {
    json document;
    document["schema_version"] = 1;
    document["encounters"] = json::array({ActiveEncounterRow("enc.any")});
    return document;
}

/// Parses `document` and asserts the refusal: ParseEncounterContent returns nullopt with a non-empty error
/// that names `mention` (when given).
void ExpectRefused(const json& document, const std::string& mention = {}) {
    std::string error;
    const auto parsed = game::ParseEncounterContent(document, error);
    CHECK(!parsed.has_value());
    CHECK(!error.empty());
    if (!mention.empty()) CHECK(error.find(mention) != std::string::npos);
}

// --- fixture documents (the encounter tests re-derive the generator config, dungeon_sequence style) --

json FixtureDungeonDoc() { return FixtureDoc("dungeon.json"); }

/// Builds the generator config from the fixture `generation` block, deliberately re-derived so a change to
/// the game-side parser cannot move these expectations.
game::gameplay::FRoomGenConfig ConfigFromDocument(const json& document, std::string_view family = "open") {
    const auto& generation = document.at("generation");
    game::gameplay::FRoomGenConfig config;
    for (const auto& name : generation.at("room_names")) config.names.push_back(name.get<std::string>());
    for (const auto& enemy : generation.at("enemies")) {
        config.enemies.push_back({enemy.at("unit").get<std::string>(),
                                  enemy.at("display_name").get<std::string>(),
                                  enemy.at("weight").get<int>()});
    }
    config.bossUnit = generation.at("boss_unit").get<std::string>();
    config.bossName = generation.at("boss_name").get<std::string>();
    config.enemyCount = {generation.at("enemy_count")[0].get<int>(), generation.at("enemy_count")[1].get<int>()};
    config.obstacleCount = {generation.at("obstacle_count")[0].get<int>(),
                            generation.at("obstacle_count")[1].get<int>()};
    config.trapCount = {generation.at("trap_count")[0].get<int>(), generation.at("trap_count")[1].get<int>()};
    config.family = std::string(family);
    return config;
}

std::vector<std::pair<int, int>> PartyCellsFromDocument(const json& document) {
    std::vector<std::pair<int, int>> cells;
    for (const auto& member : document.at("party")) {
        cells.emplace_back(member.at("grid_x").get<int>(), member.at("grid_y").get<int>());
    }
    return cells;
}

/// The enemy rows of a battlefield document (the party rows have `faction: "party"`).
std::vector<json> EnemyRows(const json& document) {
    std::vector<json> rows;
    for (const auto& unit : document.at("units")) {
        if (unit.at("faction") == "enemy") rows.push_back(unit);
    }
    return rows;
}

// --- a hand-built generator config with role slots (content-free: GenerateRoom resolves no ids) --

game::gameplay::FRoomGenConfig HandBuiltConfig(std::string_view family) {
    game::gameplay::FRoomGenConfig config;
    config.names = {"Test chamber"};
    config.family = std::string(family);
    config.enemyCount = {4, 6};
    config.obstacleCount = {2, 4};
    config.trapCount = {1, 3};
    // The legacy pool: roleSlots/fill win when set, this is what an emptied composition falls back to
    // (PlaceEnemies picks from `enemies` for empty roleSlots and for every boss room).
    config.enemies = {{"u.legacy.a", "Legacy A", 2}, {"u.legacy.b", "Legacy B", 1}};
    config.roleSlots = {
        {"melee", {{"u.melee.a", "Melee A", 2}, {"u.melee.b", "Melee B", 1}}},
        {"ranged", {{"u.ranged.a", "Ranged A", 1}}},
        {"support", {{"u.support.a", "Support A", 1}}},
        {"artillery", {{"u.artillery.a", "Artillery A", 2}, {"u.artillery.b", "Artillery B", 1}}},
    };
    config.fill = {{"u.fill.a", "Fill A", 2}, {"u.fill.b", "Fill B", 1}};
    return config;
}

const std::vector<std::pair<int, int>> kPartyCells{{3, 9}, {4, 9}, {6, 9}, {7, 9}};

bool SameRoom(const game::gameplay::FGeneratedRoom& lhs, const game::gameplay::FGeneratedRoom& rhs) {
    if (lhs.name != rhs.name || lhs.terrain != rhs.terrain || lhs.enemies.size() != rhs.enemies.size()) return false;
    for (std::size_t i = 0; i < lhs.enemies.size(); ++i) {
        if (lhs.enemies[i].unit != rhs.enemies[i].unit || lhs.enemies[i].displayName != rhs.enemies[i].displayName ||
            lhs.enemies[i].x != rhs.enemies[i].x || lhs.enemies[i].y != rhs.enemies[i].y) {
            return false;
        }
    }
    return true;
}

/// The generator's wall-level reachability guarantee (the AllConnected rule in FRoomGenerator.cpp /
/// AllUnitsReachable in dungeon_sequence_tests): walls block, units do not. MeasureRoom's stricter minPath
/// blocks on bodies too, so a fully packed spawn band necessarily reads -1 for interior units — the
/// saturation cases assert this invariant instead.
bool UnitsConnectedOverWalls(const game::gameplay::FGeneratedRoom& room,
                             const std::vector<std::pair<int, int>>& party) {
    const int size = game::gameplay::FGeneratedRoom::kSize;
    std::vector<std::pair<int, int>> units = party;
    for (const auto& enemy : room.enemies) units.emplace_back(enemy.x, enemy.y);
    CHECK(!units.empty());
    std::array<std::array<bool, game::gameplay::FGeneratedRoom::kSize>, game::gameplay::FGeneratedRoom::kSize> seen{};
    std::vector<std::pair<int, int>> open{units.front()};
    seen[units.front().second][units.front().first] = true;
    while (!open.empty()) {
        const auto [x, y] = open.back();
        open.pop_back();
        for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= size || ny >= size || seen[ny][nx]) continue;
            if (room.terrain[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)] == '#') continue;
            seen[ny][nx] = true;
            open.push_back({nx, ny});
        }
    }
    return std::ranges::all_of(units, [&](const std::pair<int, int>& unit) {
        return seen[unit.second][unit.first];
    });
}

/// One single-slot encounter checked against `content` (the role predicate probe).
bool CheckSingleSlot(std::string_view role, std::string_view unit, const game::gameplay::FContentRegistry& content,
                     std::string& error) {
    game::FEncounterContent table;
    game::FEncounterDef def;
    def.id = "enc.probe";
    game::FEncounterSlot slot;
    slot.role = std::string(role);
    slot.units.push_back(game::FEncounterUnit{std::string(unit), 1});
    def.slots.push_back(std::move(slot));
    table.encounters.push_back(std::move(def));
    return game::CheckEncounterContent(table, content, error);
}

// ------------------------------------------------------------------------------------------------
// 1. schema/parse
// ------------------------------------------------------------------------------------------------

void EncounterSchemaValidated() {
    // The accepted baseline parses with no error.
    {
        std::string error;
        const auto parsed = game::ParseEncounterContent(BaseDoc(), error);
        CHECK(parsed.has_value());
        CHECK(error.empty());
        CHECK(parsed->schemaVersion == 1);
        CHECK(parsed->encounters.size() == 1);
        CHECK(parsed->encounters.front().slots.size() == 3);
        CHECK(parsed->encounters.front().intentCommitMix == 1);
    }
    // An empty `encounters` array is a valid "no composition" file (the frozen fixture ships one).
    {
        auto document = BaseDoc();
        document["encounters"] = json::array();
        std::string error;
        const auto parsed = game::ParseEncounterContent(document, error);
        CHECK(parsed.has_value());
        CHECK(parsed->encounters.empty());
    }
    // Unknown role.
    {
        auto document = BaseDoc();
        document["encounters"][0]["slots"][0]["role"] = "skirmisher";
        ExpectRefused(document, "enc.base");
    }
    // count min > max.
    {
        auto document = BaseDoc();
        document["encounters"][0]["count"] = json::array({4, 3});
        ExpectRefused(document, "enc.base");
    }
    // count max above the battle cap (8).
    {
        auto document = BaseDoc();
        document["encounters"][0]["count"] = json::array({3, 9});
        ExpectRefused(document, "enc.base");
    }
    // count min below the role slot count.
    {
        auto document = BaseDoc();
        document["encounters"][0]["count"] = json::array({2, 3});
        ExpectRefused(document, "enc.base");
    }
    // fill missing while count.max exceeds the slots.
    {
        auto document = BaseDoc();
        document["encounters"][0]["count"] = json::array({3, 4});
        ExpectRefused(document, "enc.base");
    }
    // weight 0.
    {
        auto document = BaseDoc();
        document["encounters"][0]["slots"][0]["units"][0]["weight"] = 0;
        ExpectRefused(document, "enc.base");
    }
    // duplicate id.
    {
        auto document = BaseDoc();
        document["encounters"].push_back(BaseRow("enc.base"));
        ExpectRefused(document, "duplicate");
    }
    // intent_commit_mix 0.
    {
        auto document = BaseDoc();
        document["encounters"][0]["intent_commit_mix"] = 0;
        ExpectRefused(document, "enc.base");
    }
    // intent_commit_mix above the artillery slot count.
    {
        auto document = BaseDoc();
        document["encounters"][0]["intent_commit_mix"] = 2;
        ExpectRefused(document, "enc.base");
    }
    // introduced_at_depth 0.
    {
        auto document = BaseDoc();
        document["encounters"][0]["introduced_at_depth"] = 0;
        ExpectRefused(document, "enc.base");
    }
    // The 14a closed key set: the 14b `elite` block is not a legal key yet.
    {
        auto document = BaseDoc();
        document["elite"] = json::object();
        ExpectRefused(document, "elite");
    }
    // schema_version 2.
    {
        auto document = BaseDoc();
        document["schema_version"] = 2;
        ExpectRefused(document);
    }
    // Structural pass + content refusal: an unknown unit id is caught by CheckEncounterContent.
    {
        auto document = BaseDoc();
        document["encounters"][0]["slots"][0]["units"] = json::array({UnitRow("enemy.zzz")});
        std::string error;
        const auto parsed = game::ParseEncounterContent(document, error);
        CHECK(parsed.has_value());
        const auto content = game::gameplay::FContentRegistry::load(game::FAssetManager{TestAssetRoots()});
        CHECK(content.has_value());
        std::string checkError;
        CHECK(!game::CheckEncounterContent(*parsed, *content, checkError));
        CHECK(checkError.find("enemy.zzz") != std::string::npos);
    }
}

// ------------------------------------------------------------------------------------------------
// 2/3. boot-time role checks: the tile-commit gate and the role predicates
// ------------------------------------------------------------------------------------------------

void IntentCommitMixValidatedAtBoot() {
    // The fixture skills table carries the tile-commit skill the derivation below depends on.
    const auto skills = FixtureDoc("skills.json");
    bool hasFirebomb = false;
    for (const auto& skill : skills.at("skills")) {
        if (skill.at("id").get<std::string>() == "skill.imp_firebomb") hasFirebomb = true;
    }
    CHECK(hasFirebomb);

    FTempAssets assets("enc-mix");
    assets.Write("encounters.json", BaseDoc());  // artillery pool = the fixture imp_shaman (bolt only)
    const std::vector<std::string> roots = assets.Roots();
    CHECK(!BootsGameplay(roots));  // the artillery pool has no tile-commit skill: boot refused

    // Derive: imp_shaman + skill.imp_firebomb (target cell / shape zone -> Tile by IntentModeOf).
    auto units = FixtureDoc("units.json");
    int patched = 0;
    for (auto& unit : units["units"]) {
        if (unit["id"] == "enemy.imp_shaman") {
            unit["skills"].push_back("skill.imp_firebomb");
            ++patched;
        }
    }
    CHECK(patched == 1);
    assets.Write("units.json", units);
    CHECK(BootsGameplay(roots));  // the same table now passes the role gate
}

void RolePredicatesValidated() {
    const auto fixture = game::gameplay::FContentRegistry::load(game::FAssetManager{TestAssetRoots()});
    CHECK(fixture.has_value());
    const auto ship = game::gameplay::FContentRegistry::load(game::FAssetManager{ShipAssetRoots()});
    CHECK(ship.has_value());
    std::string error;

    // A melee slot needs a short-reach hostile skill: the archer's arrow (range 4) is not one.
    CHECK(!CheckSingleSlot("melee", "enemy.imp_archer", *fixture, error));
    CHECK(error.find("enemy.imp_archer") != std::string::npos);
    // A support slot needs an ally-target skill: the imp carries only strike.
    CHECK(!CheckSingleSlot("support", "enemy.imp", *fixture, error));
    CHECK(error.find("enemy.imp") != std::string::npos);
    // A ranged slot needs a hostile unit-mode skill at >= 2 tiles: strike reaches 1.
    CHECK(!CheckSingleSlot("ranged", "enemy.imp", *fixture, error));
    CHECK(error.find("enemy.imp") != std::string::npos);
    // Positive controls (the same table accepts role-correct units).
    CHECK(CheckSingleSlot("melee", "enemy.imp", *fixture, error));
    CHECK(CheckSingleSlot("ranged", "enemy.imp_archer", *fixture, error));
    CHECK(CheckSingleSlot("support", "char.healer", *fixture, error));
    // Artillery = a tile-commit skill by the FIntent mode rule: the fixture shaman (bolt only) is refused,
    // the shipped shaman (firebomb) passes.
    CHECK(!CheckSingleSlot("artillery", "enemy.imp_shaman", *fixture, error));
    CHECK(error.find("enemy.imp_shaman") != std::string::npos);
    CHECK(CheckSingleSlot("artillery", "enemy.imp_shaman", *ship, error));
}

// ------------------------------------------------------------------------------------------------
// 4. the generator places one unit per role slot
// ------------------------------------------------------------------------------------------------

void SlotsDetermineRoles() {
    const int size = game::gameplay::FGeneratedRoom::kSize;
    for (const char* family : {"open", "open_variant", "corridor", "cover", "split"}) {
        CHECK(game::gameplay::FamilyStrengthOf(family).has_value());  // a loader-valid family name
        const auto config = HandBuiltConfig(family);
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            const auto room = game::gameplay::GenerateRoom(config, {seed, 1, 0, false, kPartyCells});
            CHECK(room.enemies.size() >= 4 && room.enemies.size() <= 6);  // config.enemyCount
            for (std::size_t i = 0; i < room.enemies.size(); ++i) {
                const auto& pool = i < config.roleSlots.size() ? config.roleSlots[i].units : config.fill;
                CHECK(std::ranges::any_of(pool, [&](const game::gameplay::FEnemyChoice& choice) {
                    return choice.unit == room.enemies[i].unit;
                }));
                CHECK(room.enemies[i].y >= game::gameplay::kRoomEnemyFirstRow);
                CHECK(room.enemies[i].y <= game::gameplay::kRoomEnemyLastRow);
                CHECK(room.enemies[i].x >= 1 && room.enemies[i].x <= size - 2);
            }
            CHECK(game::gameplay::MeasureRoom(room, kPartyCells).minPath != -1);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// 5. the legacy path: absent file, empty fixture table, defaulted request extension
// ------------------------------------------------------------------------------------------------

void LegacyGenerationWhenEncountersAbsent() {
    // An absent file loads as "nothing installed".
    {
        FTempAssets bare("enc-bare");
        entt::registry registry;
        game::FAssetManager::Initialize(registry, {bare.root.string()});
        CHECK(game::LoadEncounterContent(registry, registry.ctx().get<game::FAssetManager>()));
        CHECK(game::FindEncounterContent(registry) == nullptr);
    }
    // A re-initialize really falls back: the fixture's (empty) table must not survive a bare-root reload.
    {
        FTempAssets bare("enc-bare-reinit");
        entt::registry registry;
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::LoadEncounterContent(registry, registry.ctx().get<game::FAssetManager>()));
        CHECK(game::FindEncounterContent(registry) != nullptr);
        registry.ctx().erase<game::FAssetManager>();
        game::FAssetManager::Initialize(registry, {bare.root.string()});
        CHECK(game::LoadEncounterContent(registry, registry.ctx().get<game::FAssetManager>()));
        CHECK(game::FindEncounterContent(registry) == nullptr);
    }
    // The fixture boot keeps the legacy generation.enemies composition: the plan names no encounter and
    // the built battlefield's enemies are exactly GenerateRoom's legacy output.
    {
        FBoot boot(UniquePath("enc-legacy"), TestAssetRoots());
        CHECK(boot.content && boot.dungeon);
        CHECK(game::FindEncounterContent(boot.registry) != nullptr);
        CHECK(game::FindEncounterContent(boot.registry)->encounters.empty());
        game::StartRun(boot.registry, kSeed, true);
        const game::FRoomPlan planned = game::FrameRoomPlan(boot.registry, boot.run().room);
        CHECK(planned.encounterId.empty());
        const auto document = game::BuildDungeonBattlefield(boot.registry);
        const auto config = ConfigFromDocument(FixtureDungeonDoc());
        const auto expected = game::gameplay::GenerateRoom(
            config, {kSeed, boot.run().depth, boot.run().room, false, PartyCellsFromDocument(FixtureDungeonDoc())});
        const auto enemies = EnemyRows(document);
        CHECK(enemies.size() == expected.enemies.size());
        for (std::size_t i = 0; i < expected.enemies.size(); ++i) {
            CHECK(enemies[i]["unit"] == expected.enemies[i].unit);
            CHECK(enemies[i]["display_name"] == expected.enemies[i].displayName);
            CHECK(enemies[i]["grid_x"].get<int>() == expected.enemies[i].x);
            CHECK(enemies[i]["grid_y"].get<int>() == expected.enemies[i].y);
        }
    }
    // The request-extension defaults are the legacy output: an explicitly defaulted request is identical to
    // the aggregate-initialized call shape that predates WP-14, across every family. (The absolute golden of
    // the open layout lives in dungeon_sequence_tests OpenFamilyUnchanged.)
    for (const char* family : {"open", "open_variant", "corridor", "cover", "split"}) {
        auto config = HandBuiltConfig(family);
        config.roleSlots.clear();
        config.fill.clear();
        for (std::uint32_t seed = 1; seed <= 8; ++seed) {
            const auto legacy = game::gameplay::GenerateRoom(config, {seed, 1, 0, false, kPartyCells});
            game::gameplay::FRoomRequest defaults{seed, 1, 0, false, kPartyCells};
            defaults.family = "";
            defaults.extraEnemyCount = 0;
            defaults.extraEnemyUnit = "";
            defaults.extraEnemyName = "";
            defaults.leaderNamePrefix = "";
            CHECK(SameRoom(game::gameplay::GenerateRoom(config, defaults), legacy));
        }
    }
}

// ------------------------------------------------------------------------------------------------
// 6. boss slots stay on the generation pool
// ------------------------------------------------------------------------------------------------

void BossSlotsUseGenerationPool() {
    FTempAssets assets("enc-boss");
    assets.Write("encounters.json", ActiveDoc());
    FBoot boot(UniquePath("enc-boss-run"), assets.Roots());
    CHECK(boot.content && boot.dungeon);
    game::StartRun(boot.registry, kSeed, true);

    // Find the first boss battle of the sequence (dungeon 3's mid boss with the fixture data).
    const auto* sequence = game::FindRunSequence(boot.registry);
    CHECK(sequence != nullptr);
    int bossDepth = 0;
    int bossOrdinal = -1;
    for (int index = 0; index < sequence->TotalDungeons() && bossOrdinal < 0; ++index) {
        const auto position = sequence->PositionOf(index);
        for (int ordinal = 0; ordinal < sequence->BattleCountAt(position); ++ordinal) {
            if (sequence->IsBossBattle(position, ordinal)) {
                bossDepth = index + 1;
                bossOrdinal = ordinal;
                break;
            }
        }
    }
    CHECK(bossOrdinal >= 0);
    boot.run().depth = bossDepth;
    boot.run().room = bossOrdinal;

    const game::FRoomPlan planned = game::FrameRoomPlan(boot.registry, bossOrdinal);
    CHECK(planned.encounterId.empty());
    const auto document = game::BuildDungeonBattlefield(boot.registry);
    const auto enemies = EnemyRows(document);
    const auto config = ConfigFromDocument(FixtureDungeonDoc());
    CHECK(enemies.size() == 3);  // boss rooms keep the classic count
    CHECK(enemies.front().at("unit") == config.bossUnit);
    for (const auto& enemy : enemies) {
        const std::string unit = enemy.at("unit").get<std::string>();
        const bool inGenerationPool =
            unit == config.bossUnit ||
            std::ranges::any_of(config.enemies, [&](const game::gameplay::FEnemyChoice& choice) {
                return choice.unit == unit;
            });
        CHECK(inGenerationPool);
        // The active encounter's pools are party units; roleSlots must never leak into a boss room.
        CHECK(unit.rfind("char.", 0) != 0);
    }
}

// ------------------------------------------------------------------------------------------------
// 7. room composition (확정 §3) is independent of the encounter table
// ------------------------------------------------------------------------------------------------

void CompositionUnchanged() {
    FTempAssets assets("enc-compose");
    assets.Write("encounters.json", ActiveDoc());
    FBoot absent(UniquePath("enc-compose-absent"), TestAssetRoots());  // fixture: the empty table
    FBoot active(UniquePath("enc-compose-active"), assets.Roots());
    CHECK(absent.content && absent.dungeon);
    CHECK(active.content && active.dungeon);
    // The boots really differ by the table (otherwise the comparison below is vacuous).
    CHECK(game::FindEncounterContent(absent.registry) != nullptr);
    CHECK(game::FindEncounterContent(absent.registry)->encounters.empty());
    CHECK(game::FindEncounterContent(active.registry) != nullptr);
    CHECK(!game::FindEncounterContent(active.registry)->encounters.empty());

    const auto* seqAbsent = game::FindRunSequence(absent.registry);
    const auto* seqActive = game::FindRunSequence(active.registry);
    CHECK(seqAbsent != nullptr && seqActive != nullptr);
    CHECK(seqAbsent->TotalDungeons() == seqActive->TotalDungeons());
    for (int index = 0; index < seqAbsent->TotalDungeons(); ++index) {
        const auto posAbsent = seqAbsent->PositionOf(index);
        const auto posActive = seqActive->PositionOf(index);
        CHECK(seqAbsent->BattleCountAt(posAbsent) == seqActive->BattleCountAt(posActive));
        CHECK(seqAbsent->DungeonAt(posAbsent).rooms.size() == seqActive->DungeonAt(posActive).rooms.size());
        for (int ordinal = 0; ordinal < seqAbsent->BattleCountAt(posAbsent); ++ordinal) {
            const auto* slotAbsent = seqAbsent->BattleSlotAt(posAbsent, ordinal);
            const auto* slotActive = seqActive->BattleSlotAt(posActive, ordinal);
            CHECK(slotAbsent != nullptr && slotActive != nullptr);
            CHECK(slotAbsent->type == slotActive->type && slotAbsent->family == slotActive->family &&
                  slotAbsent->boss == slotActive->boss && slotAbsent->encounter == slotActive->encounter);
            const auto* gapAbsent = seqAbsent->GapEvent(posAbsent, ordinal);
            const auto* gapActive = seqActive->GapEvent(posActive, ordinal);
            CHECK((gapAbsent == nullptr) == (gapActive == nullptr));
            if (gapAbsent != nullptr && gapActive != nullptr) CHECK(gapAbsent->eventPool == gapActive->eventPool);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// 8/9. the dynamic pool: depth/theme gate and determinism
// ------------------------------------------------------------------------------------------------

game::FEncounterContent DepthTable() {
    game::FEncounterContent table;
    const auto add = [&table](const char* id, int depth, std::vector<std::string> tags) {
        game::FEncounterDef def;
        def.id = id;
        def.introducedAtDepth = depth;
        def.tags = std::move(tags);
        table.encounters.push_back(std::move(def));
    };
    add("enc.crypt_a", 1, {"theme.ember_crypt"});
    add("enc.any_b", 2, {});
    add("enc.ashen_c", 3, {"theme.ashen_reach"});
    add("enc.both_d", 4, {"theme.ember_crypt", "theme.ashen_reach"});
    add("enc.ashen_e", 6, {"theme.ashen_reach"});
    return table;
}

void IntroducedAtDepth() {
    const auto table = DepthTable();
    std::set<std::string> pickedForAshenSix;
    bool anyPick = false;
    for (int depth = 1; depth <= 6; ++depth) {
        for (const char* theme : {"theme.ember_crypt", "theme.ashen_reach"}) {
            for (std::uint32_t seed = 1; seed <= 40; ++seed) {
                for (int room = 0; room < 3; ++room) {
                    const auto* def = game::ResolveEncounter(table, "", theme, seed, depth, room);
                    if (def == nullptr) continue;
                    anyPick = true;
                    CHECK(def->introducedAtDepth <= depth);
                    const bool themed = def->tags.empty() ||
                                        std::ranges::find(def->tags, std::string(theme)) != def->tags.end();
                    CHECK(themed);
                    if (depth == 6 && std::string_view(theme) == "theme.ashen_reach") {
                        pickedForAshenSix.insert(def->id);
                    }
                }
            }
        }
    }
    CHECK(anyPick);
    // A theme with nothing eligible at this depth resolves to nothing (ashen at depth 1).
    CHECK(game::ResolveEncounter(table, "", "theme.ashen_reach", 7, 1, 0) == nullptr);
    CHECK(game::ResolveEncounter(table, "", "theme.void", 7, 1, 0) == nullptr);  // only enc.crypt_a at depth 1
    // The depth-gated encounter appears exactly once its gate opens.
    CHECK(pickedForAshenSix.count("enc.ashen_e") > 0);
    std::set<std::string> pickedForAshenFive;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        if (const auto* def = game::ResolveEncounter(table, "", "theme.ashen_reach", seed, 5, 0)) {
            pickedForAshenFive.insert(def->id);
        }
    }
    CHECK(pickedForAshenFive.count("enc.ashen_e") == 0);
}

game::FEncounterContent SeedTable() {
    game::FEncounterContent table;
    for (const char* id : {"enc.one", "enc.two"}) {
        game::FEncounterDef def;
        def.id = id;
        def.introducedAtDepth = 1;
        def.count = {3, 3};
        game::FEncounterSlot melee;
        melee.role = "melee";
        melee.units.push_back({"enemy.imp", 1});
        game::FEncounterSlot ranged;
        ranged.role = "ranged";
        ranged.units.push_back({"enemy.imp_archer", 1});
        game::FEncounterSlot artillery;
        artillery.role = "artillery";
        artillery.units.push_back({"enemy.imp_shaman", 1});
        def.slots = {std::move(melee), std::move(ranged), std::move(artillery)};
        table.encounters.push_back(std::move(def));
    }
    return table;
}

void Deterministic() {
    const auto table = SeedTable();
    const auto* first = game::ResolveEncounter(table, "", "theme.ember_crypt", 123, 3, 1);
    const auto* second = game::ResolveEncounter(table, "", "theme.ember_crypt", 123, 3, 1);
    CHECK(first != nullptr && second != nullptr);
    CHECK(first->id == second->id);

    // The room the first pick composes is deterministic too.
    const auto content = game::gameplay::FContentRegistry::load(game::FAssetManager{TestAssetRoots()});
    CHECK(content.has_value());
    auto config = HandBuiltConfig("open");
    game::ApplyEncounter(config, *first, *content);
    const auto roomA = game::gameplay::GenerateRoom(config, {123, 3, 1, false, kPartyCells});
    const auto roomB = game::gameplay::GenerateRoom(config, {123, 3, 1, false, kPartyCells});
    CHECK(SameRoom(roomA, roomB));

    // Different seeds reach more than one encounter (the pick is seed-driven, not fixed).
    std::set<std::string> distinct;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        if (const auto* def = game::ResolveEncounter(table, "", "theme.ember_crypt", seed, 3, 1)) {
            distinct.insert(def->id);
        }
    }
    CHECK(distinct.size() > 1);
}

// ------------------------------------------------------------------------------------------------
// 10. the ctx plan belongs to the real battlefield build only
// ------------------------------------------------------------------------------------------------

void RoomPlanEmplacedByBattlefieldOnly() {
    FTempAssets assets("enc-plan");
    assets.Write("encounters.json", ActiveDoc());
    FBoot boot(UniquePath("enc-plan-run"), assets.Roots());
    CHECK(boot.content && boot.dungeon);
    // InitializeDungeon ran the boot probe; the probe must never touch the ctx.
    CHECK(game::FindRoomPlan(boot.registry) == nullptr);
    game::StartRun(boot.registry, kSeed, true);
    CHECK(game::FindRoomPlan(boot.registry) == nullptr);  // no battlefield has been built yet

    const game::FRoomPlan expected = game::FrameRoomPlan(boot.registry, boot.run().room);
    const auto document = game::BuildDungeonBattlefield(boot.registry);
    const auto* plan = game::FindRoomPlan(boot.registry);
    CHECK(plan != nullptr);
    CHECK(plan->encounterId == expected.encounterId);
    CHECK(plan->family == expected.family);
    CHECK(plan->encounterId == "enc.any");  // the active table covers battle slot 0
    CHECK(plan->family == "open");
    CHECK(!plan->elite && plan->affixId.empty() && plan->leaderId.empty());  // 14a defaults
    // The room the plan describes really was built from it (the composed role slots).
    CHECK(EnemyRows(document).size() == 3);
}

// ------------------------------------------------------------------------------------------------
// 11. the request extension fields (P14-2, the single placement rule)
// ------------------------------------------------------------------------------------------------

void RequestExtensionFields() {
    // request.family overrides config.family: the terrain equals a config-family call with that family.
    for (std::uint32_t seed = 1; seed <= 5; ++seed) {
        auto openConfig = HandBuiltConfig("open");
        auto coverConfig = HandBuiltConfig("cover");
        game::gameplay::FRoomRequest overrideRequest{seed, 1, 0, false, kPartyCells};
        overrideRequest.family = "cover";
        const auto overridden = game::gameplay::GenerateRoom(openConfig, overrideRequest);
        const auto configFamily = game::gameplay::GenerateRoom(coverConfig, {seed, 1, 0, false, kPartyCells});
        CHECK(SameRoom(overridden, configFamily));
        // Guard: the override really changes the layout (otherwise the equality above proves nothing).
        const auto plainOpen = game::gameplay::GenerateRoom(openConfig, {seed, 1, 0, false, kPartyCells});
        CHECK(overridden.terrain != plainOpen.terrain);
    }

    // leaderNamePrefix prefixes the first enemy (the leader) only.
    {
        const auto config = HandBuiltConfig("open");
        const auto base = game::gameplay::GenerateRoom(config, {kSeed, 1, 0, false, kPartyCells});
        game::gameplay::FRoomRequest request{kSeed, 1, 0, false, kPartyCells};
        request.leaderNamePrefix = "Elite ";
        const auto renamed = game::gameplay::GenerateRoom(config, request);
        CHECK(!base.enemies.empty());
        CHECK(renamed.terrain == base.terrain);
        CHECK(renamed.enemies.size() == base.enemies.size());
        CHECK(renamed.enemies.front().displayName == "Elite " + base.enemies.front().displayName);
        for (std::size_t i = 1; i < base.enemies.size(); ++i) {
            CHECK(renamed.enemies[i].displayName == base.enemies[i].displayName);
            CHECK(renamed.enemies[i].unit == base.enemies[i].unit);
            CHECK(renamed.enemies[i].x == base.enemies[i].x && renamed.enemies[i].y == base.enemies[i].y);
        }
    }

    // extraEnemyCount appends extraEnemyUnit/extraEnemyName AFTER the composition.
    {
        const auto config = HandBuiltConfig("open");
        const auto base = game::gameplay::GenerateRoom(config, {kSeed, 1, 0, false, kPartyCells});
        game::gameplay::FRoomRequest request{kSeed, 1, 0, false, kPartyCells};
        request.extraEnemyCount = 2;
        request.extraEnemyUnit = "enemy.extra";
        request.extraEnemyName = "Extra";
        const auto room = game::gameplay::GenerateRoom(config, request);
        CHECK(room.enemies.size() == base.enemies.size() + 2);
        for (std::size_t i = 0; i < base.enemies.size(); ++i) {
            CHECK(room.enemies[i].unit == base.enemies[i].unit);
            CHECK(room.enemies[i].x == base.enemies[i].x && room.enemies[i].y == base.enemies[i].y);
        }
        for (std::size_t i = base.enemies.size(); i < room.enemies.size(); ++i) {
            CHECK(room.enemies[i].unit == "enemy.extra");
            CHECK(room.enemies[i].displayName == "Extra");
        }
        CHECK(game::gameplay::MeasureRoom(room, kPartyCells).minPath != -1);
    }

    // A huge ask shrinks at the family's spawn-slot cap instead of failing (W10 §5-4).
    {
        // open_variant's slot list is exactly its two 2-column flanks per side x 3 band rows = 12 cells.
        auto variantConfig = HandBuiltConfig("open_variant");
        game::gameplay::FRoomRequest huge{kSeed, 1, 0, false, kPartyCells};
        huge.extraEnemyCount = 1000;
        huge.extraEnemyUnit = "enemy.extra";
        huge.extraEnemyName = "Extra";
        const auto capped = game::gameplay::GenerateRoom(variantConfig, huge);
        CHECK(capped.enemies.size() == 12);  // placed == the slot count, no failure
        CHECK(game::gameplay::MeasureRoom(capped, kPartyCells).minPath != -1);
        // Saturated: a larger ask cannot seat more units.
        game::gameplay::FRoomRequest huger = huge;
        huger.extraEnemyCount = 2000;
        CHECK(game::gameplay::GenerateRoom(variantConfig, huger).enemies.size() == capped.enemies.size());
        // Every other family saturates at its 30-cell band (3 rows x columns 1..10) the same way; a fully
        // packed band reads minPath -1 (bodies block), so the wall-level reachability guarantee is checked.
        for (const char* family : {"open", "corridor", "cover", "split"}) {
            const auto config = HandBuiltConfig(family);
            const auto first = game::gameplay::GenerateRoom(config, huge);
            const auto second = game::gameplay::GenerateRoom(config, huger);
            CHECK(first.enemies.size() == 30);
            CHECK(second.enemies.size() == first.enemies.size());
            CHECK(UnitsConnectedOverWalls(first, kPartyCells));
            std::vector<std::pair<int, int>> cells = kPartyCells;
            int extras = 0;
            for (const auto& enemy : first.enemies) {
                cells.emplace_back(enemy.x, enemy.y);
                if (enemy.unit != "enemy.extra") continue;
                ++extras;
                CHECK(enemy.y >= game::gameplay::kRoomEnemyFirstRow);
                CHECK(enemy.y <= game::gameplay::kRoomEnemyLastRow);
            }
            CHECK(extras > 0);
            std::ranges::sort(cells);
            CHECK(std::ranges::adjacent_find(cells) == cells.end());  // no two units share a cell
        }
    }

    // The extra-unit/prename extensions are ignored in a boss room (their header contract); the family
    // field keeps its general rule (heavy families are reserved for boss/elite rooms by D-23, not banned).
    {
        const auto config = HandBuiltConfig("open");
        const auto plain = game::gameplay::GenerateRoom(config, {kSeed, 1, 0, true, kPartyCells});
        game::gameplay::FRoomRequest extended{kSeed, 1, 0, true, kPartyCells};
        extended.extraEnemyCount = 5;
        extended.extraEnemyUnit = "enemy.extra";
        extended.extraEnemyName = "Extra";
        extended.leaderNamePrefix = "Boss ";
        const auto boss = game::gameplay::GenerateRoom(config, extended);
        CHECK(plain.enemies.size() == 3);  // the classic boss room count
        CHECK(plain.enemies.front().unit == config.bossUnit);
        CHECK(SameRoom(boss, plain));
        for (const auto& enemy : boss.enemies) CHECK(enemy.displayName.rfind("Boss ", 0) != 0);

        auto coverConfig = HandBuiltConfig("cover");
        game::gameplay::FRoomRequest bossCover{kSeed, 1, 0, true, kPartyCells};
        bossCover.family = "cover";
        CHECK(game::gameplay::GenerateRoom(config, bossCover).terrain ==
              game::gameplay::GenerateRoom(coverConfig, {kSeed, 1, 0, true, kPartyCells}).terrain);
    }
}

// ------------------------------------------------------------------------------------------------
// 12. the dungeon-init coverage gate
// ------------------------------------------------------------------------------------------------

void CoverageValidatedAtDungeonInit() {
    FTempAssets assets("enc-coverage");

    // (a) An encounter tagged for a theme that does not exist: the content boot passes, the dungeon boot
    // refuses (every non-boss slot would have nothing eligible).
    {
        auto document = ActiveDoc();
        document["encounters"][0]["tags"] = json::array({"theme.void"});
        assets.Write("encounters.json", document);
        std::string parseError;
        const auto table = game::ParseEncounterContent(document, parseError);
        CHECK(table.has_value());
        std::string sequenceError;
        const auto sequence = game::FRunSequence::Load(FixtureDungeonDoc(), sequenceError);
        CHECK(sequence.has_value());
        std::string coverageError;
        CHECK(!game::ValidateEncounterCoverage(*table, *sequence, coverageError));
        CHECK(!coverageError.empty());
        CHECK(coverageError.find("theme.ember_crypt") != std::string::npos);

        const auto loaded = game_test::LoadTestRegistry(assets.Roots(), UniquePath("enc-coverage-theme"));
        CHECK(loaded.content);
        CHECK(!loaded.dungeon);
    }

    // (b) introduced_at_depth beyond the whole sequence: the same refusal.
    {
        auto document = ActiveDoc();
        document["encounters"][0]["introduced_at_depth"] = 99;
        assets.Write("encounters.json", document);
        const auto loaded = game_test::LoadTestRegistry(assets.Roots(), UniquePath("enc-coverage-depth"));
        CHECK(loaded.content);
        CHECK(!loaded.dungeon);
    }

    // (c) A table that covers every slot boots.
    {
        assets.Write("encounters.json", ActiveDoc());
        const auto loaded = game_test::LoadTestRegistry(assets.Roots(), UniquePath("enc-coverage-ok"));
        CHECK(loaded.content);
        CHECK(loaded.dungeon);
    }
}

// ------------------------------------------------------------------------------------------------
// 13. the shipped table
// ------------------------------------------------------------------------------------------------

void ShippedEncountersBoot() {
    const auto loaded = game_test::LoadTestRegistry(ShipAssetRoots(), UniquePath("enc-ship"));
    CHECK(loaded.content);
    CHECK(loaded.dungeon);
    const auto* table = game::FindEncounterContent(loaded.registry);
    CHECK(table != nullptr);
    CHECK(!table->encounters.empty());
    CHECK(table->schemaVersion == 1);
    const auto& content = loaded.registry.ctx().get<game::gameplay::FContentRegistry>();

    std::set<std::string> meleeUnits;
    std::set<std::string> rangedUnits;
    std::set<std::string> supportUnits;
    std::set<std::string> artilleryUnits;
    for (const auto& encounter : table->encounters) {
        int artillerySlots = 0;
        for (const auto& slot : encounter.slots) {
            if (slot.role == game::kEncounterRoleArtillery) ++artillerySlots;
            for (const auto& choice : slot.units) {
                if (slot.role == game::kEncounterRoleMelee) meleeUnits.insert(choice.unit);
                else if (slot.role == game::kEncounterRoleRanged) rangedUnits.insert(choice.unit);
                else if (slot.role == game::kEncounterRoleSupport) supportUnits.insert(choice.unit);
                else if (slot.role == game::kEncounterRoleArtillery) artilleryUnits.insert(choice.unit);
            }
            // Deep units appear only behind the depth gate (introduced_at_depth >= 3).
            for (const auto& choice : slot.units) {
                if (choice.unit == "enemy.bone_guard" || choice.unit == "enemy.ash_priest") {
                    CHECK(encounter.introducedAtDepth >= 3);
                }
            }
        }
        for (const auto& choice : encounter.fill) {
            if (choice.unit == "enemy.bone_guard" || choice.unit == "enemy.ash_priest") {
                CHECK(encounter.introducedAtDepth >= 3);
            }
        }
        CHECK(artillerySlots >= 1);  // every encounter supplies a tile-commit intent (D-16)
        CHECK(encounter.intentCommitMix >= 1);
        CHECK(encounter.intentCommitMix <= artillerySlots);
    }
    // The six WP-14 units exist in the shipped content.
    for (const char* unit :
         {"enemy.brute", "enemy.bone_guard", "enemy.slinger", "enemy.chanter", "enemy.mortar", "enemy.ash_priest"}) {
        CHECK(content.units.find(unit) != nullptr);
    }
    // The role axes are occupied: melee/ranged/artillery each have >= 2 distinct units, support >= 1.
    CHECK(meleeUnits.size() >= 2);
    CHECK(rangedUnits.size() >= 2);
    CHECK(artilleryUnits.size() >= 2);
    CHECK(supportUnits.size() >= 1);
}

// ------------------------------------------------------------------------------------------------
// 14. the sprite mapping (P14-8)
// ------------------------------------------------------------------------------------------------

void SpriteFieldResolves() {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    auto content = game::gameplay::FContentRegistry::load(registry.ctx().get<game::FAssetManager>());
    CHECK(content.has_value());
    registry.ctx().emplace<game::gameplay::FContentRegistry>(std::move(*content));

    const std::vector<std::pair<std::string, game::EDungeonSprite>> cases{
        {"enemy.brute", game::EDungeonSprite::Imp},
        {"enemy.slinger", game::EDungeonSprite::ImpArcher},
        {"enemy.chanter", game::EDungeonSprite::ImpShaman},
        {"enemy.mortar", game::EDungeonSprite::ImpShaman},
        {"char.mage", game::EDungeonSprite::Mage},
        {"enemy.guardian_heart", game::EDungeonSprite::Guardian},
    };
    std::vector<game::FUnitPlacement> placements;
    placements.reserve(cases.size());
    for (const auto& [unit, sprite] : cases) {
        game::FUnitPlacement placement;
        placement.id = unit;
        placement.unitTemplateId = unit;
        placement.entity = registry.create();
        placements.push_back(std::move(placement));
    }
    game::AssignDungeonSprites(registry, placements);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto* art = registry.try_get<game::FUnitArt>(placements[i].entity);
        CHECK(art != nullptr);
        CHECK(art->sprite == cases[i].second);
    }
}

void UnknownEnemyFallsBackToImp() {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    auto content = game::gameplay::FContentRegistry::load(registry.ctx().get<game::FAssetManager>());
    CHECK(content.has_value());
    registry.ctx().emplace<game::gameplay::FContentRegistry>(std::move(*content));

    const std::vector<std::pair<std::string, game::EDungeonSprite>> cases{
        {"enemy.zzz", game::EDungeonSprite::Imp},         // an unknown enemy never renders as a party warrior
        {"party.zzz", game::EDungeonSprite::Warrior},     // an unknown party id keeps the warrior default
        {"char.zzz", game::EDungeonSprite::Warrior},
    };
    std::vector<game::FUnitPlacement> placements;
    placements.reserve(cases.size());
    for (const auto& [unit, sprite] : cases) {
        game::FUnitPlacement placement;
        placement.id = unit;
        placement.unitTemplateId = unit;
        placement.entity = registry.create();
        placements.push_back(std::move(placement));
    }
    game::AssignDungeonSprites(registry, placements);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto* art = registry.try_get<game::FUnitArt>(placements[i].entity);
        CHECK(art != nullptr);
        CHECK(art->sprite == cases[i].second);
    }
}

} // namespace

int main() {
    // Save safety (the effect_hooks contract): ctest exports no GAME01P_SAVE_DIR for feature tests, so
    // outside build-test.ps1 a plain run would target the user's real save under %APPDATA%. Commit an
    // override before any dungeon service can touch disk.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "encounter_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("encounters", {
        {"EncounterSchemaValidated", EncounterSchemaValidated},
        {"IntentCommitMixValidatedAtBoot", IntentCommitMixValidatedAtBoot},
        {"RolePredicatesValidated", RolePredicatesValidated},
        {"SlotsDetermineRoles", SlotsDetermineRoles},
        {"LegacyGenerationWhenEncountersAbsent", LegacyGenerationWhenEncountersAbsent},
        {"BossSlotsUseGenerationPool", BossSlotsUseGenerationPool},
        {"CompositionUnchanged", CompositionUnchanged},
        {"IntroducedAtDepth", IntroducedAtDepth},
        {"Deterministic", Deterministic},
        {"RoomPlanEmplacedByBattlefieldOnly", RoomPlanEmplacedByBattlefieldOnly},
        {"RequestExtensionFields", RequestExtensionFields},
        {"CoverageValidatedAtDungeonInit", CoverageValidatedAtDungeonInit},
        {"ShippedEncountersBoot", ShippedEncountersBoot},
        {"SpriteFieldResolves", SpriteFieldResolves},
        {"UnknownEnemyFallsBackToImp", UnknownEnemyFallsBackToImp},
    });
}
