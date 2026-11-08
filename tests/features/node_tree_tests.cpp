// WP-32a passive tree v2: directed authoring graph + undirected allocation adjacency, repeatable rank
// miners, either/or capstones, data-driven caps, save replay and the run-sizing simulations.
// Asset roots (WP §6): mechanism tests use in-memory JSON documents (Doc1/Doc2 + Node/V1Node);
// V1SchemaStillLoads reads the frozen fixture tests/fixtures/assets/data/passive-tree.json; the ship
// content-lint tests read ShipAssetRoots (passive-tree.json + dungeon.json) and print EXP/points as
// reports, never gates. Save isolation: GAME01P_SAVE_DIR falls back to out/test-saves/node_tree_tests.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/progression/FPassiveTree.h"
#include "presentation/FPassiveTreePanel.h"
#include "presentation/FUiTheme.h"

namespace {

using nlohmann::json;
using game::FPassiveNode;
using game::FPassiveTree;

// --- in-memory document builders ----------------------------------------------------------------
json Bonuses(int hp = 0, int atk = 0, int matk = 0, int def = 0, int mdef = 0, int spd = 0) {
    return {{"max_health", hp}, {"attack", atk}, {"magic_attack", matk},
            {"defense", def}, {"magic_defense", mdef}, {"speed", spd}};
}
json Node(const std::string& id, json next = json::array(), json bonuses = Bonuses()) {
    return {{"id", id}, {"display_name", id}, {"description", "Test node."}, {"x", 50}, {"y", 50},
            {"next", std::move(next)}, {"bonuses", std::move(bonuses)}};
}
json V1Node(const std::string& id, const std::string& parent) {
    return {{"id", id}, {"display_name", id}, {"description", "Test node."}, {"x", 50}, {"y", 50},
            {"parent", parent.empty() ? json(nullptr) : json(parent)}, {"bonuses", Bonuses()}};
}
json Doc2(json nodes, json caps = json::object()) {
    json document = {{"schema_version", 2}, {"nodes", std::move(nodes)}};
    if (!caps.empty()) document["caps"] = std::move(caps);
    return document;
}
json Doc1(json nodes) { return {{"schema_version", 1}, {"nodes", std::move(nodes)}}; }
json MinimalV2() { return Doc2({Node("root", {"n.a"}), Node("n.a")}); }
json WithRanks(json node, int ranks) { node["max_ranks"] = ranks; return node; }
json WithGroup(json node, const std::string& group) { node["exclusive_group"] = group; return node; }

// --- ship data + sizing helpers (content-lint) --------------------------------------------------
json ShipDocument() {
    const game::FAssetManager assets{ShipAssetRoots()};
    const auto digest = assets.digest("data/passive-tree");
    CHECK(digest != nullptr);
    return digest->json();
}
FPassiveTree LoadShipTree() {
    const auto loaded = FPassiveTree::Load(ShipDocument());
    CHECK(loaded.has_value());
    return *loaded;
}
FPassiveTree LoadFixtureTree() {
    std::ifstream file(FixtureDataPath("passive-tree.json"));
    CHECK(file.good());
    json document;
    file >> document;
    const auto loaded = FPassiveTree::Load(document);
    CHECK(loaded.has_value());
    return *loaded;
}

/// EXP sources come from the shipped data (never hard-coded): balance.room_experience x battle rooms.
/// The curve mirrors FDungeonGrowth::ExperienceForNextLevel.
struct FShipSizing {
    int roomExperience{0};
    int levelBase{0};
    int levelStep{0};
    int battleRooms{0};
};
FShipSizing ReadShipSizing() {
    const game::FAssetManager assets{ShipAssetRoots()};
    const auto digest = assets.digest("data/dungeon");
    CHECK(digest != nullptr);
    const auto& document = digest->json();
    FShipSizing sizing;
    sizing.roomExperience = document.at("balance").at("room_experience").get<int>();
    sizing.levelBase = document.at("balance").at("level_experience").get<int>();
    sizing.levelStep = document.at("balance").at("level_experience_step").get<int>();
    for (const auto& theme : document.at("themes")) {
        for (const auto& dungeon : theme.at("dungeons")) {
            for (const auto& room : dungeon.at("rooms")) {
                if (room.at("type").get<std::string>() == "battle") ++sizing.battleRooms;
            }
        }
    }
    return sizing;
}
int PointsForExperience(const FShipSizing& sizing, int experience) {
    int points = 0;
    int spent = 0;
    for (int level = 1;; ++level) {
        const int threshold = sizing.levelBase + sizing.levelStep * (level - 1);
        if (spent + threshold > experience) break;
        spent += threshold;
        ++points;
    }
    return points;
}

bool HasEffect(const FPassiveNode& node) {
    const auto& b = node.bonuses;
    return b.maxHealth != 0 || b.attack != 0 || b.magicAttack != 0 || b.defense != 0
        || b.magicDefense != 0 || b.speed != 0;
}
/// Candidates = allocatable AND worth a point (non-zero bonus) — the simulation's spend definition.
std::vector<std::string> Candidates(const FPassiveTree& tree, const std::vector<std::string>& allocated) {
    std::vector<std::string> candidates;
    for (const FPassiveNode& node : tree.nodes) {
        if (tree.CanAllocate(allocated, node.id) && HasEffect(node)) candidates.push_back(node.id);
    }
    return candidates;
}
enum class EStrategy { First, Last };
struct FSimulation {
    std::vector<std::vector<std::string>> candidates;  // candidates BEFORE each spend
    std::vector<std::string> allocated;
};
FSimulation Simulate(const FPassiveTree& tree, EStrategy strategy, int spends) {
    FSimulation sim;
    for (int spend = 0; spend < spends; ++spend) {
        auto candidates = Candidates(tree, sim.allocated);
        if (candidates.empty()) break;
        sim.candidates.push_back(std::move(candidates));
        sim.allocated.push_back(strategy == EStrategy::First ? sim.candidates.back().front()
                                                             : sim.candidates.back().back());
    }
    return sim;
}
/// A notable slot/capstone itself, or a directed (`next`) path to an unallocated one.
bool PathsToNotable(const FPassiveTree& tree, const std::vector<std::string>& allocated, const std::string& start) {
    static const std::set<std::string> targets = {"node.conduction", "node.conflagration", "node.permafrost",
                                                  "node.deadeye", "node.warpath", "node.wardpath"};
    std::set<std::string> visited;
    std::vector<std::string> frontier{start};
    while (!frontier.empty()) {
        const std::string current = frontier.back();
        frontier.pop_back();
        if (!visited.insert(current).second) continue;
        if (targets.contains(current) && std::ranges::find(allocated, current) == allocated.end()) return true;
        if (const FPassiveNode* node = tree.Find(current); node != nullptr) {
            for (const std::string& next : node->next) frontier.push_back(next);
        }
    }
    return false;
}

// --- save fixture ------------------------------------------------------------------------------
struct FSaveEnv {
    game::FAssetManager assets{ShipAssetRoots()};
    FPassiveTree tree;
    std::optional<game::gameplay::FContentRegistry> content;
    game::FDungeonSaveRules rules;
    FSaveEnv() {
        const auto treeAsset = assets.digest("data/passive-tree");
        CHECK(treeAsset != nullptr);
        const auto loaded = FPassiveTree::Load(treeAsset->json());
        CHECK(loaded.has_value());
        tree = *loaded;
        content = game::gameplay::FContentRegistry::load(assets);
        CHECK(content.has_value());
        rules.content = &*content;
        rules.passiveTree = &tree;
        rules.partyMatch = game::EPartyMatch::Subset;
        const auto dungeon = assets.digest("data/dungeon");
        CHECK(dungeon != nullptr);
        for (const auto& member : dungeon->json().at("party")) rules.partyIds.push_back(member.at("id").get<std::string>());
    }
};
json SaveDocument(const std::vector<std::string>& allocated) {
    json member = {{"id", "party.warrior"}, {"display_name", "Alden / Warrior"}, {"health", -1},
                   {"bonus_health", 0}, {"bonus_attack", 0}, {"bonus_magic", 0}, {"bonus_defense", 0},
                   {"bonus_magic_defense", 0}, {"bonus_speed", 0}, {"experience", 0}, {"level", 1},
                   {"passive_points", 0}, {"allocated_nodes", allocated}, {"skills", json::array()}};
    json run = {{"seed", 1}, {"depth", 1}, {"room", 0}, {"cleared", 0}, {"materials", 0},
                {"phase", "battle"}, {"room_name", ""}, {"message", ""},
                {"runes", json::array()}, {"party", json::array({member})}};
    return {{"schema_version", 2}, {"run", run}};
}
std::string SavePath(const char* stem) {
    const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
    const std::filesystem::path root = (overrideDir != nullptr && *overrideDir != '\0')
        ? std::filesystem::path(overrideDir) : std::filesystem::path("out/test-saves/node_tree_tests");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    static std::uint32_t serial = 0;
    return (root / (std::string(stem) + "-" + std::to_string(++serial) + "-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json")).string();
}

// --- headless ImGui fixture (the hud_legibility_tests recipe) ----------------------------------
class FImGuiFixture {
public:
    FImGuiFixture() {
        context = ImGui::CreateContext();
        CHECK(context != nullptr);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = {1280.f, 800.f};
        io.DeltaTime = 1.f / 60.f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        game::ui::ApplyTheme();
    }
    ~FImGuiFixture() { ImGui::DestroyContext(context); }
    FImGuiFixture(const FImGuiFixture&) = delete;
    FImGuiFixture& operator=(const FImGuiFixture&) = delete;

private:
    ImGuiContext* context{nullptr};
};

// --- schema ------------------------------------------------------------------------------------
void V2SchemaLoads() {
    const FPassiveTree tree = LoadShipTree();
    CHECK(tree.nodes.size() == 22u);
    CHECK(tree.rootNodeId == "node.origin");
    CHECK(tree.caps.maxHealth == 30 && tree.caps.attack == 7 && tree.caps.magicAttack == 6);
    CHECK(tree.caps.defense == 6 && tree.caps.magicDefense == 3 && tree.caps.speed == 4);
    const FPassiveNode* tempered = tree.Find("node.tempered");
    const FPassiveNode* warpath = tree.Find("node.warpath");
    const FPassiveNode* wardpath = tree.Find("node.wardpath");
    const FPassiveNode* zenith = tree.Find("node.zenith");
    CHECK(tempered != nullptr && tempered->maxRanks == 5);
    CHECK(warpath != nullptr && warpath->exclusiveGroup == "capstone.doctrine");
    CHECK(wardpath != nullptr && wardpath->exclusiveGroup == "capstone.doctrine");
    CHECK(zenith != nullptr && zenith->neighbors.size() == 8u);  // 4 out-edges + 4 in-edges
}
void V1SchemaStillLoads() {
    const FPassiveTree tree = LoadFixtureTree();
    CHECK(tree.nodes.size() == 9u);
    const FPassiveNode* swiftstep = tree.Find("node.swiftstep");
    const FPassiveNode* might = tree.Find("node.might");
    CHECK(swiftstep != nullptr && swiftstep->parentId == "node.might");
    CHECK(might != nullptr && std::ranges::find(might->next, "node.swiftstep") != might->next.end());
    CHECK(!tree.caps.maxHealth && !tree.caps.attack && !tree.caps.magicAttack && !tree.caps.defense
          && !tree.caps.magicDefense && !tree.caps.speed);
    CHECK(std::ranges::all_of(tree.nodes, [](const FPassiveNode& node) { return node.maxRanks == 1; }));
    const json base = Doc1({V1Node("root", ""), V1Node("n.a", "root")});
    json ranks = base;
    ranks["nodes"][1]["max_ranks"] = 2;  // v2-only key: refused, never silently ignored
    CHECK(!FPassiveTree::Load(ranks).has_value());
    json caps = base;
    caps["caps"] = {{"max_total_attack", 1}};
    CHECK(!FPassiveTree::Load(caps).has_value());
}

// --- graph -------------------------------------------------------------------------------------
void DirectedGraphAcyclic() {
    CHECK(!FPassiveTree::Load(Doc2({Node("root", {"n.a"}), Node("n.a", {"n.b"}),
                                    Node("n.b", {"n.c"}), Node("n.c", {"n.a"})})).has_value());  // cycle
    CHECK(!FPassiveTree::Load(Doc2({Node("root", {"n.a"}), Node("n.a", {"n.a"})})).has_value());  // self
    CHECK(!FPassiveTree::Load(Doc2({Node("root", {"n.a", "n.a"}), Node("n.a")})).has_value());    // duplicate
    json duplicateParent = Doc2({Node("root", {"n.a"}), Node("n.a")});
    duplicateParent["nodes"][1]["parent"] = "root";  // next + parent declare the same edge
    CHECK(!FPassiveTree::Load(duplicateParent).has_value());
}
void UndirectedReachabilityRequired() {
    CHECK(!FPassiveTree::Load(Doc2({Node("root", {"n.a"}), Node("n.a"), Node("n.isolated")})).has_value());
    CHECK(FPassiveTree::Load(Doc2({Node("root", {"n.a"}), Node("n.a")})).has_value());
}
void DiamondAllowsEitherPath() {
    const FPassiveTree tree = LoadShipTree();
    CHECK(tree.CanAllocate({"node.might", "node.swiftstep"}, "node.zenith"));
    CHECK(tree.CanAllocate({"node.vitality", "node.precision", "node.conflagration"}, "node.zenith"));
    CHECK(!tree.CanAllocate({"node.vitality", "node.precision"}, "node.zenith"));
}
void ExclusiveGroupNeedsTwoMembers() {
    CHECK(!FPassiveTree::Load(Doc2({Node("root", {"n.a"}), WithGroup(Node("n.a"), "solo")})).has_value());
    CHECK(FPassiveTree::Load(Doc2({Node("root", {"n.a", "n.b"}), WithGroup(Node("n.a"), "pair"),
                                   WithGroup(Node("n.b"), "pair")})).has_value());
    CHECK(!FPassiveTree::Load(Doc2({WithGroup(Node("root", {"n.a"}), "pair"),
                                    WithGroup(Node("n.a"), "pair")})).has_value());  // root cannot join
}

// --- validation --------------------------------------------------------------------------------
void MaxRanksValidated() {
    const auto withRanks = [](json ranks) {
        json document = MinimalV2();
        document["nodes"][1]["max_ranks"] = std::move(ranks);
        return document;
    };
    CHECK(!FPassiveTree::Load(withRanks(0)).has_value());
    CHECK(!FPassiveTree::Load(withRanks(10)).has_value());
    CHECK(!FPassiveTree::Load(withRanks(2.5)).has_value());
    json rootRanks = MinimalV2();
    rootRanks["nodes"][0]["max_ranks"] = 2;
    CHECK(!FPassiveTree::Load(rootRanks).has_value());
    CHECK(FPassiveTree::Load(withRanks(9)).has_value());
}
void CapsValidated() {
    const auto withCaps = [](json caps) {
        json document = MinimalV2();
        document["caps"] = std::move(caps);
        return document;
    };
    CHECK(!FPassiveTree::Load(withCaps({{"max_total_luck", 1}})).has_value());
    CHECK(!FPassiveTree::Load(withCaps({{"max_total_attack", -1}})).has_value());
    CHECK(!FPassiveTree::Load(withCaps({{"max_total_attack", 1.5}})).has_value());
    const auto loaded = FPassiveTree::Load(withCaps({{"max_total_attack", 3}}));
    CHECK(loaded.has_value());
    CHECK(loaded->caps.attack == 3);
    CHECK(!loaded->caps.maxHealth && !loaded->caps.magicAttack && !loaded->caps.defense
          && !loaded->caps.magicDefense && !loaded->caps.speed);
}

// --- allocation --------------------------------------------------------------------------------
void AdjacentOnlyAcrossGraph() {
    const auto loaded = FPassiveTree::Load(Doc2({Node("n.root", {"n.a", "n.c"}), Node("n.a", {"n.b"}),
                                                 Node("n.c", {"n.b"}), Node("n.b", {"n.d"}), Node("n.d")}));
    CHECK(loaded.has_value());
    // B's undirected neighbors include A and C: allocating C+B reaches A through B (A is B's parent).
    CHECK(loaded->CanAllocate({"n.c", "n.b"}, "n.a"));
    CHECK(!loaded->CanAllocate({"n.c"}, "n.d"));  // D has no allocated neighbor
    CHECK(loaded->CanAllocate({"n.c", "n.b"}, "n.d"));
}
void RootNotAllocatable() {
    const FPassiveTree tree = LoadShipTree();
    CHECK(!tree.CanAllocate({}, "node.origin"));
    CHECK(!tree.CanAllocate({"node.might"}, "node.origin"));
}
void RepeatableUntilRankCap() {
    const FPassiveTree tree = LoadShipTree();
    std::vector<std::string> allocated = {"node.might", "node.swiftstep", "node.zenith"};
    for (int rank = 0; rank < 5; ++rank) {
        CHECK(tree.CanAllocate(allocated, "node.tempered"));
        allocated.push_back("node.tempered");
    }
}
void RepeatableStopsAtMaxRanks() {
    const FPassiveTree tree = LoadShipTree();
    std::vector<std::string> allocated = {"node.might", "node.swiftstep", "node.zenith"};
    for (int rank = 0; rank < 5; ++rank) allocated.push_back("node.tempered");
    CHECK(!tree.CanAllocate(allocated, "node.tempered"));
    CHECK(!tree.CanAllocate({"node.might"}, "node.might"));  // max_ranks 1, held once
}
void ExclusiveBlocksBothDirections() {
    const FPassiveTree tree = LoadShipTree();
    const std::vector<std::string> withWarpath = {"node.might", "node.swiftstep", "node.zenith", "node.warpath"};
    CHECK(!tree.CanAllocate(withWarpath, "node.wardpath"));
    const FPassiveNode* forward = game::PassiveExclusiveConflict(tree.nodes, withWarpath, "node.wardpath");
    CHECK(forward != nullptr && forward->id == "node.warpath");
    const std::vector<std::string> withWardpath = {"node.vitality", "node.precision", "node.conflagration",
                                                   "node.zenith", "node.wardpath"};
    CHECK(!tree.CanAllocate(withWardpath, "node.warpath"));
    const FPassiveNode* backward = game::PassiveExclusiveConflict(tree.nodes, withWardpath, "node.warpath");
    CHECK(backward != nullptr && backward->id == "node.wardpath");
}
void ExclusiveAbsentInOldSaves() {
    const FPassiveTree tree = LoadShipTree();
    const std::vector<std::string> old = {"node.might", "node.swiftstep"};
    CHECK(game::PassiveExclusiveConflict(tree.nodes, old, "node.warpath") == nullptr);
    CHECK(game::PassiveExclusiveConflict(tree.nodes, old, "node.wardpath") == nullptr);
    const FPassiveTree fixture = LoadFixtureTree();
    CHECK(std::ranges::all_of(fixture.nodes, [](const FPassiveNode& node) { return node.exclusiveGroup.empty(); }));
}
void RanksCountTwice() {
    const FPassiveTree tree = LoadShipTree();
    CHECK(tree.Bonuses({"node.might", "node.swiftstep", "node.zenith", "node.tempered", "node.tempered"}).maxHealth == 4);
}
void CapsClampTotals() {
    const auto loaded = FPassiveTree::Load(Doc2({Node("root", {"n.a", "n.b", "n.c", "n.hp"}),
                                                 Node("n.a", json::array(), Bonuses(0, 1)),
                                                 Node("n.b", json::array(), Bonuses(0, 1)),
                                                 Node("n.c", json::array(), Bonuses(0, 1)),
                                                 Node("n.hp", json::array(), Bonuses(3, 0))},
                                                {{"max_total_attack", 2}}));
    CHECK(loaded.has_value());
    const auto bonuses = loaded->Bonuses({"n.a", "n.b", "n.c", "n.hp"});
    CHECK(bonuses.attack == 2);     // clamped
    CHECK(bonuses.maxHealth == 3);  // no cap on health
}
void CapsAreDataDriven() {
    json document = ShipDocument();
    const auto original = FPassiveTree::Load(document);
    CHECK(original.has_value());
    CHECK(original->Bonuses({"node.might", "node.precision"}).attack == 2);
    document["caps"]["max_total_attack"] = 1;
    const auto lowered = FPassiveTree::Load(document);
    CHECK(lowered.has_value());
    CHECK(lowered->Bonuses({"node.might", "node.precision"}).attack == 1);
}

// --- save --------------------------------------------------------------------------------------
void OldSaveUniqueIdsLoad() {
    FSaveEnv env;
    const auto loaded = game::DungeonRunFromJson(SaveDocument({"node.might", "node.swiftstep"}), env.rules);
    CHECK(loaded.has_value());
    CHECK((loaded->party[0].allocatedNodes == std::vector<std::string>{"node.might", "node.swiftstep"}));
}
void RepeatableSaveRoundTrip() {
    FSaveEnv env;
    const std::vector<std::string> list = {"node.might", "node.swiftstep", "node.zenith",
                                           "node.tempered", "node.tempered", "node.tempered"};
    const auto loaded = game::DungeonRunFromJson(SaveDocument(list), env.rules);
    CHECK(loaded.has_value());
    CHECK(loaded->party[0].allocatedNodes == list);
    const auto reloaded = game::DungeonRunFromJson(game::DungeonRunToJson(*loaded), env.rules);
    CHECK(reloaded.has_value());
    CHECK(reloaded->party[0].allocatedNodes == list);
}
void IllegalOrderRejected() {
    FSaveEnv env;
    const auto loaded = game::DungeonRunFromJson(SaveDocument({"node.swiftstep", "node.might"}), env.rules);
    CHECK(!loaded.has_value());
    CHECK(loaded.error().contains("allocated_nodes"));
}
void UnknownNodeRejected() {
    FSaveEnv env;
    const auto loaded = game::DungeonRunFromJson(SaveDocument({"node.ghost"}), env.rules);
    CHECK(!loaded.has_value());
    CHECK(loaded.error().contains("allocated_nodes"));
}
void RespecReturnsRepeats() {
    game_test::FTestRegistry loaded = game_test::LoadTestRegistry(ShipAssetRoots(), SavePath("respec"));
    CHECK(loaded.content && loaded.dungeon);
    game::StartDungeonRun(loaded.registry, std::uint32_t{1}, true);
    auto& run = loaded.registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 100;
    auto& member = run.party[0];
    member.allocatedNodes = {"node.might", "node.swiftstep", "node.zenith",
                             "node.tempered", "node.tempered", "node.tempered"};
    member.passivePoints = 0;
    CHECK(!game::RespecDungeonNodes(loaded.registry).has_value());
    CHECK(member.allocatedNodes.empty());
    CHECK(member.passivePoints == 6);  // every entry refunded, repeats included
}

// --- sizing simulations (content-lint; EXP/points are reports, never gates) ---------------------
void EveryAllocatableNodeHasEffect() {
    const FPassiveTree tree = LoadShipTree();
    int allocatable = 0;
    for (const FPassiveNode& node : tree.nodes) {
        if (node.id == tree.rootNodeId) continue;
        ++allocatable;
        CHECK(HasEffect(node));
    }
    CHECK(allocatable == 21);
}
void CycleOneNotSaturated() {
    const FShipSizing sizing = ReadShipSizing();
    const int experience = sizing.roomExperience * sizing.battleRooms;
    const int points = PointsForExperience(sizing, experience);
    std::cout << "  [report] cycle 1: " << sizing.battleRooms << " battle rooms x " << sizing.roomExperience
              << " EXP = " << experience << " EXP -> " << points << " points\n";
    CHECK(points >= 1);
    const FPassiveTree tree = LoadShipTree();
    for (const EStrategy strategy : {EStrategy::First, EStrategy::Last}) {
        const FSimulation sim = Simulate(tree, strategy, points);
        CHECK(sim.candidates.size() == static_cast<std::size_t>(points));
        CHECK(std::ranges::all_of(sim.candidates, [](const std::vector<std::string>& list) { return !list.empty(); }));
        CHECK(!Candidates(tree, sim.allocated).empty());
    }
}
void LevelUpCandidatesAtLeastTwo() {
    const FPassiveTree tree = LoadShipTree();
    for (const EStrategy strategy : {EStrategy::First, EStrategy::Last}) {
        const FSimulation sim = Simulate(tree, strategy, 12);  // levels 2-13
        CHECK(sim.candidates.size() == 12u);
        CHECK(std::ranges::all_of(sim.candidates, [](const std::vector<std::string>& list) { return list.size() >= 2; }));
    }
}
void NotablePathCandidateAfterLevelFive() {
    const FPassiveTree tree = LoadShipTree();
    for (const EStrategy strategy : {EStrategy::First, EStrategy::Last}) {
        const FSimulation sim = Simulate(tree, strategy, 12);
        CHECK(sim.candidates.size() == 12u);
        // spend index 3 = the point earned at level 5 (level 2 = index 0)
        for (std::size_t spend = 3; spend < sim.candidates.size(); ++spend) {
            const std::vector<std::string> prefix(sim.allocated.begin(),
                                                  sim.allocated.begin() + static_cast<std::ptrdiff_t>(spend));
            CHECK(std::ranges::any_of(sim.candidates[spend], [&](const std::string& id) {
                return PathsToNotable(tree, prefix, id);
            }));
        }
    }
}
void TemperedSoakThenExcluded() {
    const FPassiveTree tree = LoadShipTree();
    std::vector<std::string> allocated = {"node.might", "node.swiftstep", "node.zenith"};
    for (int rank = 0; rank < 5; ++rank) {
        CHECK(tree.CanAllocate(allocated, "node.tempered"));
        allocated.push_back("node.tempered");
    }
    CHECK(!tree.CanAllocate(allocated, "node.tempered"));
    CHECK(Candidates(tree, allocated).size() >= 2);
}

// --- panel -------------------------------------------------------------------------------------
void PanelDrawSmoke() {
    const FPassiveTree tree = LoadShipTree();
    FImGuiFixture imgui;
    std::vector<std::string> allocated = {"node.might", "node.swiftstep", "node.zenith",
                                          "node.tempered", "node.tempered", "node.warpath"};
    ImGui::NewFrame();
    ImGui::Begin("passive tree smoke");
    game::ui::FPassiveTreePanel panel;
    panel.nodes = &tree.nodes;
    panel.rootNodeId = tree.rootNodeId;
    panel.allocated = &allocated;
    panel.points = 1;
    panel.canAllocate = [&tree, &allocated](std::string_view nodeId) { return tree.CanAllocate(allocated, nodeId); };
    game::ui::DrawPassiveTreePanel(panel);
    ImGui::End();
    ImGui::Render();
}

}  // namespace

int main() {
    // Save safety: build-test.ps1 sets GAME01P_SAVE_DIR for the whole run; a plain ctest or a direct run
    // would otherwise reach the user's real save under %APPDATA%, so fall back to
    // out/test-saves/node_tree_tests.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", "out/test-saves/node_tree_tests");
#else
        setenv("GAME01P_SAVE_DIR", "out/test-saves/node_tree_tests", 1);
#endif
    }
    return game_test::RunTests("node_tree", {
        {"V2SchemaLoads", V2SchemaLoads},
        {"V1SchemaStillLoads", V1SchemaStillLoads},
        {"DirectedGraphAcyclic", DirectedGraphAcyclic},
        {"UndirectedReachabilityRequired", UndirectedReachabilityRequired},
        {"DiamondAllowsEitherPath", DiamondAllowsEitherPath},
        {"ExclusiveGroupNeedsTwoMembers", ExclusiveGroupNeedsTwoMembers},
        {"MaxRanksValidated", MaxRanksValidated},
        {"CapsValidated", CapsValidated},
        {"AdjacentOnlyAcrossGraph", AdjacentOnlyAcrossGraph},
        {"RootNotAllocatable", RootNotAllocatable},
        {"RepeatableUntilRankCap", RepeatableUntilRankCap},
        {"RepeatableStopsAtMaxRanks", RepeatableStopsAtMaxRanks},
        {"ExclusiveBlocksBothDirections", ExclusiveBlocksBothDirections},
        {"ExclusiveAbsentInOldSaves", ExclusiveAbsentInOldSaves},
        {"RanksCountTwice", RanksCountTwice},
        {"CapsClampTotals", CapsClampTotals},
        {"CapsAreDataDriven", CapsAreDataDriven},
        {"OldSaveUniqueIdsLoad", OldSaveUniqueIdsLoad},
        {"RepeatableSaveRoundTrip", RepeatableSaveRoundTrip},
        {"IllegalOrderRejected", IllegalOrderRejected},
        {"UnknownNodeRejected", UnknownNodeRejected},
        {"RespecReturnsRepeats", RespecReturnsRepeats},
        {"EveryAllocatableNodeHasEffect", EveryAllocatableNodeHasEffect},
        {"CycleOneNotSaturated", CycleOneNotSaturated},
        {"LevelUpCandidatesAtLeastTwo", LevelUpCandidatesAtLeastTwo},
        {"NotablePathCandidateAfterLevelFive", NotablePathCandidateAfterLevelFive},
        {"TemperedSoakThenExcluded", TemperedSoakThenExcluded},
        {"PanelDrawSmoke", PanelDrawSmoke},
    });
}
