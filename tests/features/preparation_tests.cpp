// WP-21a/21b: the preparation screen is the hub — roster selection (4 of the shipped 8), per-member
// initial loadouts, the single setup-driven start path, the unlock gates and the roster boot validation.
//
// Content worlds (lead decision, 2026-11-08): a test that drives the draft to a start (ValidateRunSetup /
// StartDungeonRunFromSetup) or asserts the roster boot validation's positive control needs the REAL unit
// pools — 4-slot pools and the unlocks.json gate rows — and therefore boots on the SHIPPED content. The
// frozen fixture is NOT spliced under the shipped tables: its older skill/status/ai rows do not satisfy the
// shipped tables' cross-references, so a mixed root cannot boot. Tests that only resolve ids, read gates or
// inspect a draft without validating it keep the frozen-fixture boot (TestAssetRoots()). The temp override
// root always comes first and holds only the documents the case rewrites (roster/unlocks/rewards).
//
// Everything a case writes lives under out/ and is removed when the case ends; no test touches %APPDATA%.
//
// The 21a export (ResolveRunMemberUnit) keeps its behavior-identical pin at the bottom.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>

#include "core/EngineState.h"
#include "core/InputState.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/dungeon/FRunStart.h"
#include "gameplay/meta/FMetaProgress.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FPreparationView.h"
#include "states/FBaseState.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberHubState.h"
#include "states/FEmberMenuRequest.h"
#include "states/FEmberPreparationState.h"
#include "states/FEmberTitleState.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 2101;

// ---------------------------------------------------------------------------------------------
// Scratch roots and the booted registry
// ---------------------------------------------------------------------------------------------

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

json ReadJsonFile(const std::filesystem::path& path) { return json::parse(ReadTextFile(path)); }

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    CHECK(!error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    CHECK(file.good());
    file << text;
    CHECK(file.good());
}

void WriteJsonFile(const std::filesystem::path& path, const json& document) {
    WriteTextFile(path, document.dump(2));
}

std::string ReadBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/// A per-test scratch directory under out/, removed when the case ends (pass or fail). Every file a test
/// writes — the asset overrides, the checkpoint, the run save and the meta save — lives inside it.
struct FScratchRoot {
    std::filesystem::path root;

    explicit FScratchRoot(const char* stem) {
        root = std::filesystem::path("out") / UniqueStem(stem);
        std::error_code error;
        std::filesystem::create_directories(root / "data", error);
        CHECK(!error);
    }
    ~FScratchRoot() {
        std::error_code error;
        std::filesystem::remove_all(root, error);  // best effort: a locked file must not fail the test
    }
    FScratchRoot(const FScratchRoot&) = delete;
    FScratchRoot& operator=(const FScratchRoot&) = delete;
};

/// Overrides written into <scratch>/data BEFORE the content loads (roster.json, unlocks.json, ...).
using FDataOverrides = std::function<void(const std::filesystem::path& dataDir)>;

/// The content world an FPrepBoot loads (see the file header). Shipped = {temp override root, assets}:
/// the real 4-slot pools and the shipped unlock gates, no fixture. Fixture = {temp, fixture, assets}: the
/// frozen snapshot for cases that resolve ids/gates but never validate a draft.
enum class EContentWorld { Shipped, Fixture };

/// A booted registry for the preparation rules: the temp override root first, then the chosen content
/// world. The checkpoint and both save slots live inside the scratch root, so the case is fully isolated.
struct FPrepBoot {
    FScratchRoot scratch;   // declared first: destroyed last, after the registry released its files
    entt::registry registry;

    explicit FPrepBoot(const char* stem, const FDataOverrides& overrides = {},
                       EContentWorld world = EContentWorld::Shipped)
        : scratch(stem) {
        if (overrides) overrides(scratch.root / "data");
        std::vector<std::string> roots{scratch.root.string()};
        roots.push_back(world == EContentWorld::Shipped ? kShipAssetRoot : kTestFixtureRoot);
        if (world == EContentWorld::Fixture) roots.push_back(kShipAssetRoot);
        game::FAssetManager::Initialize(registry, roots);
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, (scratch.root / "checkpoint.json").string()));
    }
    FPrepBoot(const FPrepBoot&) = delete;
    FPrepBoot& operator=(const FPrepBoot&) = delete;

    [[nodiscard]] bool InitMeta() {
        return game::InitializeMetaProgress(registry, (scratch.root / "meta_save.json").string());
    }
};

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

void PressKey(entt::registry& registry, SDL_Scancode scancode) {
    auto* input = registry.ctx().find<game::FInputState>();
    CHECK(input != nullptr);
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    (void)input->processEvent(event);
}

const game::FRosterChoice* FindChoice(const std::vector<game::FRosterChoice>& choices, std::string_view memberId) {
    for (const game::FRosterChoice& choice : choices) {
        if (choice.memberId == memberId) return &choice;
    }
    return nullptr;
}

std::vector<json> RowsOfFaction(const json& document, const char* faction) {
    std::vector<json> rows;
    for (const json& row : document.at("units")) {
        if (row.at("faction").get<std::string>() == faction) rows.push_back(row);
    }
    return rows;
}

/// The authored dungeon row for a member id (display name / cells live there).
const json* AuthoredRow(const json& authored, std::string_view memberId) {
    for (const json& row : authored) {
        if (row.at("id").get<std::string>() == memberId) return &row;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Roster selection and the roster -> battlefield pipeline
// ---------------------------------------------------------------------------------------------

void RosterSelection() {
    // A deliberately shuffled catalog (placement order must win) with a fifth selectable member and a
    // locked stub.
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.healer", "name": "Lumen", "enabled": true},
        {"id": "party.scout", "name": "Nyx", "trait": "Quick", "enabled": true, "unit": "char.archer"},
        {"id": "party.warrior", "name": "Alden", "enabled": true},
        {"id": "party.rogue_stub", "name": "???", "locked": true},
        {"id": "party.mage", "name": "Mira", "enabled": true},
        {"id": "party.archer", "name": "Rowan", "enabled": true}
    ]})";
    FPrepBoot boot("prep-roster-selection", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
    });
    entt::registry& registry = boot.registry;

    const game::FRunStartRequest request{true, -1};
    game::FPreparationDraft draft = game::MakeDefaultDraft(registry, request);
    CHECK(draft.fresh);
    CHECK(draft.maxStartCleared == 0);
    CHECK(draft.startCleared == 0);
    const std::vector<std::string> authored = {"party.warrior", "party.archer", "party.mage", "party.healer"};
    CHECK(draft.setup.party == authored);  // dungeon.json placement order, not catalog order
    CHECK(draft.setup.skills.size() == authored.size());
    const int slots = game::MemberActiveSkillSlots(registry, authored[0]);
    CHECK(slots == game::kBaseActiveSkillSlots);
    for (std::size_t i = 0; i < authored.size(); ++i) {
        const std::vector<std::string> pool = game::MemberSkillPool(registry, authored[i]);
        CHECK(static_cast<int>(pool.size()) >= slots);
        const std::vector<std::string> expected(pool.begin(), pool.begin() + slots);
        CHECK(draft.setup.skills[i] == expected);  // pool-first-slots, order preserved
        std::vector<std::string> unique = draft.setup.skills[i];
        std::ranges::sort(unique);
        CHECK(std::ranges::adjacent_find(unique) == unique.end());
    }
    CHECK(!game::ValidateRunSetup(registry, draft.setup).has_value());  // startable as-is

    // Toggle off: the member and its loadout leave together.
    CHECK(game::ToggleDraftMember(registry, draft, "party.mage"));
    CHECK(draft.setup.party == std::vector<std::string>({"party.warrior", "party.archer", "party.healer"}));
    CHECK(draft.setup.skills.size() == 3);
    // Toggle on: appended (placement order is the draft's order) with its default loadout.
    CHECK(game::ToggleDraftMember(registry, draft, "party.mage"));
    CHECK(draft.setup.party == std::vector<std::string>({"party.warrior", "party.archer", "party.healer", "party.mage"}));
    CHECK(draft.setup.skills[3] == game::DefaultMemberLoadout(registry, "party.mage"));
    // The party is full: a fifth selectable member is refused and the draft is untouched.
    CHECK(!game::ToggleDraftMember(registry, draft, "party.scout"));
    CHECK(draft.setup.party.size() == game::kRunPartySize);
    // A locked stub is refused even with room; the selectable scout is not.
    CHECK(game::ToggleDraftMember(registry, draft, "party.warrior"));  // make room
    CHECK(!game::ToggleDraftMember(registry, draft, "party.rogue_stub"));
    CHECK(draft.setup.party.size() == game::kRunPartySize - 1);
    CHECK(game::IsRosterMemberSelectable(registry, "party.scout"));
    CHECK(!game::IsRosterMemberSelectable(registry, "party.rogue_stub"));
    CHECK(game::ToggleDraftMember(registry, draft, "party.scout"));
    CHECK(draft.setup.party.back() == "party.scout");
    CHECK(draft.setup.skills.back() == game::DefaultMemberLoadout(registry, "party.scout"));
    CHECK(!game::ValidateRunSetup(registry, draft.setup).has_value());

    // The catalog view classifies by the same rule.
    const std::vector<game::FRosterChoice> choices = game::BuildRosterChoices(registry);
    CHECK(choices.size() == 6);
    CHECK(choices.front().memberId == "party.healer");  // catalog order, not placement order
    const game::FRosterChoice* scout = FindChoice(choices, "party.scout");
    CHECK(scout != nullptr);
    CHECK(scout->availability == game::ERosterAvailability::Selectable);
    CHECK(scout->unitId == "char.archer");
    const game::FRosterChoice* stub = FindChoice(choices, "party.rogue_stub");
    CHECK(stub != nullptr);
    CHECK(stub->availability == game::ERosterAvailability::LockedStub);
    CHECK(!stub->reason.empty());
}

void RosterSelectionReachesBattlefield() {
    FPrepBoot boot("prep-reaches-battlefield");
    entt::registry& registry = boot.registry;
    const json& authored = game::DungeonDefinition(registry).at("party");
    CHECK(authored.size() == game::kRunPartySize);

    // The authored document for the same seed: the generation baseline.
    game::StartDungeonRun(registry, kSeed, true);
    const json baseline = game::BuildDungeonBattlefield(registry);
    CHECK(RowsOfFaction(baseline, "party").size() == game::kRunPartySize);

    // A reordered roster party with a non-default warrior loadout.
    game::FRunSetup setup;
    for (const char* id : {"party.healer", "party.warrior", "party.mage", "party.archer"}) {
        setup.party.emplace_back(id);
    }
    for (const std::string& memberId : setup.party) {
        std::vector<std::string> loadout = game::DefaultMemberLoadout(registry, memberId);
        CHECK(loadout.size() == static_cast<std::size_t>(game::MemberActiveSkillSlots(registry, memberId)));
        setup.skills.push_back(std::move(loadout));
    }
    const std::vector<std::string> warriorPool = game::MemberSkillPool(registry, "party.warrior");
    CHECK(warriorPool.size() >= 5);
    setup.skills[1] = {warriorPool[4], warriorPool[1], warriorPool[2], warriorPool[3]};  // a different set AND order
    CHECK(setup.skills[1] != game::DefaultMemberLoadout(registry, "party.warrior"));
    CHECK(game::StartDungeonRunFromSetup(registry, kSeed, true, -1, setup));

    // The run carries the member ids, names and exactly the chosen skills, in setup order.
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.party.size() == setup.party.size());
    for (std::size_t i = 0; i < setup.party.size(); ++i) {
        CHECK(run.party[i].id == setup.party[i]);
        const json* row = AuthoredRow(authored, setup.party[i]);
        CHECK(row != nullptr);
        CHECK(run.party[i].displayName == row->at("display_name").get<std::string>());
        CHECK(run.party[i].skills.size() == setup.skills[i].size());
        for (std::size_t slot = 0; slot < setup.skills[i].size(); ++slot) {
            CHECK(run.party[i].skills[slot].skillId == setup.skills[i][slot]);
        }
    }

    // The battlefield document: placement ids in setup order, the resolved unit, the authored cells.
    const json document = game::BuildDungeonBattlefield(registry);
    const std::vector<json> partyRows = RowsOfFaction(document, "party");
    CHECK(partyRows.size() == game::kRunPartySize);
    for (std::size_t i = 0; i < partyRows.size(); ++i) {
        CHECK(partyRows[i].at("id").get<std::string>() == setup.party[i]);
        const auto* unit = game::ResolveRunMemberUnit(registry, setup.party[i]);
        CHECK(unit != nullptr);
        CHECK(partyRows[i].at("unit").get<std::string>() == unit->id);
        const json* row = AuthoredRow(authored, setup.party[i]);
        CHECK(row != nullptr);
        CHECK(partyRows[i].at("display_name") == row->at("display_name"));
        CHECK(partyRows[i].at("grid_x") == authored[i].at("grid_x"));
        CHECK(partyRows[i].at("grid_y") == authored[i].at("grid_y"));
    }
    // Generation does not depend on the party: the same seed produces the same room, terrain and enemies.
    CHECK(document.at("name") == baseline.at("name"));
    CHECK(document.at("terrain") == baseline.at("terrain"));
    CHECK(RowsOfFaction(document, "enemy") == RowsOfFaction(baseline, "enemy"));
}

void InstallKeepsAuthoredPartyWhenNoSetup() {
    // The no-setup path (debug/fresh boots, every pre-21b call site) keeps the authored dungeon party and
    // records no skills — the combat fallback supplies the authored loadout at battle time.
    FPrepBoot boot("prep-authored-fallback", {}, EContentWorld::Fixture);
    entt::registry& registry = boot.registry;
    const json& authored = game::DungeonDefinition(registry).at("party");
    CHECK(!registry.ctx().contains<game::FRunSetupRequest>());

    game::StartDungeonRun(registry, kSeed, true);
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.party.size() == authored.size());
    for (std::size_t i = 0; i < authored.size(); ++i) {
        CHECK(run.party[i].id == authored[i].at("id").get<std::string>());
        CHECK(run.party[i].displayName == authored[i].at("display_name").get<std::string>());
        CHECK(run.party[i].skills.empty());
    }

    const json document = game::BuildDungeonBattlefield(registry);
    const std::vector<json> partyRows = RowsOfFaction(document, "party");
    CHECK(partyRows.size() == authored.size());
    for (std::size_t i = 0; i < authored.size(); ++i) {
        CHECK(partyRows[i].at("id") == authored[i].at("id"));
        const std::string unitId = authored[i].at("unit").get<std::string>();
        CHECK(partyRows[i].at("unit").get<std::string>() == unitId);
        const auto* unit = game::ResolveRunMemberUnit(registry, partyRows[i].at("id").get<std::string>());
        CHECK(unit != nullptr && unit->id == unitId);
        CHECK(partyRows[i].at("grid_x") == authored[i].at("grid_x"));
        CHECK(partyRows[i].at("grid_y") == authored[i].at("grid_y"));
    }
}

void SubsetSaveRoundTrip() {
    FPrepBoot boot("prep-subset-save");
    entt::registry& registry = boot.registry;

    game::FPreparationDraft draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});
    CHECK(draft.setup.party.size() == game::kRunPartySize);
    std::swap(draft.setup.party[0], draft.setup.party[1]);   // a roster-selected order, not the authored one
    std::swap(draft.setup.skills[0], draft.setup.skills[1]);
    CHECK(draft.setup.party[1] == "party.warrior");
    const std::vector<std::string> pool = game::MemberSkillPool(registry, "party.warrior");
    CHECK(pool.size() >= 5);
    CHECK(game::SetDraftSkill(registry, draft, 1, 0, pool[4]));  // a non-default, pool-legal pick
    const std::string customWarriorSkill = pool[4];

    CHECK(game::StartDungeonRunFromSetup(registry, kSeed, true, -1, draft.setup));
    const std::string savePath = registry.ctx().get<game::FDungeonSave>().path;
    CHECK(!savePath.empty());
    CHECK(std::filesystem::exists(savePath));

    // Save -> continue keeps the roster-selected party and every chosen skill.
    registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(registry));
    const auto& resumed = registry.ctx().get<game::FDungeonRun>();
    CHECK(resumed.party.size() == draft.setup.party.size());
    for (std::size_t i = 0; i < resumed.party.size(); ++i) {
        CHECK(resumed.party[i].id == draft.setup.party[i]);
        CHECK(resumed.party[i].skills.size() == draft.setup.skills[i].size());
        for (std::size_t slot = 0; slot < resumed.party[i].skills.size(); ++slot) {
            CHECK(resumed.party[i].skills[slot].skillId == draft.setup.skills[i][slot]);
        }
    }
    CHECK(resumed.party[1].skills[0].skillId == customWarriorSkill);

    // A hand-edited save naming an id outside dungeon ∪ roster is rejected whole.
    json document = ReadJsonFile(savePath);
    document["run"]["party"][0]["id"] = "party.ghost";
    WriteJsonFile(savePath, document);
    registry.ctx().erase<game::FDungeonRun>();
    CHECK(!game::ContinueDungeonRun(registry));
    CHECK(!registry.ctx().get<game::FDungeonSave>().exists);
    CHECK(!registry.ctx().get<game::FDungeonSave>().error.empty());
    CHECK(!registry.ctx().contains<game::FDungeonRun>());

    // An old authored-4 save still loads: Subset widens WHICH ids are legal, never the size.
    game::StartDungeonRun(registry, kSeed, true);
    const auto& authoredRun = registry.ctx().get<game::FDungeonRun>();
    std::vector<std::string> authoredIds;
    for (const game::FRunMember& member : authoredRun.party) authoredIds.push_back(member.id);
    registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(registry));
    const auto& resumedAuthored = registry.ctx().get<game::FDungeonRun>();
    CHECK(resumedAuthored.party.size() == authoredIds.size());
    for (std::size_t i = 0; i < authoredIds.size(); ++i) {
        CHECK(resumedAuthored.party[i].id == authoredIds[i]);
    }
}

void LockedStubNeedsNoUnit() {
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.warrior", "name": "Alden", "enabled": true},
        {"id": "party.archer", "name": "Rowan", "enabled": true},
        {"id": "party.mage", "name": "Mira", "enabled": true},
        {"id": "party.healer", "name": "Lumen", "enabled": true},
        {"id": "party.rogue_stub", "name": "???", "locked": true},
        {"id": "party.warden_stub", "name": "???", "locked": true}
    ]})";
    FPrepBoot boot("prep-locked-stub", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
    });
    entt::registry& registry = boot.registry;

    // The stubs carry no unit and no pool, yet the boot validation passes (they are skipped by design)...
    CHECK(!game::ValidateRosterContent(registry).has_value());
    // ...and they can never be resolved or fielded.
    CHECK(game::ResolveRunMemberUnit(registry, "party.rogue_stub") == nullptr);
    CHECK(!game::IsRosterMemberSelectable(registry, "party.rogue_stub"));
    const std::vector<game::FRosterChoice> choices = game::BuildRosterChoices(registry);
    const game::FRosterChoice* stub = FindChoice(choices, "party.rogue_stub");
    CHECK(stub != nullptr);
    CHECK(stub->availability == game::ERosterAvailability::LockedStub);
    CHECK(stub->unitId.empty());
    CHECK(!stub->reason.empty());
    const game::FRosterChoice* warrior = FindChoice(choices, "party.warrior");
    CHECK(warrior != nullptr);
    CHECK(warrior->availability == game::ERosterAvailability::Selectable);

    // Even with an open party slot the stub cannot join.
    game::FPreparationDraft draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});
    CHECK(draft.setup.party.size() == game::kRunPartySize);
    CHECK(game::ToggleDraftMember(registry, draft, "party.mage"));
    CHECK(!game::ToggleDraftMember(registry, draft, "party.rogue_stub"));
    CHECK(draft.setup.party.size() == game::kRunPartySize - 1);
}

void BattlefieldUsesResolvedUnit() {
    // The roster row's `unit` wins over dungeon party[]; the battlefield document must carry the RESOLVED
    // unit (never the authored dungeon unit).
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.warrior", "name": "Alden", "enabled": true, "unit": "char.archer"},
        {"id": "party.archer", "name": "Rowan", "enabled": true},
        {"id": "party.mage", "name": "Mira", "enabled": true},
        {"id": "party.healer", "name": "Lumen", "enabled": true}
    ]})";
    FPrepBoot boot("prep-battlefield-unit", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
    });
    entt::registry& registry = boot.registry;

    const game::FPreparationDraft draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});
    CHECK(draft.setup.party == std::vector<std::string>({"party.warrior", "party.archer", "party.mage", "party.healer"}));
    CHECK(game::StartDungeonRunFromSetup(registry, kSeed, true, -1, draft.setup));

    const json document = game::BuildDungeonBattlefield(registry);
    const std::vector<json> partyRows = RowsOfFaction(document, "party");
    CHECK(partyRows.size() == game::kRunPartySize);
    CHECK(partyRows[0].at("id").get<std::string>() == "party.warrior");
    CHECK(partyRows[0].at("unit").get<std::string>() == "char.archer");
    CHECK(partyRows[0].at("unit").get<std::string>() != "char.warrior");
    const auto* content = registry.ctx().find<game::gameplay::FContentRegistry>();
    CHECK(content != nullptr);
    CHECK(partyRows[0].at("unit").get<std::string>() == game::ResolveRunMemberUnit(registry, "party.warrior")->id);
}

// ---------------------------------------------------------------------------------------------
// The single resolver
// ---------------------------------------------------------------------------------------------

void ResolverPrefersRosterUnit() {
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.warrior", "name": "Alden", "enabled": true, "unit": "char.mage"}
    ]})";
    FPrepBoot boot("prep-resolver-roster", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
    }, EContentWorld::Fixture);
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const game::gameplay::FUnitContent* unit = game::ResolveRunMemberUnit(boot.registry, "party.warrior");
    CHECK(unit == content.units.find("char.mage"));
    CHECK(unit != content.units.find("char.warrior"));
    CHECK(game::ResolveRunMemberUnit(boot.registry, "party.ghost") == nullptr);
}

void ResolverFallsBackToDungeonParty() {
    // (a) A roster row WITHOUT a `unit` key: dungeon party[] decides.
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.warrior", "name": "Alden", "enabled": true}
    ]})";
    FPrepBoot boot("prep-resolver-fallback", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
    }, EContentWorld::Fixture);
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    CHECK(game::ResolveRunMemberUnit(boot.registry, "party.warrior") == content.units.find("char.warrior"));

    // (b) No roster document at all (fixture root only): the same fallback applies.
    entt::registry bare;
    game::FAssetManager::Initialize(bare, {kTestFixtureRoot});
    CHECK(game::gameplay::FGameplayServices::Initialize(bare));
    const auto& bareContent = bare.ctx().get<game::gameplay::FContentRegistry>();
    CHECK(game::ResolveRunMemberUnit(bare, "party.warrior") == bareContent.units.find("char.warrior"));
    CHECK(game::ResolveRunMemberUnit(bare, "party.ghost") == nullptr);
}

void MaxHealthUsesRosterUnit() {
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.warrior", "name": "Alden", "enabled": true, "unit": "char.mage"},
        {"id": "party.scout", "name": "Nyx", "enabled": true, "unit": "char.archer"}
    ]})";
    FPrepBoot boot("prep-max-health", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
    }, EContentWorld::Fixture);
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const game::gameplay::FUnitContent* archer = content.units.find("char.archer");
    const game::gameplay::FUnitContent* mage = content.units.find("char.mage");
    CHECK(archer != nullptr && mage != nullptr);

    // A roster-only member (not in dungeon party[]): its max HP is its roster unit's base.
    game::FRunMember scout;
    scout.id = "party.scout";
    CHECK(game::DungeonMemberMaxHealth(boot.registry, scout) == static_cast<int>(archer->maxHealth));

    // A dungeon member whose roster row names another unit: the roster unit wins over dungeon party[].
    game::FRunMember warrior;
    warrior.id = "party.warrior";
    CHECK(game::ResolveRunMemberUnit(boot.registry, "party.warrior") == mage);
    CHECK(game::DungeonMemberMaxHealth(boot.registry, warrior) == static_cast<int>(mage->maxHealth));
}

void LockedRowWithUnitResolvesNull() {
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.warrior", "name": "???", "locked": true, "unit": "char.mage"}
    ]})";
    FPrepBoot boot("prep-locked-unit", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
    }, EContentWorld::Fixture);
    // A locked stub resolves to nullptr even when it carries a unit key: locked rows cannot be fielded.
    CHECK(game::ResolveRunMemberUnit(boot.registry, "party.warrior") == nullptr);
    CHECK(!game::IsRosterMemberSelectable(boot.registry, "party.warrior"));
    const std::vector<game::FRosterChoice> choices = game::BuildRosterChoices(boot.registry);
    const game::FRosterChoice* stub = FindChoice(choices, "party.warrior");
    CHECK(stub != nullptr);
    CHECK(stub->availability == game::ERosterAvailability::LockedStub);
    CHECK(stub->unitId.empty());
}

// ---------------------------------------------------------------------------------------------
// Boot validation
// ---------------------------------------------------------------------------------------------

std::string ScoutRoster(const char* unitId) {
    return std::string(R"({"schema_version": 1, "roster": [
        {"id": "party.scout", "name": "Nyx", "enabled": true, "unit": ")") + unitId + R"("}
    ]})";
}

void RosterUnitMissingFailsBoot() {
    FPrepBoot broken("prep-boot-missing-unit", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", ScoutRoster("char.ghost"));
    });
    const std::optional<std::string> problem = game::ValidateRosterContent(broken.registry);
    CHECK(problem.has_value());
    CHECK(problem->find("party.scout") != std::string::npos);

    // The positive control: the same roster with a resolvable unit passes (needs the real pools, so this
    // pair boots on the shipped content).
    FPrepBoot fixed("prep-boot-missing-unit-ok", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", ScoutRoster("char.mage"));
    });
    CHECK(!game::ValidateRosterContent(fixed.registry).has_value());
}

void SkillPoolShorterThanSlotsFailsBoot() {
    const FDataOverrides kOverrides = [](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", ScoutRoster("char.healer"));
        // Shrink the healer's authored pool below the 4 active slots: the member must fail the boot check.
        json units = ReadJsonFile("assets/data/units.json");
        for (json& row : units.at("units")) {
            if (row.at("id").get<std::string>() == "char.healer") {
                row["skills"] = json::array({"skill.heal", "skill.renew", "skill.buff"});
            }
        }
        WriteJsonFile(data / "units.json", units);
    };
    FPrepBoot shortPool("prep-boot-short-pool", kOverrides);
    const std::optional<std::string> problem = game::ValidateRosterContent(shortPool.registry);
    CHECK(problem.has_value());
    CHECK(problem->find("party.scout") != std::string::npos);

    // The positive control: the shipped healer pool (5 entries) satisfies the check for the same roster.
    FPrepBoot fullPool("prep-boot-short-pool-ok", [](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", ScoutRoster("char.healer"));
    });
    CHECK(!game::ValidateRosterContent(fullPool.registry).has_value());
}

void UnlockGatedEntryValidatedAtBoot() {
    const char* kUnlocks = R"({"schema_version": 1, "unlocks": [
        {"id": "party.scout", "kind": "roster", "cost": 3, "seal_gate": 0}
    ]})";
    FPrepBoot gated("prep-boot-gated", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", ScoutRoster("char.ghost"));
        WriteTextFile(data / "unlocks.json", kUnlocks);
    });
    // The entry is unlock-gated — and the boot validation still checks it: an unlock target that cannot
    // resolve must stop the boot (it would be un-fieldable right after the purchase).
    CHECK(game::IsUnlockGated(gated.registry, "party.scout"));
    const std::optional<std::string> problem = game::ValidateRosterContent(gated.registry);
    CHECK(problem.has_value());
    CHECK(problem->find("party.scout") != std::string::npos);

    // The positive control: a resolvable unit passes even though the entry stays gated (unpurchased).
    FPrepBoot fixed("prep-boot-gated-ok", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", ScoutRoster("char.mage"));
        WriteTextFile(data / "unlocks.json", kUnlocks);
    });
    CHECK(game::IsUnlockGated(fixed.registry, "party.scout"));
    CHECK(!game::ValidateRosterContent(fixed.registry).has_value());
}

void ShippedRosterContentValid() {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    CHECK(game::gameplay::FGameplayServices::Initialize(registry));
    const std::vector<game::FMetaRosterRow> catalog = game::MetaRosterCatalog(registry);
    CHECK(catalog.size() == 8);
    int locked = 0;
    for (const game::FMetaRosterRow& row : catalog) locked += row.locked ? 1 : 0;
    CHECK(locked == 4);
    CHECK(!game::ValidateRosterContent(registry).has_value());
}

// ---------------------------------------------------------------------------------------------
// Unlock gates
// ---------------------------------------------------------------------------------------------

void RosterUnlockGatesSelection() {
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.warrior", "name": "Alden", "enabled": true},
        {"id": "party.archer", "name": "Rowan", "enabled": true},
        {"id": "party.mage", "name": "Mira", "enabled": true},
        {"id": "party.healer", "name": "Lumen", "enabled": true},
        {"id": "party.scout", "name": "Nyx", "trait": "Quick", "enabled": true, "unit": "char.archer"}
    ]})";
    const char* kUnlocks = R"({"schema_version": 1, "unlocks": [
        {"id": "party.scout", "kind": "roster", "cost": 5, "seal_gate": 0}
    ]})";
    FPrepBoot boot("prep-roster-unlock", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
        WriteTextFile(data / "unlocks.json", kUnlocks);
    }, EContentWorld::Fixture);
    entt::registry& registry = boot.registry;
    CHECK(boot.InitMeta());

    // Before the unlock: NeedsUnlock with the catalog's id and cost, and not selectable.
    std::vector<game::FRosterChoice> choices = game::BuildRosterChoices(registry);
    const game::FRosterChoice* scout = FindChoice(choices, "party.scout");
    CHECK(scout != nullptr);
    CHECK(scout->availability == game::ERosterAvailability::NeedsUnlock);
    CHECK(scout->reason == "Unlock party.scout for 5 seals");
    CHECK(scout->unitId == "char.archer");
    CHECK(!game::IsRosterMemberSelectable(registry, "party.scout"));
    CHECK(!game::HasMetaUnlock(registry, "party.scout"));
    game::FPreparationDraft draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});
    CHECK(draft.setup.party.size() == game::kRunPartySize);
    CHECK(game::ToggleDraftMember(registry, draft, "party.mage"));  // make room
    CHECK(!game::ToggleDraftMember(registry, draft, "party.scout"));
    CHECK(draft.setup.party.size() == game::kRunPartySize - 1);

    // After the grant the member is selectable and joins the draft with its default loadout.
    game::GrantMetaUnlock(registry, "party.scout");
    CHECK(game::HasMetaUnlock(registry, "party.scout"));
    CHECK(game::IsRosterMemberSelectable(registry, "party.scout"));
    choices = game::BuildRosterChoices(registry);
    scout = FindChoice(choices, "party.scout");
    CHECK(scout != nullptr);
    CHECK(scout->availability == game::ERosterAvailability::Selectable);
    CHECK(scout->reason.empty());
    CHECK(game::ToggleDraftMember(registry, draft, "party.scout"));
    CHECK(draft.setup.party.back() == "party.scout");
    CHECK(draft.setup.skills.back() == game::DefaultMemberLoadout(registry, "party.scout"));
}

void LockedStubNotOpenedByUnlock() {
    const char* kRoster = R"({"schema_version": 1, "roster": [
        {"id": "party.warrior", "name": "Alden", "enabled": true},
        {"id": "party.rogue_stub", "name": "???", "locked": true, "unit": "char.archer"}
    ]})";
    const char* kUnlocks = R"({"schema_version": 1, "unlocks": [
        {"id": "party.rogue_stub", "kind": "roster", "cost": 8, "seal_gate": 8}
    ]})";
    FPrepBoot boot("prep-stub-unlock", [&](const std::filesystem::path& data) {
        WriteTextFile(data / "roster.json", kRoster);
        WriteTextFile(data / "unlocks.json", kUnlocks);
    }, EContentWorld::Fixture);
    entt::registry& registry = boot.registry;
    CHECK(boot.InitMeta());

    // A locked stub resolves to nullptr even with a unit key...
    CHECK(game::ResolveRunMemberUnit(registry, "party.rogue_stub") == nullptr);
    // ...and unlocking it changes nothing: the stub stays a locked placeholder, never a selectable member.
    game::GrantMetaUnlock(registry, "party.rogue_stub");
    CHECK(game::HasMetaUnlock(registry, "party.rogue_stub"));
    CHECK(!game::IsRosterMemberSelectable(registry, "party.rogue_stub"));
    const std::vector<game::FRosterChoice> choices = game::BuildRosterChoices(registry);
    const game::FRosterChoice* stub = FindChoice(choices, "party.rogue_stub");
    CHECK(stub != nullptr);
    CHECK(stub->availability == game::ERosterAvailability::LockedStub);
    CHECK(stub->unitId.empty());
    // The normal member is unaffected (positive control).
    const game::FRosterChoice* warrior = FindChoice(choices, "party.warrior");
    CHECK(warrior != nullptr);
    CHECK(warrior->availability == game::ERosterAvailability::Selectable);

    // Even an open party slot does not let the stub in.
    game::FPreparationDraft draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});
    CHECK(game::ToggleDraftMember(registry, draft, "party.mage"));
    CHECK(!game::ToggleDraftMember(registry, draft, "party.rogue_stub"));
    CHECK(std::ranges::find(draft.setup.party, "party.rogue_stub") == draft.setup.party.end());
}

void SkillPoolHidesLockedUnlock() {
    FPrepBoot boot("prep-skill-gate");  // shipped unit tables + the shipped unlocks catalog; meta not yet up
    entt::registry& registry = boot.registry;
    CHECK(boot.InitMeta());

    // Nothing bought: the gated skills are absent from the pool, the ungated ones are present.
    std::vector<std::string> pool = game::MemberSkillPool(registry, "party.warrior");
    CHECK(std::ranges::find(pool, "skill.strike") != pool.end());
    CHECK(std::ranges::find(pool, "skill.hook") == pool.end());
    CHECK(std::ranges::find(pool, "skill.bash") == pool.end());
    game::FPreparationDraft draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});
    CHECK(draft.setup.party.front() == "party.warrior");
    CHECK(!game::SetDraftSkill(registry, draft, 0, 0, "skill.hook"));  // hidden: cannot be picked

    // After the unlock the skill appears in the pool and can be picked into a slot.
    game::GrantMetaUnlock(registry, "skill.hook");
    CHECK(game::HasMetaUnlock(registry, "skill.hook"));
    pool = game::MemberSkillPool(registry, "party.warrior");
    CHECK(std::ranges::find(pool, "skill.hook") != pool.end());
    CHECK(std::ranges::find(pool, "skill.bash") == pool.end());  // only the bought unlock opens
    CHECK(game::SetDraftSkill(registry, draft, 0, 0, "skill.hook"));
    CHECK(draft.setup.skills[0][0] == "skill.hook");

    // Meta never initialized: nothing is unlocked, so the same rows stay hidden.
    FPrepBoot bare("prep-skill-gate-bare");
    CHECK(game::FindMetaSave(bare.registry) == nullptr);
    const std::vector<std::string> barePool = game::MemberSkillPool(bare.registry, "party.warrior");
    CHECK(std::ranges::find(barePool, "skill.strike") != barePool.end());
    CHECK(std::ranges::find(barePool, "skill.hook") == barePool.end());
    CHECK(std::ranges::find(barePool, "skill.bash") == barePool.end());
}

void FourSlotsEnforced() {
    FPrepBoot boot("prep-four-slots");
    entt::registry& registry = boot.registry;
    const json& authored = game::DungeonDefinition(registry).at("party");

    game::FRunSetup setup;
    for (const json& row : authored) {
        setup.party.push_back(row.at("id").get<std::string>());
        setup.skills.push_back(game::DefaultMemberLoadout(registry, setup.party.back()));
    }
    CHECK(game::MemberActiveSkillSlots(registry, setup.party[0]) == 4);
    CHECK(setup.skills[0].size() == 4);

    // Three skills: refused with the slot contract's reason, and no run is installed.
    setup.skills[0].pop_back();
    const std::optional<std::string> problem = game::ValidateRunSetup(registry, setup);
    CHECK(problem.has_value());
    CHECK(problem->find("4") != std::string::npos);
    CHECK(!game::StartDungeonRunFromSetup(registry, kSeed, true, -1, setup));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    CHECK(!registry.ctx().contains<game::FRunSetupRequest>());  // refused before the carrier is inserted

    // Four skills: the run records exactly the chosen skills, in order.
    setup.skills[0].push_back(game::MemberSkillPool(registry, setup.party[0])[3]);
    CHECK(!game::ValidateRunSetup(registry, setup).has_value());
    CHECK(game::StartDungeonRunFromSetup(registry, kSeed, true, -1, setup));
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.party.size() == setup.party.size());
    for (std::size_t i = 0; i < setup.party.size(); ++i) {
        CHECK(run.party[i].skills.size() == 4);
        for (std::size_t slot = 0; slot < 4; ++slot) {
            CHECK(run.party[i].skills[slot].skillId == setup.skills[i][slot]);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The start path
// ---------------------------------------------------------------------------------------------

void PrepareConsumesCarrierOnly() {
    FPrepBoot boot("prep-carrier");
    entt::registry& registry = boot.registry;
    registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{true, -1});

    FEmberPreparationState prepare;
    // The first update consumes the carrier; without a confirm nothing starts.
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{}));
    CHECK(!registry.ctx().contains<game::FRunStartRequest>());
    CHECK(!registry.ctx().contains<game::FDungeonRun>());

    // A later start uses the already-consumed request (fresh = depth 1).
    registry.ctx().insert_or_assign<game::FPreparationCommand>(game::FPreparationCommand{game::EPreparationCommand::Start});
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)}));
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.depth == 1 && run.cleared == 0);
}

void PrepareDoesNotSkipWithExistingRun() {
    FPrepBoot boot("prep-existing-run");
    entt::registry& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    registry.ctx().get<game::FDungeonRun>().materials = 777;

    registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{true, -1});
    FEmberPreparationState prepare;
    // A run is already installed: the hub refuses and hands back to the title instead of starting a second.
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
    CHECK(!registry.ctx().contains<game::FRunStartRequest>());  // consumed once, also on the guard path
    CHECK(registry.ctx().get<game::FDungeonRun>().materials == 777);
}

void PrepareStartsExactlyOneRunFromSetup() {
    FPrepBoot boot("prep-one-run");
    entt::registry& registry = boot.registry;
    registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{true, -1});
    FEmberPreparationState prepare;

    // Update with no command: the draft is built, no run is created.
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());

    // The confirm (the view's Start command) starts exactly one run and hands to the battle.
    registry.ctx().insert_or_assign<game::FPreparationCommand>(game::FPreparationCommand{game::EPreparationCommand::Start});
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)}));
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());  // taken and erased: no second start
    auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.depth == 1 && run.room == 0);
    CHECK(run.party.size() == game::kRunPartySize);

    // The next update refuses (a run exists) and returns to the title. The marker proves no second run was
    // installed (a reinstall would reset the materials).
    const std::uint32_t seed = run.seed;
    run.materials = 777;
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
    const auto& after = registry.ctx().get<game::FDungeonRun>();
    CHECK(after.seed == seed && after.materials == 777);
    CHECK(after.party.size() == game::kRunPartySize);
}

void PrepareCancelReturnsToTitle() {
    FPrepBoot boot("prep-cancel");
    entt::registry& registry = boot.registry;
    registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{false, -1});
    registry.ctx().insert_or_assign<game::FPreparationCommand>(game::FPreparationCommand{game::EPreparationCommand::Cancel});

    FEmberPreparationState prepare;
    CHECK((prepare.Update(registry, 0.f) == FStateTransition{EStateChangeKind::Root, std::string(kEmberTitleId)}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    CHECK(!registry.ctx().contains<game::FRunStartRequest>());
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());
}

void HubConfirmDoesNotStartBattle() {
    FPrepBoot boot("prep-hub-confirm", {}, EContentWorld::Fixture);  // no draft involved
    entt::registry& registry = boot.registry;
    game::FInputState::Initialize(registry);
    PressKey(registry, SDL_SCANCODE_RETURN);

    FEmberHubState hub;
    // WP-21 §5: the Confirm shortcut into a battle is gone — the hub returns no transition...
    CHECK((hub.Update(registry, 0.f) == FStateTransition{}));
    // ...so no run and no save can appear behind the preparation screen's back.
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    CHECK(!registry.ctx().get<game::FDungeonSave>().exists);
}

void ConfirmWithSaveDoesNotOverwrite() {
    FPrepBoot boot("prep-confirm-save", {}, EContentWorld::Fixture);  // authored-path run, no draft
    entt::registry& registry = boot.registry;
    game::StartDungeonRun(registry, kSeed, true);
    const std::string savePath = registry.ctx().get<game::FDungeonSave>().path;
    const std::string before = ReadBytes(savePath);
    CHECK(!before.empty());

    // A fresh session: a valid save waits, no run is installed.
    registry.ctx().erase<game::FDungeonRun>();
    CHECK(registry.ctx().get<game::FDungeonSave>().exists);
    game::FInputState::Initialize(registry);
    PressKey(registry, SDL_SCANCODE_RETURN);

    FEmberHubState hub;
    CHECK((hub.Update(registry, 0.f) == FStateTransition{}));
    CHECK(!registry.ctx().contains<game::FDungeonRun>());
    CHECK(ReadBytes(savePath) == before);  // byte-identical: the shortcut cannot bypass the overwrite check
    CHECK(registry.ctx().get<game::FDungeonSave>().exists);
}

void DebugBattleStartUsesAuthoredLoadout() {
    // The documented bypass (GAME01P_START=battle / debug): the battle entered without a run starts the
    // authored party itself — authored ids, names and no recorded skills (the combat fallback loadout).
    FPrepBoot boot("prep-debug-battle", {}, EContentWorld::Fixture);  // nothing validates a draft on this path
    entt::registry& registry = boot.registry;
    const json& authored = game::DungeonDefinition(registry).at("party");
    CHECK(!registry.ctx().contains<game::FDungeonRun>());

    game::ecs::TurnSystem turns;
    turns.onRegister(registry);
    FEmberBattleState battle;
    battle.OnEnter(registry);

    CHECK(registry.ctx().contains<game::FDungeonRun>());
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.phase == game::EDungeonPhase::Battle);
    CHECK(run.party.size() == authored.size());
    for (std::size_t i = 0; i < authored.size(); ++i) {
        CHECK(run.party[i].id == authored[i].at("id").get<std::string>());
        CHECK(run.party[i].displayName == authored[i].at("display_name").get<std::string>());
        CHECK(run.party[i].skills.empty());
    }
    battle.OnExit(registry);
}

void ContinueSkipsPreparation() {
    FPrepBoot boot("prep-continue", {}, EContentWorld::Fixture);  // the authored-4 save round trip
    entt::registry& registry = boot.registry;
    game::StartDungeonRun(registry, kSeed, true);
    const std::uint32_t seed = registry.ctx().get<game::FDungeonRun>().seed;
    registry.ctx().erase<game::FDungeonRun>();

    registry.ctx().get<game::FMenuRequest>().value = game::EMenuRequest::Continue;
    const std::optional<FStateTransition> next = ResolveEmberMenuRequest(registry);
    CHECK(next.has_value());
    CHECK(next->kind == EStateChangeKind::Root);
    CHECK(next->targetId == std::string(kEmberBattleId));
    CHECK(next->targetId != std::string(kEmberPrepareId));  // Continue never re-enters the hub
    CHECK(registry.ctx().contains<game::FDungeonRun>());
    CHECK(registry.ctx().get<game::FDungeonRun>().seed == seed);
    CHECK(!registry.ctx().contains<game::FRunStartRequest>());
}

// ---------------------------------------------------------------------------------------------
// D-21 / D-7: the start-dungeon choice and the start support
// ---------------------------------------------------------------------------------------------

void StartAtLowerDungeonWithinCheckpoint() {
    FPrepBoot boot("prep-lower-start");
    entt::registry& registry = boot.registry;
    registry.ctx().get<game::FDungeonCheckpoint>().cleared = 3;

    const game::FRunStartRequest request{false, 1};  // the D-21 choice carried in from the menu
    game::FPreparationDraft draft = game::MakeDefaultDraft(registry, request);
    CHECK(draft.startCleared == 1);
    CHECK(draft.maxStartCleared == 3);
    CHECK(!game::SetDraftStartCleared(draft, 4));  // above the checkpoint: rejected
    CHECK(draft.startCleared == 1);
    CHECK(game::SetDraftStartCleared(draft, 2));

    CHECK(game::StartDungeonRunFromSetup(registry, kSeed, draft.fresh, draft.startCleared, draft.setup));
    const auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.depth == 3);
    CHECK(run.cleared == 2);
}

void CheckpointNeverLowers() {
    FPrepBoot boot("prep-checkpoint");
    entt::registry& registry = boot.registry;
    registry.ctx().get<game::FDungeonCheckpoint>().cleared = 2;

    game::FPreparationDraft lower = game::MakeDefaultDraft(registry, game::FRunStartRequest{false, 0});
    CHECK(lower.startCleared == 0);
    CHECK(game::StartDungeonRunFromSetup(registry, kSeed, lower.fresh, lower.startCleared, lower.setup));
    CHECK(registry.ctx().get<game::FDungeonRun>().depth == 1);
    CHECK(registry.ctx().get<game::FDungeonCheckpoint>().cleared == 2);  // a lower start does not lower it

    // The next non-fresh draft still defaults to the checkpoint.
    game::FPreparationDraft highest = game::MakeDefaultDraft(registry, game::FRunStartRequest{false, -1});
    CHECK(highest.startCleared == 2);
    CHECK(game::StartDungeonRunFromSetup(registry, kSeed, highest.fresh, highest.startCleared, highest.setup));
    CHECK(registry.ctx().get<game::FDungeonRun>().depth == 3);

    // A start index above the checkpoint is clamped, never raised.
    CHECK(game::StartDungeonRunFromSetup(registry, kSeed, false, 9, highest.setup));
    CHECK(registry.ctx().get<game::FDungeonRun>().depth == 3);
    CHECK(registry.ctx().get<game::FDungeonCheckpoint>().cleared == 2);
}

void SupportPreviewMatchesApplied() {
    // The preview must equal what StartRunAt applies. The tested value is pinned by an override derived from
    // the SHIPPED rewards table (one content world): threshold 2 makes the forced-rune-offer half real.
    const FDataOverrides kOverrides = [](const std::filesystem::path& data) {
        json rewards = ReadJsonFile(std::filesystem::path("assets") / "data" / "rewards.json");
        rewards["rune_offer"]["pity_threshold"] = 2;
        WriteJsonFile(data / "rewards.json", rewards);
    };
    FPrepBoot boot("prep-support", kOverrides);
    entt::registry& registry = boot.registry;
    registry.ctx().get<game::FDungeonCheckpoint>().cleared = 3;
    const auto* sequence = game::FindRunSequence(registry);
    CHECK(sequence != nullptr);
    CHECK(sequence->startSupport.runeOffers > 0);
    const auto* rewards = registry.ctx().find<game::FRewards>();
    CHECK(rewards != nullptr);
    const int threshold = rewards->tables.pityThreshold;
    CHECK(threshold > 0);

    for (int index = 0; index <= 3; ++index) {
        const game::FStartSupportPreview preview = game::PreviewStartSupport(registry, index);
        CHECK(preview.materials == sequence->StartMaterials(index));
        if (index > 0) {
            CHECK(preview.forcedRuneOffer);  // rune_offers > 0 and pity > 0: deeper starts force the offer
        } else {
            CHECK(!preview.forcedRuneOffer);
        }

        const game::FPreparationDraft draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{false, index});
        CHECK(draft.startCleared == index);
        CHECK(game::StartDungeonRunFromSetup(registry, kSeed, draft.fresh, draft.startCleared, draft.setup));

        const auto& run = registry.ctx().get<game::FDungeonRun>();
        CHECK(run.cleared == index && run.depth == index + 1);
        CHECK(run.materials == preview.materials);  // the applied materials are the previewed ones
        const auto& applied = registry.ctx().get<game::FRewards>();
        if (preview.forcedRuneOffer) {
            CHECK(applied.pity == threshold);
        } else {
            CHECK(applied.pity == 0);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The hub view
// ---------------------------------------------------------------------------------------------

/// A headless ImGui fixture with the software renderer and the dungeon art, the run_modal pattern.
struct FPrepView {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, &SDL_DestroySurface};
    FRendererPtr renderer{nullptr};
    FScratchRoot scratch;
    entt::registry registry;
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{nullptr, &ImGui::DestroyContext};

    explicit FPrepView(const char* stem) : scratch(stem) {
        game::FAssetManager::Initialize(registry, {scratch.root.string(), kShipAssetRoot});
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, (scratch.root / "checkpoint.json").string()));
        surface.reset(SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888));
        CHECK(surface != nullptr);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        CHECK(renderer != nullptr);
        registry.ctx().emplace<game::FEngineWindowState>().width = 1280;
        registry.ctx().get<game::FEngineWindowState>().height = 800;
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        CHECK(game::InitializeDungeonArt(registry));
        context.reset(ImGui::CreateContext());
        CHECK(context != nullptr);
        auto& io = ImGui::GetIO();
        io.DisplaySize = {1280, 800};
        io.DeltaTime = 1.f / 60.f;
        io.Fonts->Build();
        game::InitializeDungeonUi(registry);
    }
    FPrepView(const FPrepView&) = delete;
    FPrepView& operator=(const FPrepView&) = delete;
    ~FPrepView() { context.reset(); }

    /// Queues a tab focus the way a click would, on the REAL tab bar of the preparation panel. A tab's id
    /// is hashed with the TAB BAR's id: BeginTabBarEx pushes the bar id onto the window id stack (like
    /// PushOverrideID) before every BeginTabItem hashes its label. The pointer overload of
    /// TabBarQueueFocus fills the queue without touching per-frame window stacks; the queue is consumed by
    /// the next BeginTabBar in the same frame it is set.
    void QueueTab(const char* name) {
        ImGuiWindow* window = ImGui::FindWindowByName("Preparation");
        CHECK(window != nullptr);
        ImGuiTabBar* bar = ImGui::TabBarFindByID(ImHashStr("Hub tabs", 0, window->ID));
        CHECK(bar != nullptr);
        ImGuiTabItem* tab = ImGui::TabBarFindTabByID(bar, ImHashStr(name, 0, bar->ID));
        CHECK(tab != nullptr);
        ImGui::TabBarQueueFocus(bar, tab);
    }

    void DrawFrame(game::FPreparationScreen& screen, const char* selectTab = nullptr) {
        ImGui::NewFrame();
        if (selectTab != nullptr) QueueTab(selectTab);
        game::DrawPreparationScreen(registry, screen);
        ImGui::Render();
    }
};

void TabsRenderWithoutMeta() {
    FPrepView view("prep-tabs");
    entt::registry& registry = view.registry;
    CHECK(game::FindMetaSave(registry) == nullptr);  // meta uninitialized for the first half

    game::FPreparationScreen screen;
    screen.draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});

    // Frame 1 settles the freshly created tab bar: its tabs are registered during that frame, so their
    // laid-out offsets/widths (and therefore the drawn labels) arrive with the next frame's layout — the
    // compendium view test logs on frame 2 for the same reason. Frame 2 is thus the captured frame, and the
    // log is opened and closed inside it (the implicit Debug##Default window owns the log scope).
    view.DrawFrame(screen);  // frame 1: the default draft's Expedition tab
    CHECK(screen.tab == game::EHubTab::Expedition);
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());  // no click, no command

    const std::string logPath = (view.scratch.root / "prep-ui.log").string();
    ImGui::NewFrame();
    ImGui::LogToFile(0, logPath.c_str());
    game::DrawPreparationScreen(registry, screen);
    ImGui::LogFinish();
    ImGui::Render();  // frame 2: the logged frame

    // Every tab body renders without meta (Expedition, Unlocks read-only list, Records placeholder).
    view.DrawFrame(screen, "Unlocks");
    CHECK(screen.tab == game::EHubTab::Unlocks);
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());
    view.DrawFrame(screen, "Records");
    CHECK(screen.tab == game::EHubTab::Records);
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());

    // With the meta store up the same three tabs render (the seal header and the records body now read it).
    CHECK(game::InitializeMetaProgress(registry, (view.scratch.root / "meta_save.json").string()));
    CHECK(game::FindMetaSave(registry) != nullptr);
    view.DrawFrame(screen, "Expedition");
    CHECK(screen.tab == game::EHubTab::Expedition);
    view.DrawFrame(screen, "Unlocks");
    CHECK(screen.tab == game::EHubTab::Unlocks);
    view.DrawFrame(screen, "Records");
    CHECK(screen.tab == game::EHubTab::Records);
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());

    const std::string log = ReadTextFile(logPath);
    // Diagnostic before the assertions (a failing CHECK throws the rest of the case away): when the text
    // log came up empty or partial, show what it actually holds.
    if (log.find("Expedition") == std::string::npos) {
        std::cout << "[report] prep-ui.log bytes=" << log.size() << " content=[" << log << "]\n";
    }
    CHECK(log.find("PREPARATION") != std::string::npos);
    CHECK(log.find("Expedition") != std::string::npos);
    CHECK(log.find("Unlocks") != std::string::npos);
    CHECK(log.find("Records") != std::string::npos);
    CHECK(log.find("Start expedition") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// WP-21a: the behavior-identical export pin
// ---------------------------------------------------------------------------------------------

void ResolveExportIsBehaviorIdentical() {
    FPrepBoot boot("preparation-resolve", {}, EContentWorld::Fixture);

    // The authored dungeon party is the resolver's source of truth: every row maps to its content row, and
    // the pointer is the very row `content.units.find` returns — one lookup path, no copy.
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const json& party = game::DungeonDefinition(boot.registry).at("party");
    CHECK(!party.empty());
    for (const json& row : party) {
        const std::string id = row.at("id").get<std::string>();
        const std::string unitId = row.at("unit").get<std::string>();
        const game::gameplay::FUnitContent* unit = game::ResolveRunMemberUnit(boot.registry, id);
        CHECK(unit != nullptr);
        CHECK(unit == content.units.find(unitId));
        CHECK(unit->id == unitId);
    }

    // A fresh run's members resolve to those same pointers (run.party is authored from the same rows), and
    // the existing max-HP call site goes through the export: a fresh member has no bonuses or nodes, so
    // its maximum is the unit template's base.
    game::StartDungeonRun(boot.registry, kSeed, true);
    const auto& run = boot.registry.ctx().get<game::FDungeonRun>();
    CHECK(run.party.size() == party.size());
    for (const game::FRunMember& member : run.party) {
        const json* row = nullptr;
        for (const json& candidate : party) {
            if (candidate.at("id").get<std::string>() == member.id) {
                row = &candidate;
                break;
            }
        }
        CHECK(row != nullptr);
        const game::gameplay::FUnitContent* unit = game::ResolveRunMemberUnit(boot.registry, member.id);
        CHECK(unit != nullptr);
        CHECK(unit == content.units.find(row->at("unit").get<std::string>()));
        CHECK(game::DungeonMemberMaxHealth(boot.registry, member) == static_cast<int>(unit->maxHealth));
    }

    // Unknown member id: nullptr, never a throw (callers keep their existing warning/fallback).
    CHECK(game::ResolveRunMemberUnit(boot.registry, "party.ghost") == nullptr);

    // A registry that never booted the content: nullptr — the resolver must not assume its context.
    entt::registry bare;
    CHECK(game::ResolveRunMemberUnit(bare, "party.warrior") == nullptr);
}

} // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);  // a crash must still show how far the suite got
    // CTest sets GAME01P_SAVE_DIR per feature test; a direct run does not. Fall back to out/test-saves so
    // a run can never touch %APPDATA%\Game01P\EmberDungeon.
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path =
            (std::filesystem::current_path() / "out" / "test-saves" / "preparation_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("preparation", {
        {"ResolveExportIsBehaviorIdentical", ResolveExportIsBehaviorIdentical},
        {"RosterSelection", RosterSelection},
        {"RosterSelectionReachesBattlefield", RosterSelectionReachesBattlefield},
        {"InstallKeepsAuthoredPartyWhenNoSetup", InstallKeepsAuthoredPartyWhenNoSetup},
        {"SubsetSaveRoundTrip", SubsetSaveRoundTrip},
        {"LockedStubNeedsNoUnit", LockedStubNeedsNoUnit},
        {"BattlefieldUsesResolvedUnit", BattlefieldUsesResolvedUnit},
        {"ResolverPrefersRosterUnit", ResolverPrefersRosterUnit},
        {"ResolverFallsBackToDungeonParty", ResolverFallsBackToDungeonParty},
        {"MaxHealthUsesRosterUnit", MaxHealthUsesRosterUnit},
        {"LockedRowWithUnitResolvesNull", LockedRowWithUnitResolvesNull},
        {"RosterUnitMissingFailsBoot", RosterUnitMissingFailsBoot},
        {"SkillPoolShorterThanSlotsFailsBoot", SkillPoolShorterThanSlotsFailsBoot},
        {"UnlockGatedEntryValidatedAtBoot", UnlockGatedEntryValidatedAtBoot},
        {"ShippedRosterContentValid", ShippedRosterContentValid},
        {"RosterUnlockGatesSelection", RosterUnlockGatesSelection},
        {"LockedStubNotOpenedByUnlock", LockedStubNotOpenedByUnlock},
        {"SkillPoolHidesLockedUnlock", SkillPoolHidesLockedUnlock},
        {"FourSlotsEnforced", FourSlotsEnforced},
        {"PrepareConsumesCarrierOnly", PrepareConsumesCarrierOnly},
        {"PrepareDoesNotSkipWithExistingRun", PrepareDoesNotSkipWithExistingRun},
        {"PrepareStartsExactlyOneRunFromSetup", PrepareStartsExactlyOneRunFromSetup},
        {"PrepareCancelReturnsToTitle", PrepareCancelReturnsToTitle},
        {"HubConfirmDoesNotStartBattle", HubConfirmDoesNotStartBattle},
        {"ConfirmWithSaveDoesNotOverwrite", ConfirmWithSaveDoesNotOverwrite},
        {"DebugBattleStartUsesAuthoredLoadout", DebugBattleStartUsesAuthoredLoadout},
        {"ContinueSkipsPreparation", ContinueSkipsPreparation},
        {"StartAtLowerDungeonWithinCheckpoint", StartAtLowerDungeonWithinCheckpoint},
        {"CheckpointNeverLowers", CheckpointNeverLowers},
        {"SupportPreviewMatchesApplied", SupportPreviewMatchesApplied},
        {"TabsRenderWithoutMeta", TabsRenderWithoutMeta},
    });
}
