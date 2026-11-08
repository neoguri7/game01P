// WP-32a passive tree v2: graph/allocation/rank/exclusive/cap mechanics, save replay and run sizing.
//
// Asset roots (WP §6 asset-root table):
//   * mechanism tests use the in-memory v2 document of MechanismDocument()/MechanismTree(); every
//     expectation derives from the named k* constants above it, never from shipped balance data;
//   * V1SchemaStillLoads reads the FROZEN fixture tests/fixtures/assets/data/passive-tree.json;
//   * content-lint tests read the SHIP root (passive-tree.json + dungeon.json) and assert structure or
//     data-derived bounds only — their EXP/point/cap lines are reports, never gates.
//
// Save isolation: build-test.ps1 sets GAME01P_SAVE_DIR for the whole run; a plain ctest or a direct run
// would otherwise reach the user's real save under %APPDATA%, so main() falls back to
// out/test-saves/node_tree_tests.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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
using game::FPassiveStatBonuses;
using game::FPassiveStatCaps;
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

// --- mechanism tree: a compact v2 replica of the ship topology, namespaced ids -------------------
// m.root -> m.left, m.right; m.left -> m.hub; m.right -> m.mid -> m.hub (diamond: hub has two in-edges)
// m.hub -> m.capA, m.capB (either/or), m.rep (repeatable), m.leaf. Every expectation below derives from
// these constants, so shipped balance changes can never break a mechanism test.
constexpr int kRepRanks = 3;
constexpr int kRepHealth = 2;
constexpr int kLeftAttack = 1;
constexpr int kRightAttack = 1;
constexpr int kMidHealth = 1;
constexpr int kHubDefense = 1;
constexpr int kCapAAttack = 2;
constexpr int kCapBAttack = 2;
constexpr int kLeafMagicAttack = 1;
constexpr int kAttackCap = 2;                  // below kLeftAttack + kCapAAttack, so a legal pick clamps
constexpr std::size_t kHubNeighbors = 6;       // 2 in-edges (left, mid) + 4 out-edges

json MechanismDocument() {
    return Doc2({Node("m.root", {"m.left", "m.right"}),
                 Node("m.left", {"m.hub"}, Bonuses(0, kLeftAttack)),
                 Node("m.right", {"m.mid"}, Bonuses(0, kRightAttack)),
                 Node("m.mid", {"m.hub"}, Bonuses(kMidHealth, 0)),
                 Node("m.hub", {"m.capA", "m.capB", "m.rep", "m.leaf"}, Bonuses(0, 0, 0, kHubDefense)),
                 WithGroup(Node("m.capA", json::array(), Bonuses(0, kCapAAttack)), "m.pair"),
                 WithGroup(Node("m.capB", json::array(), Bonuses(0, kCapBAttack)), "m.pair"),
                 WithRanks(Node("m.rep", json::array(), Bonuses(kRepHealth, 0)), kRepRanks),
                 Node("m.leaf", json::array(), Bonuses(0, 0, kLeafMagicAttack))},
                {{"max_total_attack", kAttackCap}});
}
FPassiveTree MechanismTree() {
    const auto loaded = FPassiveTree::Load(MechanismDocument());
    CHECK(loaded.has_value());
    return *loaded;
}

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

bool HasEffect(const FPassiveNode& node) {
    const auto& b = node.bonuses;
    return b.maxHealth != 0 || b.attack != 0 || b.magicAttack != 0 || b.defense != 0
        || b.magicDefense != 0 || b.speed != 0;
}
std::array<int, 6> StatValues(const FPassiveStatBonuses& bonuses) {
    return {bonuses.maxHealth, bonuses.attack, bonuses.magicAttack,
            bonuses.defense, bonuses.magicDefense, bonuses.speed};
}
std::array<std::optional<int>, 6> CapValues(const FPassiveStatCaps& caps) {
    return {caps.maxHealth, caps.attack, caps.magicAttack, caps.defense, caps.magicDefense, caps.speed};
}
constexpr std::array<std::string_view, 6> kStatNames = {"health", "attack", "magic_attack",
                                                        "defense", "magic_defense", "speed"};

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

// --- save fixtures ------------------------------------------------------------------------------
struct FSaveEnv {
    explicit FSaveEnv(FPassiveTree passiveTree) : tree(std::move(passiveTree)) {
        content = game::gameplay::FContentRegistry::load(assets);
        CHECK(content.has_value());
        rules.content = &*content;
        rules.passiveTree = &tree;
        rules.partyMatch = game::EPartyMatch::Subset;
        const auto dungeon = assets.digest("data/dungeon");
        CHECK(dungeon != nullptr);
        for (const auto& member : dungeon->json().at("party")) rules.partyIds.push_back(member.at("id").get<std::string>());
    }
    game::FAssetManager assets{ShipAssetRoots()};
    FPassiveTree tree;
    std::optional<game::gameplay::FContentRegistry> content;
    game::FDungeonSaveRules rules;
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

// --- mechanism schema --------------------------------------------------------------------------
void MechanismSchemaLoads() {
    const FPassiveTree tree = MechanismTree();
    CHECK(tree.caps.attack == kAttackCap);
    CHECK(!tree.caps.maxHealth && !tree.caps.magicAttack && !tree.caps.defense
          && !tree.caps.magicDefense && !tree.caps.speed);
    const FPassiveNode* rep = tree.Find("m.rep");
    const FPassiveNode* capA = tree.Find("m.capA");
    const FPassiveNode* capB = tree.Find("m.capB");
    const FPassiveNode* hub = tree.Find("m.hub");
    CHECK(rep != nullptr && rep->maxRanks == kRepRanks);
    CHECK(capA != nullptr && capA->exclusiveGroup == "m.pair");
    CHECK(capB != nullptr && capB->exclusiveGroup == "m.pair");
    CHECK(hub != nullptr && hub->neighbors.size() == kHubNeighbors);
}

// --- ship schema (content-lint: structure only, no balance literals) ----------------------------
void V2SchemaLoads() {
    const json document = ShipDocument();
    CHECK(document.at("schema_version").get<int>() == 2);
    const FPassiveTree tree = LoadShipTree();
    CHECK(tree.nodes.size() == 22u);
    CHECK(!tree.rootNodeId.empty());
    const FPassiveNode* root = tree.Find(tree.rootNodeId);
    CHECK(root != nullptr && !HasEffect(*root));
    int allocatable = 0;
    bool repeatable = false;
    std::map<std::string, int> groupSizes;
    std::map<std::string, int> inDegrees;
    for (const FPassiveNode& node : tree.nodes) {
        if (node.id != tree.rootNodeId) ++allocatable;
        if (node.maxRanks > 1) repeatable = true;
        if (!node.exclusiveGroup.empty()) ++groupSizes[node.exclusiveGroup];
        for (const std::string& next : node.next) ++inDegrees[next];
    }
    CHECK(allocatable == 21);
    CHECK(repeatable);
    CHECK(std::ranges::any_of(groupSizes, [](const auto& group) { return group.second >= 2; }));
    CHECK(std::ranges::any_of(inDegrees, [](const auto& entry) { return entry.second >= 2; }));
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
    const FPassiveTree tree = MechanismTree();
    CHECK(tree.CanAllocate({"m.left"}, "m.hub"));
    CHECK(tree.CanAllocate({"m.right", "m.mid"}, "m.hub"));
    CHECK(!tree.CanAllocate({}, "m.hub"));
    CHECK(!tree.CanAllocate({"m.right"}, "m.hub"));
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

// --- allocation (mechanism tree) ---------------------------------------------------------------
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
    const FPassiveTree tree = MechanismTree();
    CHECK(!tree.CanAllocate({}, "m.root"));
    CHECK(!tree.CanAllocate({"m.left"}, "m.root"));
}
void RepeatableUntilRankCap() {
    const FPassiveTree tree = MechanismTree();
    std::vector<std::string> allocated = {"m.left", "m.hub"};
    for (int rank = 0; rank < kRepRanks; ++rank) {
        CHECK(tree.CanAllocate(allocated, "m.rep"));
        allocated.push_back("m.rep");
    }
}
void RepeatableStopsAtMaxRanks() {
    const FPassiveTree tree = MechanismTree();
    std::vector<std::string> allocated = {"m.left", "m.hub"};
    for (int rank = 0; rank < kRepRanks; ++rank) allocated.push_back("m.rep");
    CHECK(!tree.CanAllocate(allocated, "m.rep"));
    CHECK(!tree.CanAllocate({"m.left"}, "m.left"));  // max_ranks 1, held once
}
void ExclusiveBlocksBothDirections() {
    const FPassiveTree tree = MechanismTree();
    const std::vector<std::string> withCapA = {"m.left", "m.hub", "m.capA"};
    CHECK(!tree.CanAllocate(withCapA, "m.capB"));
    const FPassiveNode* forward = game::PassiveExclusiveConflict(tree.nodes, withCapA, "m.capB");
    CHECK(forward != nullptr && forward->id == "m.capA");
    const std::vector<std::string> withCapB = {"m.left", "m.hub", "m.capB"};
    CHECK(!tree.CanAllocate(withCapB, "m.capA"));
    const FPassiveNode* backward = game::PassiveExclusiveConflict(tree.nodes, withCapB, "m.capA");
    CHECK(backward != nullptr && backward->id == "m.capB");
}
void ExclusiveAbsentInOldSaves() {
    const FPassiveTree tree = MechanismTree();
    const std::vector<std::string> old = {"m.left", "m.hub"};  // no member of the pair held
    CHECK(game::PassiveExclusiveConflict(tree.nodes, old, "m.capA") == nullptr);
    CHECK(game::PassiveExclusiveConflict(tree.nodes, old, "m.capB") == nullptr);
    const FPassiveTree fixture = LoadFixtureTree();
    CHECK(std::ranges::all_of(fixture.nodes, [](const FPassiveNode& node) { return node.exclusiveGroup.empty(); }));
}
void RanksCountTwice() {
    const FPassiveTree tree = MechanismTree();
    CHECK(tree.Bonuses({"m.left", "m.hub", "m.rep", "m.rep"}).maxHealth == 2 * kRepHealth);
}
void CapsClampTotals() {
    const FPassiveTree tree = MechanismTree();
    // A legal pickup whose raw attack sum exceeds kAttackCap; health has no cap and sums freely.
    const auto bonuses = tree.Bonuses({"m.left", "m.hub", "m.mid", "m.rep", "m.rep", "m.capA"});
    CHECK(bonuses.attack == kAttackCap);
    CHECK(bonuses.maxHealth == kMidHealth + 2 * kRepHealth);
}
void CapsAreDataDriven() {
    json document = MechanismDocument();
    const auto original = FPassiveTree::Load(document);
    CHECK(original.has_value());
    CHECK(original->Bonuses({"m.left", "m.hub", "m.capA"}).attack == kAttackCap);  // clamped
    document["caps"]["max_total_attack"] = 1;
    const auto lowered = FPassiveTree::Load(document);
    CHECK(lowered.has_value());
    CHECK(lowered->Bonuses({"m.left", "m.hub", "m.capA"}).attack == 1);
    document["caps"]["max_total_attack"] = kLeftAttack + kCapAAttack;
    const auto raised = FPassiveTree::Load(document);
    CHECK(raised.has_value());
    CHECK(raised->Bonuses({"m.left", "m.hub", "m.capA"}).attack == kLeftAttack + kCapAAttack);
}

// --- save replay --------------------------------------------------------------------------------
void OldSaveUniqueIdsLoad() {
    FSaveEnv env{LoadShipTree()};  // about old shipped ids, so it stays on the ship tree
    const auto loaded = game::DungeonRunFromJson(SaveDocument({"node.might", "node.swiftstep"}), env.rules);
    CHECK(loaded.has_value());
    CHECK((loaded->party[0].allocatedNodes == std::vector<std::string>{"node.might", "node.swiftstep"}));
}
void RepeatableSaveRoundTrip() {
    FSaveEnv env{MechanismTree()};
    std::vector<std::string> list = {"m.left", "m.hub"};
    for (int rank = 0; rank < kRepRanks; ++rank) list.push_back("m.rep");
    const auto loaded = game::DungeonRunFromJson(SaveDocument(list), env.rules);
    CHECK(loaded.has_value());
    CHECK((loaded->party[0].allocatedNodes == list));
    const auto reloaded = game::DungeonRunFromJson(game::DungeonRunToJson(*loaded), env.rules);
    CHECK(reloaded.has_value());
    CHECK((reloaded->party[0].allocatedNodes == list));
}
void IllegalOrderRejected() {
    FSaveEnv env{MechanismTree()};
    // First entry has no allocated (or root) neighbor, so its position is illegal.
    const auto loaded = game::DungeonRunFromJson(SaveDocument({"m.hub", "m.left"}), env.rules);
    CHECK(!loaded.has_value());
    CHECK(loaded.error().contains("allocated_nodes"));
}
void UnknownNodeRejected() {
    FSaveEnv env{MechanismTree()};
    const auto loaded = game::DungeonRunFromJson(SaveDocument({"m.ghost"}), env.rules);
    CHECK(!loaded.has_value());
    CHECK(loaded.error().contains("allocated_nodes"));
}
void SaveAcceptsChildBeforeParentWhenAdjacent() {
    FSaveEnv env{MechanismTree()};
    // hub (child of mid) precedes mid, and mid precedes right — undirected adjacency still allows it.
    const std::vector<std::string> list = {"m.left", "m.hub", "m.mid", "m.right"};
    const auto loaded = game::DungeonRunFromJson(SaveDocument(list), env.rules);
    CHECK(loaded.has_value());
    CHECK((loaded->party[0].allocatedNodes == list));
}
void SaveRejectsDuplicateSingleRankNode() {
    FSaveEnv env{MechanismTree()};
    const auto loaded = game::DungeonRunFromJson(SaveDocument({"m.left", "m.left"}), env.rules);
    CHECK(!loaded.has_value());
    CHECK(loaded.error().contains("allocated_nodes"));
}
void SaveRejectsRanksBeyondMax() {
    FSaveEnv env{MechanismTree()};
    std::vector<std::string> beyond = {"m.left", "m.hub"};
    for (int rank = 0; rank <= kRepRanks; ++rank) beyond.push_back("m.rep");  // one rank too many
    const auto rejected = game::DungeonRunFromJson(SaveDocument(beyond), env.rules);
    CHECK(!rejected.has_value());
    CHECK(rejected.error().contains("allocated_nodes"));
    std::vector<std::string> exact = {"m.left", "m.hub"};  // positive control: exactly max ranks loads
    for (int rank = 0; rank < kRepRanks; ++rank) exact.push_back("m.rep");
    CHECK(game::DungeonRunFromJson(SaveDocument(exact), env.rules).has_value());
}
void SaveRejectsBothExclusiveMembers() {
    FSaveEnv env{MechanismTree()};
    for (const auto& order : {std::vector<std::string>{"m.left", "m.hub", "m.capA", "m.capB"},
                              std::vector<std::string>{"m.left", "m.hub", "m.capB", "m.capA"}}) {
        const auto loaded = game::DungeonRunFromJson(SaveDocument(order), env.rules);
        CHECK(!loaded.has_value());
        CHECK(loaded.error().contains("allocated_nodes"));
    }
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
void ShipCapsAboveReachableMax() {
    const FPassiveTree tree = LoadShipTree();
    const auto caps = CapValues(tree.caps);
    for (std::size_t stat = 0; stat < caps.size(); ++stat) {
        if (!caps[stat]) continue;
        std::map<std::string, int> groupMax;  // one member per either/or group at most
        int reachable = 0;
        for (const FPassiveNode& node : tree.nodes) {
            if (node.id == tree.rootNodeId) continue;
            const int value = StatValues(node.bonuses)[stat] * node.maxRanks;
            if (node.exclusiveGroup.empty()) {
                reachable += value;
            } else {
                groupMax[node.exclusiveGroup] = std::max(groupMax[node.exclusiveGroup], value);
            }
        }
        for (const auto& entry : groupMax) reachable += entry.second;
        std::cout << "  [report] cap " << kStatNames[stat] << ": reachable max " << reachable
                  << " <= cap " << *caps[stat] << "\n";
        CHECK(*caps[stat] >= reachable);
    }
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
void RepeatableSoakThenExcluded() {
    const FPassiveTree tree = MechanismTree();
    std::vector<std::string> allocated = {"m.left", "m.hub"};
    for (int rank = 0; rank < kRepRanks; ++rank) {
        CHECK(tree.CanAllocate(allocated, "m.rep"));
        allocated.push_back("m.rep");
    }
    CHECK(!tree.CanAllocate(allocated, "m.rep"));
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
        {"MechanismSchemaLoads", MechanismSchemaLoads},
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
        {"SaveAcceptsChildBeforeParentWhenAdjacent", SaveAcceptsChildBeforeParentWhenAdjacent},
        {"SaveRejectsDuplicateSingleRankNode", SaveRejectsDuplicateSingleRankNode},
        {"SaveRejectsRanksBeyondMax", SaveRejectsRanksBeyondMax},
        {"SaveRejectsBothExclusiveMembers", SaveRejectsBothExclusiveMembers},
        {"RespecReturnsRepeats", RespecReturnsRepeats},
        {"EveryAllocatableNodeHasEffect", EveryAllocatableNodeHasEffect},
        {"ShipCapsAboveReachableMax", ShipCapsAboveReachableMax},
        {"CycleOneNotSaturated", CycleOneNotSaturated},
        {"LevelUpCandidatesAtLeastTwo", LevelUpCandidatesAtLeastTwo},
        {"NotablePathCandidateAfterLevelFive", NotablePathCandidateAfterLevelFive},
        {"RepeatableSoakThenExcluded", RepeatableSoakThenExcluded},
        {"PanelDrawSmoke", PanelDrawSmoke},
    });
}
