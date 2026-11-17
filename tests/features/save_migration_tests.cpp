// Save v2: the core schema migration and the section extension mechanism (WP-07).
//
// The run save is one file: a core schema, plus — since v2 — a `run.ext` container. A module (rewards,
// run flags, the run log, ...) registers a serializer at the composition root and owns its subtree and
// version; the core never names a module. Saves from the v1 build must keep loading: DungeonRunFromJson
// migrates them in memory (never on disk — the next save rewrites the file as v2).
//
// The section functions are tested here as units; the real call-site wiring (SaveDungeonRun ->
// PackSections, ContinueDungeonRun -> UnpackSections, a new run -> ClearPreserved) is WP-02a's contract.
// V1FixtureContinuesEndToEnd and RoundTripV2 use the real content and the save file; the other section
// tests use dummy serializers over a bare registry context.
//
// Conventions follow tests/features/fixture_guard_tests.cpp: one file, the shared TestSupport block, cwd
// is the repository root and the scratch files live under the system temp directory.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FSaveSections.h"

namespace {

using nlohmann::json;

// UnpackSections/PackSections return std::expected<void, std::string>; Check needs a bool. CheckOk prints
// the reason, so a red test says why a section call failed.
bool CheckOk(const std::expected<void, std::string>& result) {
    if (!result) std::cout << "    save section call failed: " << result.error() << '\n';
    return result.has_value();
}
#define CHECK_OK(expression) CHECK(CheckOk((expression)))

// --- shared values -----------------------------------------------------------------------------

constexpr const char* kDummySectionId = "dummy";
constexpr std::uint32_t kSectionSeed = 11;    // the run preserved sections are tagged with
constexpr std::uint32_t kRoundTripSeed = 777;
constexpr const char* kExpectedContentSchema = "units:1/skills:1/runes:1/statuses:1";

// The v1 fixture's values, asserted after every load of it.
constexpr std::uint32_t kFixtureSeed = 424242;
constexpr int kFixtureDepth = 2;
constexpr int kFixtureRoom = 1;
constexpr int kFixtureCleared = 1;
constexpr int kFixtureMaterials = 4;

/// A v1 save exactly as the pre-change build wrote it (DungeonRunToJson with `"schema_version": 1`):
/// every core key and every member key, real fixture ids, one allocated node per node-taking member, one
/// skill with a rune, depth 2 / cleared 1. Hand-written on purpose — it must not track the new writer.
constexpr const char* kV1SaveFixture = R"JSON(
{
  "schema_version": 1,
  "saved_at": "2026-11-08T09:00:00Z",
  "run": {
    "seed": 424242,
    "depth": 2,
    "room": 1,
    "cleared": 1,
    "materials": 4,
    "phase": "battle",
    "room_name": "Hall of echoes",
    "message": "",
    "runes": ["rune.fire"],
    "party": [
      {
        "id": "party.warrior",
        "display_name": "Alden / Warrior",
        "health": 21,
        "bonus_health": 5,
        "bonus_attack": 1,
        "bonus_magic": 0,
        "bonus_defense": 0,
        "bonus_magic_defense": 0,
        "bonus_speed": 0,
        "experience": 50,
        "level": 2,
        "passive_points": 1,
        "allocated_nodes": ["node.might"],
        "skills": [{"skill": "skill.strike", "runes": ["rune.power"]}]
      },
      {
        "id": "party.archer",
        "display_name": "Rowan / Archer",
        "health": -1,
        "bonus_health": 0,
        "bonus_attack": 0,
        "bonus_magic": 0,
        "bonus_defense": 0,
        "bonus_magic_defense": 0,
        "bonus_speed": 0,
        "experience": 0,
        "level": 1,
        "passive_points": 0,
        "allocated_nodes": [],
        "skills": []
      },
      {
        "id": "party.mage",
        "display_name": "Mira / Mage",
        "health": 14,
        "bonus_health": 0,
        "bonus_attack": 0,
        "bonus_magic": 2,
        "bonus_defense": 0,
        "bonus_magic_defense": 0,
        "bonus_speed": 0,
        "experience": 25,
        "level": 1,
        "passive_points": 1,
        "allocated_nodes": ["node.insight"],
        "skills": [{"skill": "skill.bolt", "runes": ["rune.swift"]}]
      },
      {
        "id": "party.healer",
        "display_name": "Lumen / Healer",
        "health": -1,
        "bonus_health": 0,
        "bonus_attack": 0,
        "bonus_magic": 0,
        "bonus_defense": 0,
        "bonus_magic_defense": 0,
        "bonus_speed": 0,
        "experience": 0,
        "level": 1,
        "passive_points": 0,
        "allocated_nodes": [],
        "skills": [{"skill": "skill.heal", "runes": []}]
      }
    ]
  }
}
)JSON";

// --- helpers -----------------------------------------------------------------------------------

/// Unique scratch directory under the system temp dir; the repository root stays the working directory.
struct FTempDir {
    std::filesystem::path path;

    FTempDir() {
        static std::uint32_t serial = 0;
        path = std::filesystem::temp_directory_path() /
             ("game01p-save-tests-" + std::to_string(++serial) + "-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~FTempDir() {
        std::error_code code;
        std::filesystem::remove_all(path, code);
    }
};

json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    CHECK(file.good());
    json document;
    file >> document;
    return document;
}

/// A registry booted on the fixture content, plus save rules that name exactly the shipped dungeon party
/// (the same source `SaveRules` in FDungeonRun.cpp reads).
struct FSaveFixture {
    std::unique_ptr<FTempDir> ownedDir;  // null when the test supplies its own checkpoint path
    std::string checkpoint;
    std::string savePath;
    game_test::FTestRegistry loaded;
    game::FDungeonSaveRules rules;

    FSaveFixture() : FSaveFixture(std::make_unique<FTempDir>()) {}

    explicit FSaveFixture(std::unique_ptr<FTempDir> dir)
        : ownedDir(std::move(dir)),
          checkpoint((ownedDir->path / "checkpoint.json").string()),
          savePath(checkpoint + ".run.json"),
          loaded(game_test::LoadTestRegistry(TestAssetRoots(), checkpoint)) {
        InitRules();
    }

    /// Boots a second registry over an existing save slot (the same checkpoint file and run save).
    explicit FSaveFixture(std::string checkpointPath)
        : checkpoint(std::move(checkpointPath)),
          savePath(checkpoint + ".run.json"),
          loaded(game_test::LoadTestRegistry(TestAssetRoots(), checkpoint)) {
        InitRules();
    }

    void InitRules() {
        CHECK(loaded.content && loaded.dungeon);
        const auto* content = loaded.registry.ctx().find<game::gameplay::FContentRegistry>();
        const auto* growth = loaded.registry.ctx().find<game::FDungeonGrowth>();
        CHECK(content != nullptr && growth != nullptr);
        rules.content = content;
        rules.passiveTree = &growth->passiveTree;
        const auto dungeon = loaded.registry.ctx().get<game::FAssetManager>().digest("data/dungeon");
        CHECK(dungeon != nullptr);
        for (const auto& member : dungeon->json().at("party")) {
            rules.partyIds.push_back(member.at("id").get<std::string>());
        }
        CHECK(rules.partyIds.size() == 4u);
    }
};

void CheckFixtureRun(const game::FDungeonRun& run) {
    CHECK(run.seed == kFixtureSeed);
    CHECK(run.depth == kFixtureDepth);
    CHECK(run.room == kFixtureRoom);
    CHECK(run.cleared == kFixtureCleared);
    CHECK(run.materials == kFixtureMaterials);
    CHECK(run.phase == game::EDungeonPhase::Battle);
    CHECK(run.roomName == "Hall of echoes");
    CHECK(run.message.empty());
    CHECK(run.runes == std::vector<std::string>{"rune.fire"});
    CHECK(run.party.size() == 4u);
    CHECK(run.party[0].id == "party.warrior" && run.party[0].health == 21 && run.party[0].level == 2);
    CHECK(run.party[0].allocatedNodes == std::vector<std::string>{"node.might"});
    CHECK(run.party[0].skills.size() == 1u && run.party[0].skills[0].skillId == "skill.strike" &&
          run.party[0].skills[0].runeIds == std::vector<std::string>{"rune.power"});
    CHECK(run.party[1].id == "party.archer" && run.party[1].health == -1 && run.party[1].skills.empty());
    CHECK(run.party[2].allocatedNodes == std::vector<std::string>{"node.insight"});
    CHECK(run.party[3].skills.size() == 1u && run.party[3].skills[0].skillId == "skill.heal");
    CHECK(!run.advanceRequested && !run.abandonRequested && !run.leaveRequested);
}

void CheckSameRun(const game::FDungeonRun& before, const game::FDungeonRun& after) {
    CHECK(after.seed == before.seed && after.depth == before.depth && after.room == before.room);
    CHECK(after.cleared == before.cleared && after.materials == before.materials && after.phase == before.phase);
    CHECK(after.runes == before.runes && after.roomName == before.roomName && after.message == before.message);
    CHECK(after.party.size() == before.party.size());
    for (std::size_t index = 0; index < before.party.size(); ++index) {
        const auto& expected = before.party[index];
        const auto& actual = after.party[index];
        CHECK(actual.id == expected.id && actual.displayName == expected.displayName);
        CHECK(actual.health == expected.health && actual.bonusHealth == expected.bonusHealth);
        CHECK(actual.bonusAttack == expected.bonusAttack && actual.bonusMagic == expected.bonusMagic);
        CHECK(actual.bonusDefense == expected.bonusDefense && actual.bonusMagicDefense == expected.bonusMagicDefense);
        CHECK(actual.bonusSpeed == expected.bonusSpeed && actual.experience == expected.experience);
        CHECK(actual.level == expected.level && actual.passivePoints == expected.passivePoints);
        CHECK(actual.allocatedNodes == expected.allocatedNodes);
        CHECK(actual.skills.size() == expected.skills.size());
        for (std::size_t slot = 0; slot < expected.skills.size(); ++slot) {
            CHECK(actual.skills[slot].skillId == expected.skills[slot].skillId &&
                  actual.skills[slot].runeIds == expected.skills[slot].runeIds);
        }
    }
    CHECK(!after.advanceRequested && !after.abandonRequested && !after.leaveRequested);
}

// --- dummy sections (defined here on purpose: the real modules arrive in WP-17/18/19a) ----------

struct FDummyState {
    int count{0};
    std::string label;
};

game::FSaveSectionSerializer DummySerializer(int version = 1) {
    game::FSaveSectionSerializer serializer;
    serializer.id = kDummySectionId;
    serializer.version = version;
    serializer.pack = [](entt::registry& registry) {
        const auto& state = registry.ctx().get<FDummyState>();
        return json{{"count", state.count}, {"label", state.label}};
    };
    serializer.unpack = [](entt::registry& registry, const json& section) {
        FDummyState state;
        if (section.contains("count")) state.count = section.at("count").get<int>();
        if (section.contains("label")) state.label = section.at("label").get<std::string>();
        registry.ctx().insert_or_assign<FDummyState>(std::move(state));
    };
    return serializer;
}

json DummyDocument(std::uint32_t seed, json ext = json::object()) {
    return json{{"schema_version", 2}, {"run", {{"seed", seed}, {"ext", std::move(ext)}}}};
}

json DummyDocumentWithSection(std::uint32_t seed, const std::string& id, json section) {
    json ext = json::object();
    ext[id] = std::move(section);
    return DummyDocument(seed, std::move(ext));
}

// --- core schema: v1 loads, migration composes, party rules ------------------------------------

void V1SaveLoadsAsV2() {
    FSaveFixture fixture;
    const auto document = json::parse(kV1SaveFixture);
    const auto run = game::DungeonRunFromJson(document, fixture.rules);
    CHECK(run.has_value());
    CheckFixtureRun(*run);

    // The load never mutates the caller's document — the migration is in memory only.
    CHECK(document.at("schema_version") == 1);
    CHECK(!document.at("run").contains("ext"));

    // The v2 container's type contract: a present run.ext must be an object.
    auto broken = json::parse(kV1SaveFixture);
    broken["schema_version"] = 2;
    broken["run"]["ext"] = "not an object";
    const auto refused = game::DungeonRunFromJson(broken, fixture.rules);
    CHECK(!refused.has_value());
    CHECK(refused.error().contains("ext"));
}

void MigrationChainComposes() {
    std::vector<std::string> calls;
    std::vector<game::FSaveMigrationStep> steps;
    steps.push_back([&calls](json document) -> std::optional<json> {
        calls.push_back("v1->v2");
        document["schema_version"] = 2;
        document["step1"] = true;
        return document;
    });
    steps.push_back([&calls](json document) -> std::optional<json> {
        calls.push_back("v2->v3");
        document["schema_version"] = 3;
        document["step2"] = true;
        return document;
    });

    const json start = {{"schema_version", 1}, {"core", "kept"}};
    const auto upgraded = game::MigrateSaveDocumentWith(start, 1, 3, steps);
    CHECK(upgraded.has_value());
    const std::vector<std::string> expectedOrder = {"v1->v2", "v2->v3"};
    CHECK(calls == expectedOrder);
    CHECK(upgraded->at("schema_version") == 3 && upgraded->at("core") == "kept");
    CHECK(upgraded->at("step1") == true && upgraded->at("step2") == true);

    // v2 -> v3 runs only the second step.
    calls.clear();
    const auto direct = game::MigrateSaveDocumentWith(start, 2, 3, steps);
    CHECK(direct.has_value());
    CHECK(calls == std::vector<std::string>{"v2->v3"});
    CHECK(direct->at("schema_version") == 3 && !direct->contains("step1") && direct->at("step2") == true);

    // from == to copies; a future source, a missing step or a pre-v1 source is refused.
    const auto copied = game::MigrateSaveDocumentWith(start, 1, 1, steps);
    CHECK(copied.has_value() && *copied == start);
    CHECK(!game::MigrateSaveDocumentWith(start, 3, 2, steps).has_value());
    CHECK(!game::MigrateSaveDocumentWith(start, 1, 4, steps).has_value());
    CHECK(!game::MigrateSaveDocumentWith(start, 0, 1, steps).has_value());
}

void UnknownKeyIgnoredKnownKeyRequired() {
    FSaveFixture fixture;

    // Unknown keys at every level — even inside members — stay ignored.
    auto lenient = json::parse(kV1SaveFixture);
    lenient["mystery_top"] = 17;
    lenient["run"]["mystery_run"] = {{"nested", true}};
    lenient["run"]["party"][0]["mystery_member"] = "ignored";
    CHECK(game::DungeonRunFromJson(lenient, fixture.rules).has_value());

    // A missing required core key is refused, not defaulted.
    auto missing = json::parse(kV1SaveFixture);
    missing["run"].erase("party");
    const auto refused = game::DungeonRunFromJson(missing, fixture.rules);
    CHECK(!refused.has_value());
    CHECK(refused.error().contains("party"));
}

void FutureVersionRejected() {
    FSaveFixture fixture;
    auto document = json::parse(kV1SaveFixture);
    document["schema_version"] = 99;
    const auto refused = game::DungeonRunFromJson(document, fixture.rules);
    CHECK(!refused.has_value());
    CHECK(refused.error().contains("schema_version"));
    CHECK(refused.error().contains("not supported"));
}

// --- party rules: Subset accepts a part, Exact stays the historical whole-party rule ------------

void PartySubsetAccepted() {
    FSaveFixture fixture;
    fixture.rules.partyMatch = game::EPartyMatch::Subset;

    // 4 -> 3 members, reordered.
    auto document = json::parse(kV1SaveFixture);
    document["run"]["party"].erase(1);  // drop the archer
    const json first = document["run"]["party"][0];
    document["run"]["party"][0] = document["run"]["party"][1];
    document["run"]["party"][1] = first;
    const auto run = game::DungeonRunFromJson(document, fixture.rules);
    CHECK(run.has_value());
    CHECK(run->party.size() == 3u);
    CHECK(run->party[0].id == "party.mage" && run->party[1].id == "party.warrior");

    // The full party is still a valid subset.
    CHECK(game::DungeonRunFromJson(json::parse(kV1SaveFixture), fixture.rules).has_value());

    // Relaxing the size/order rule does not relax member validity: unknown or repeated members are refused.
    auto unknown = json::parse(kV1SaveFixture);
    unknown["run"]["party"][0]["id"] = "party.nobody";
    CHECK(!game::DungeonRunFromJson(unknown, fixture.rules).has_value());
    auto repeated = json::parse(kV1SaveFixture);
    repeated["run"]["party"][1]["id"] = "party.mage";
    CHECK(!game::DungeonRunFromJson(repeated, fixture.rules).has_value());

    // An empty party is never a subset.
    auto empty = json::parse(kV1SaveFixture);
    empty["run"]["party"] = json::array();
    CHECK(!game::DungeonRunFromJson(empty, fixture.rules).has_value());
}

void PartyExactStillRejected() {
    FSaveFixture fixture;  // default: Exact
    auto missing = json::parse(kV1SaveFixture);
    missing["run"]["party"].erase(3);
    const auto refused = game::DungeonRunFromJson(missing, fixture.rules);
    CHECK(!refused.has_value());
    CHECK(refused.error().contains("party"));

    // The full party still loads under Exact (the historical rule must not have shrunk).
    CHECK(game::DungeonRunFromJson(json::parse(kV1SaveFixture), fixture.rules).has_value());
}

// --- section mechanism: registry, round trip, preservation, versions, failure ------------------

void SectionRoundTrip() {
    entt::registry registry;
    game::RegisterCoreSaveSections(registry);
    game::RegisterSaveSection(registry, DummySerializer());
    registry.ctx().emplace<FDummyState>(FDummyState{3, "three"});

    // A duplicate id is ignored: first registration wins.
    game::RegisterSaveSection(registry, DummySerializer(7));
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().serializers.size() == 1u);
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().serializers[0].version == 1);

    // Incomplete registrations are ignored too.
    game::FSaveSectionSerializer nameless;
    game::RegisterSaveSection(registry, std::move(nameless));
    game::FSaveSectionSerializer incomplete;
    incomplete.id = "incomplete";
    game::RegisterSaveSection(registry, incomplete);
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().serializers.size() == 1u);

    json document = DummyDocument(kSectionSeed);
    CHECK_OK(game::PackSections(registry, document));
    CHECK(document["run"]["ext"].contains(kDummySectionId));
    const json expectedSection = {{"count", 3}, {"label", "three"}, {"section_version", 1}};
    CHECK(document["run"]["ext"].at(kDummySectionId) == expectedSection);
    CHECK(!document.contains("content_schema"));  // no asset boundary in this context -> no stamp

    registry.ctx().get<FDummyState>() = FDummyState{};
    CHECK_OK(game::UnpackSections(registry, document));
    CHECK(registry.ctx().get<FDummyState>().count == 3 && registry.ctx().get<FDummyState>().label == "three");
}

void UnknownSectionPreserved() {
    entt::registry registry;
    game::RegisterCoreSaveSections(registry);
    game::RegisterSaveSection(registry, DummySerializer());

    const json futureModule = {{"note", "keep me"}, {"k", 5}};
    const json futureList = json::array({1, 2, 3});
    json ext = json::object();
    ext["future_module"] = futureModule;
    ext["future_list"] = futureList;
    CHECK_OK(game::UnpackSections(registry, DummyDocument(kSectionSeed, ext)));
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.size() == 2u);

    json packed = DummyDocument(kSectionSeed);
    CHECK_OK(game::PackSections(registry, packed));
    CHECK(packed["run"]["ext"].at("future_module") == futureModule);
    CHECK(packed["run"]["ext"].at("future_list") == futureList);
    // The registered module is packed fresh alongside the preserved sections.
    CHECK(packed["run"]["ext"].at(kDummySectionId).at("section_version") == 1);
}

void UnknownSectionDeepPreserved() {
    entt::registry registry;
    game::RegisterCoreSaveSections(registry);
    game::RegisterSaveSection(registry, DummySerializer());

    json deepList = json::array();
    deepList.push_back(1);
    deepList.push_back(2);
    deepList.push_back(json{{"deep", true}});
    json nested = {{"list", std::move(deepList)}, {"obj", {{"r", 4.5}, {"s", "text"}}}};
    json ext = json::object();
    ext["future_module"] = nested;
    auto source = DummyDocument(kSectionSeed, std::move(ext));
    CHECK_OK(game::UnpackSections(registry, source));

    // The preserved copy must not alias the source document: mutate it after the unpack.
    source["run"]["ext"]["future_module"]["list"][1] = 999;
    source["run"]["ext"]["future_module"]["obj"]["s"] = "changed";
    source["run"]["ext"]["future_module"]["new_key"] = "injected";

    json packed = DummyDocument(kSectionSeed);
    CHECK_OK(game::PackSections(registry, packed));
    const auto& kept = packed["run"]["ext"].at("future_module");
    CHECK(kept.at("list").at(0) == 1 && kept.at("list").at(1) == 2 && kept.at("list").at(2).at("deep") == true);
    CHECK(kept.at("obj").at("r") == 4.5 && kept.at("obj").at("s") == "text");
    CHECK(!kept.contains("new_key"));
}

void PreservedDroppedForDifferentRun() {
    entt::registry registry;
    game::RegisterCoreSaveSections(registry);
    game::RegisterSaveSection(registry, DummySerializer());

    const json futureModule = {{"x", 1}};
    json ext = json::object();
    ext["future_module"] = futureModule;
    CHECK_OK(game::UnpackSections(registry, DummyDocument(kSectionSeed, ext)));
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.size() == 1u);

    // A save of the same run re-emits the preserved section and keeps it.
    json sameRun = DummyDocument(kSectionSeed);
    CHECK_OK(game::PackSections(registry, sameRun));
    CHECK(sameRun["run"]["ext"].contains("future_module"));
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.size() == 1u);

    // A save of another run drops it for good — the stale sections never leak into the new run's file.
    const std::uint32_t otherSeed = kSectionSeed + 1;
    json differentRun = DummyDocument(otherSeed);
    CHECK_OK(game::PackSections(registry, differentRun));
    CHECK(!differentRun["run"]["ext"].contains("future_module"));
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.empty());

    json stillDifferent = DummyDocument(otherSeed);
    CHECK_OK(game::PackSections(registry, stillDifferent));
    CHECK(!stillDifferent["run"]["ext"].contains("future_module"));

    // ClearPreserved is the explicit cleanup the new-run call site uses; without a registry it is a no-op.
    CHECK_OK(game::UnpackSections(registry, DummyDocument(kSectionSeed, ext)));
    CHECK(!registry.ctx().get<game::FSaveSectionRegistry>().preserved.empty());
    game::ClearPreserved(registry);
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.empty());
    entt::registry bare;
    game::ClearPreserved(bare);
}

void SectionFutureVersionRejected() {
    entt::registry registry;
    game::RegisterCoreSaveSections(registry);
    constexpr int kModuleVersion = 2;
    constexpr int kFileVersion = 5;
    game::RegisterSaveSection(registry, DummySerializer(kModuleVersion));

    json section = json::object();
    section["count"] = 1;
    section["section_version"] = kFileVersion;
    const auto refused = game::UnpackSections(registry, DummyDocumentWithSection(kSectionSeed, kDummySectionId, section));
    CHECK(!refused.has_value());
    CHECK(refused.error().contains(kDummySectionId));
    CHECK(refused.error().contains(std::to_string(kFileVersion)));
    CHECK(refused.error().contains(std::to_string(kModuleVersion)));

    // A missing or non-positive section_version is malformed rather than a future version.
    json noVersion = json::object();
    noVersion["count"] = 1;
    CHECK(!game::UnpackSections(registry, DummyDocumentWithSection(kSectionSeed, kDummySectionId, noVersion)).has_value());
    json zeroVersion = json::object();
    zeroVersion["count"] = 1;
    zeroVersion["section_version"] = 0;
    CHECK(!game::UnpackSections(registry, DummyDocumentWithSection(kSectionSeed, kDummySectionId, zeroVersion)).has_value());
    CHECK(!game::UnpackSections(registry, DummyDocumentWithSection(kSectionSeed, kDummySectionId, 7)).has_value());
}

void MissingSectionDefault() {
    entt::registry registry;
    game::RegisterCoreSaveSections(registry);
    json received = "unset";
    game::FSaveSectionSerializer recorder;
    recorder.id = "recorder";
    recorder.version = 1;
    recorder.pack = [](entt::registry&) { return json::object(); };
    recorder.unpack = [&received](entt::registry&, const json& section) { received = section; };
    game::RegisterSaveSection(registry, recorder);

    // An absent section reaches unpack as an empty object (the module default).
    CHECK_OK(game::UnpackSections(registry, DummyDocument(kSectionSeed)));
    CHECK(received.is_object() && received.empty());

    // A v1 document has no ext at all; the same default applies.
    received = "unset";
    const json v1 = {{"schema_version", 1}, {"run", {{"seed", kSectionSeed}}}};
    CHECK_OK(game::UnpackSections(registry, v1));
    CHECK(received.is_object() && received.empty());

    // Malformed documents are refused: no usable seed, a non-object ext.
    CHECK(!game::UnpackSections(registry, json{{"schema_version", 2}, {"run", json::object()}}).has_value());
    auto badExt = DummyDocument(kSectionSeed);
    badExt["run"]["ext"] = "not an object";
    CHECK(!game::UnpackSections(registry, badExt).has_value());

    // A registry without the section service is a wiring bug, not a silent skip.
    entt::registry bare;
    json document = DummyDocument(kSectionSeed);
    CHECK(!game::UnpackSections(bare, document).has_value());
    CHECK(!game::PackSections(bare, document).has_value());
}

void UnpackFailureRejectsLoad() {
    // (a) An unpack that throws refuses the load by name, and the previously preserved sections survive:
    // the failing document's unknown sections are not adopted.
    entt::registry registry;
    game::RegisterCoreSaveSections(registry);
    game::FSaveSectionSerializer flaky;
    flaky.id = "flaky";
    flaky.version = 1;
    flaky.pack = [](entt::registry&) { return json::object(); };
    flaky.unpack = [](entt::registry&, const json& section) {
        if (section.value("explode", false)) throw std::runtime_error("exploding on purpose");
    };
    game::RegisterSaveSection(registry, flaky);

    json preloadExt = json::object();
    preloadExt["future_module"] = json{{"x", 1}};
    CHECK_OK(game::UnpackSections(registry, DummyDocument(kSectionSeed, preloadExt)));
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.size() == 1u);

    json failingExt = json::object();
    failingExt["flaky"] = json{{"explode", true}, {"section_version", 1}};
    failingExt["future_other"] = json{{"y", 2}};
    const auto refused = game::UnpackSections(registry, DummyDocument(kSectionSeed, failingExt));
    CHECK(!refused.has_value());
    CHECK(refused.error().contains("flaky"));
    const auto& preserved = registry.ctx().get<game::FSaveSectionRegistry>().preserved;
    const json expectedPrior = {{"x", 1}};
    CHECK(preserved.size() == 1u && preserved[0].id == "future_module" && preserved[0].raw == expectedPrior);

    // (b) Everything is validated before anything is applied: a too-new section refuses the load without
    // the recorder's unpack having run — not even with its default.
    entt::registry other;
    game::RegisterCoreSaveSections(other);
    int calls = 0;
    game::FSaveSectionSerializer recorder;
    recorder.id = "recorder";
    recorder.version = 1;
    recorder.pack = [](entt::registry&) { return json::object(); };
    recorder.unpack = [&calls](entt::registry&, const json&) { ++calls; };
    game::RegisterSaveSection(other, recorder);
    game::FSaveSectionSerializer future;
    future.id = "future";
    future.version = 1;
    future.pack = [](entt::registry&) { return json::object(); };
    future.unpack = [](entt::registry&, const json&) {};
    game::RegisterSaveSection(other, future);

    json tooNew = json::object();
    tooNew["section_version"] = 5;
    const auto invalid = game::UnpackSections(other, DummyDocumentWithSection(kSectionSeed, "future", tooNew));
    CHECK(!invalid.has_value());
    CHECK(invalid.error().contains("future"));
    CHECK(calls == 0);
}

void PackFailureAbortsSave() {
    FTempDir dir;
    const auto path = dir.path / "run.json";
    entt::registry registry;
    game::RegisterCoreSaveSections(registry);
    game::FSaveSectionSerializer broken;
    broken.id = "broken";
    broken.version = 1;
    broken.pack = [](entt::registry&) -> json { throw std::runtime_error("cannot pack the broken section"); };
    broken.unpack = [](entt::registry&, const json&) {};
    game::RegisterSaveSection(registry, broken);

    const json futureModule = {{"x", 1}};
    json ext = json::object();
    ext["future_module"] = futureModule;
    CHECK_OK(game::UnpackSections(registry, DummyDocument(kSectionSeed, ext)));

    // A failing pack fails the whole pack: the document and the preserved list stay untouched.
    json document = DummyDocument(kSectionSeed);
    const auto refused = game::PackSections(registry, document);
    CHECK(!refused.has_value());
    CHECK(refused.error().contains("broken"));
    CHECK(document == DummyDocument(kSectionSeed));
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.size() == 1u);

    // The write path keeps the previous file on disk instead of writing a partial save.
    const json previous = {{"schema_version", 2}, {"run", {{"seed", kSectionSeed}, {"ext", json::object()}}}};
    std::string error;
    CHECK(game::WriteSaveFile(path, previous, error));
    const auto written = game::PackAndWriteSave(registry, DummyDocument(kSectionSeed), path);
    CHECK(!written.has_value());
    CHECK(ReadJsonFile(path) == previous);
}

// --- real content: a v2 round trip and the end-to-end v1 path ----------------------------------

void RoundTripV2() {
    FSaveFixture fixture;
    auto& registry = fixture.loaded.registry;
    game::StartDungeonRun(registry, kRoundTripSeed, true);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.depth = 2;
    run.cleared = 1;
    run.room = 1;
    run.materials = 5;
    run.roomName = "The broken gate";
    run.message = "steady";
    run.party[0].bonusAttack = 2;
    run.party[0].allocatedNodes = {"node.might"};
    run.party[0].skills = {game::FSkillSlot{"skill.strike", {"rune.fire"}}};
    run.runes = {"rune.cold"};
    CHECK(game::SaveDungeonRun(registry));

    const auto written = ReadJsonFile(fixture.savePath);
    CHECK(written.at("schema_version") == 2);
    CHECK(written.at("run").contains("ext") && written.at("run").at("ext").is_object());

    // The v2 file boots and continues to the same run.
    const game::FDungeonRun before = registry.ctx().get<game::FDungeonRun>();
    FSaveFixture reboot(fixture.checkpoint);
    CHECK(reboot.loaded.registry.ctx().get<game::FDungeonSave>().exists);
    CHECK(game::ContinueDungeonRun(reboot.loaded.registry));
    CheckSameRun(before, reboot.loaded.registry.ctx().get<game::FDungeonRun>());

    // With the asset boundary in the context, packing stamps the advisory content summary, and a
    // mismatching summary only warns on load — it never refuses a save.
    json packed = game::DungeonRunToJson(before);
    CHECK_OK(game::PackSections(registry, packed));
    CHECK(packed.at("content_schema") == kExpectedContentSchema);
    CHECK_OK(game::UnpackSections(registry, packed));
    auto drifted = packed;
    drifted["content_schema"] = "units:2/skills:1/runes:1/statuses:1";
    CHECK_OK(game::UnpackSections(registry, drifted));
}

void V1FixtureContinuesEndToEnd() {
    FTempDir dir;
    const std::string checkpoint = (dir.path / "checkpoint.json").string();
    {
        std::ofstream file(checkpoint + ".run.json");
        CHECK(file.good());
        file << kV1SaveFixture;
    }

    // A fresh boot finds the v1 save and the menu offers Continue...
    FSaveFixture fixture(checkpoint);
    CHECK(fixture.loaded.registry.ctx().get<game::FDungeonSave>().exists);
    CHECK(game::ContinueDungeonRun(fixture.loaded.registry));
    CheckFixtureRun(fixture.loaded.registry.ctx().get<game::FDungeonRun>());

    // ...and the load never rewrote the file: it is still the v1 save the old build left.
    CHECK(ReadJsonFile(fixture.savePath).at("schema_version") == 1);

    // The next real save upgrades the slot to v2.
    CHECK(game::SaveDungeonRun(fixture.loaded.registry));
    const auto upgraded = ReadJsonFile(fixture.savePath);
    CHECK(upgraded.at("schema_version") == 2);
    CHECK(upgraded.at("run").contains("ext") && upgraded.at("run").at("ext").is_object());

    // And that v2 file boots and continues to the same run.
    FSaveFixture reboot(checkpoint);
    CHECK(reboot.loaded.registry.ctx().get<game::FDungeonSave>().exists);
    CHECK(game::ContinueDungeonRun(reboot.loaded.registry));
    CheckFixtureRun(reboot.loaded.registry.ctx().get<game::FDungeonRun>());
}

} // namespace

int main() {
    return game_test::RunTests("save migration", {
        {"V1SaveLoadsAsV2", V1SaveLoadsAsV2},
        {"MigrationChainComposes", MigrationChainComposes},
        {"UnknownKeyIgnoredKnownKeyRequired", UnknownKeyIgnoredKnownKeyRequired},
        {"FutureVersionRejected", FutureVersionRejected},
        {"PartySubsetAccepted", PartySubsetAccepted},
        {"PartyExactStillRejected", PartyExactStillRejected},
        {"SectionRoundTrip", SectionRoundTrip},
        {"UnknownSectionPreserved", UnknownSectionPreserved},
        {"UnknownSectionDeepPreserved", UnknownSectionDeepPreserved},
        {"PreservedDroppedForDifferentRun", PreservedDroppedForDifferentRun},
        {"SectionFutureVersionRejected", SectionFutureVersionRejected},
        {"MissingSectionDefault", MissingSectionDefault},
        {"UnpackFailureRejectsLoad", UnpackFailureRejectsLoad},
        {"PackFailureAbortsSave", PackFailureAbortsSave},
        {"RoundTripV2", RoundTripV2},
        {"V1FixtureContinuesEndToEnd", V1FixtureContinuesEndToEnd},
    });
}
