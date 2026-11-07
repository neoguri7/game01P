// Drift guard for the frozen test fixtures (tests/fixtures/assets/data).
//
// The fixtures are a frozen copy of the shipped gameplay content. The unit tests read them, so a balance
// change in assets/data never moves a test expectation. This guard keeps the copy honest:
//
//   * fixture ids are a SUBSET of the shipped ids (never the reverse: other lanes add ship content in
//     parallel and must not have to touch the fixtures),
//   * every cross-reference inside the fixtures resolves (dungeon -> party/generation/boss/rune_drops/
//     passive_tree, units -> skills, units -> ai profiles, skills/runes -> statuses),
//   * the shipped dungeon itself still loads (so a broken ship asset cannot hide behind the fixture).
//
// The loaders are the authority: FContentRegistry / FAiLibrary / InitializeDungeon already reject unknown
// references and log the reason. A green load here means the fixture is internally consistent.
//
// The CHECK macro, RunTests() and the registry builder come from tests/TestSupport.h (B6), so this file is only
// the test bodies plus one runner call.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gameplay/data/FContentRegistry.h"

namespace {

// Every file the fixture set must carry. The comment in each file says why the values are frozen.
constexpr const char* kFixtureFiles[] = {"dungeon", "units", "skills", "runes", "statuses", "ai", "passive-tree"};

// A named seed keeps the guard run reproducible; the party contract is dungeon.json's "party" array: four
// playable units (one per class plus the healer), so the fixture cannot silently lose a party member.
constexpr std::uint32_t kGuardSeed = 1234;
constexpr std::size_t kExpectedPartySize = 4;

void FixtureFilesExist() {
    for (const char* file : kFixtureFiles) {
        const std::string path = FixtureDataPath(std::string(file) + ".json");
        CHECK(std::filesystem::exists(path));
        std::ifstream input(path);
        CHECK(input.good());
        nlohmann::json document;
        input >> document;
        CHECK(document.is_object());
        CHECK(document.contains("schema_version"));
    }
}

void RemoveCheckpoint(const std::string& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path + ".run.json", error);
}

/// fixture ids ⊆ ship ids: new content may only be added to the ship side without touching the fixtures.
void FixtureIdsAreSubsetOfShip() {
    const auto ship = game::gameplay::FContentRegistry::load(game::FAssetManager{ShipAssetRoots()});
    const auto fixture = game::gameplay::FContentRegistry::load(game::FAssetManager{TestAssetRoots()});
    CHECK(ship.has_value());
    CHECK(fixture.has_value());

    const auto subset = [](const auto& table, const auto& source) {
        for (const auto& row : source.rows) {
            CHECK(table.find(row.id) != nullptr);
        }
    };
    subset(ship->units, fixture->units);
    subset(ship->skills, fixture->skills);
    subset(ship->runes, fixture->runes);
    subset(ship->statuses, fixture->statuses);
}

/// The fixture's own dungeon loads: this is the cross-reference check for party/generation/boss/
/// rune_drops/passive_tree and the units/skills/statuses/ai references inside them.
void FixtureDungeonLoads() {
    const std::string checkpoint = "out/guard-fixture-checkpoint.json";
    auto loaded = game_test::LoadTestRegistry(TestAssetRoots(), checkpoint);
    CHECK(loaded.content);
    CHECK(loaded.dungeon);
    // InitializeDungeon only validates the definition; the run is materialised by StartDungeonRun. Never get<>
    // a ctx type that has not been proven to exist: find() is the assertion, and the party size is the
    // dungeon.json contract read through that run.
    game::StartDungeonRun(loaded.registry, kGuardSeed, true);
    const auto* run = loaded.registry.ctx().find<game::FDungeonRun>();
    CHECK(run != nullptr);
    CHECK(run->party.size() == kExpectedPartySize);
    RemoveCheckpoint(checkpoint);
}

/// The shipped dungeon loads too: the fixture must never mask a broken ship asset.
void ShipDungeonLoads() {
    const std::string checkpoint = "out/guard-ship-checkpoint.json";
    auto loaded = game_test::LoadTestRegistry(ShipAssetRoots(), checkpoint);
    CHECK(loaded.content);
    CHECK(loaded.dungeon);
    game::StartDungeonRun(loaded.registry, kGuardSeed, true);
    const auto* run = loaded.registry.ctx().find<game::FDungeonRun>();
    CHECK(run != nullptr);
    CHECK(run->party.size() == kExpectedPartySize);
    RemoveCheckpoint(checkpoint);
}

} // namespace

int main() {
    return game_test::RunTests("fixture guard", {
        {"fixture files exist", FixtureFilesExist},
        {"fixture ids are a subset of ship ids", FixtureIdsAreSubsetOfShip},
        {"fixture dungeon loads and its references resolve", FixtureDungeonLoads},
        {"ship dungeon loads", ShipDungeonLoads},
    });
}
