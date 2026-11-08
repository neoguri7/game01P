// WP-32h M2: the passive-tree panel's pure helpers (list state labels, hints, row visibility, hit
// testing, node-center geometry, empty-blocker fallback) plus one headless draw smoke with the
// blocker callback set and zero points.
//
// Asset roots: none — the draw test uses a small in-memory v2 document (root → a → b, an either/or
// pair and a repeatable node). Every geometry expectation derives from the header's kPassive*
// constants and the font size, never from pixel literals.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "gameplay/progression/FPassiveTree.h"
#include "presentation/FPassiveTreePanel.h"
#include "presentation/FUiTheme.h"

namespace {

using nlohmann::json;
using game::EAllocationBlocker;
using game::FPassiveNode;
using game::FPassiveTree;
using game::ui::FPanelPoint;

// --- in-memory v2 document (never shipped data) -------------------------------------------------
json Node(const std::string& id, json next = json::array(), int health = 1) {
    return {{"id", id}, {"display_name", id}, {"description", "Test node."}, {"x", 50}, {"y", 50},
            {"next", std::move(next)},
            {"bonuses", {{"max_health", health}, {"attack", 0}, {"magic_attack", 0},
                         {"defense", 0}, {"magic_defense", 0}, {"speed", 0}}}};
}
/// root → a → b → r(repeatable ×3); a → c1/c2 (either/or pair). The root is the only bonus-free node.
json TreeDocument() {
    json c1 = Node("c1");
    c1["exclusive_group"] = "pair";
    json c2 = Node("c2");
    c2["exclusive_group"] = "pair";
    json repeatable = Node("r");
    repeatable["max_ranks"] = 3;
    return {{"schema_version", 2},
            {"nodes", json::array({Node("root", {"a"}, 0), Node("a", {"b", "c1", "c2"}),
                                   Node("b", {"r"}), c1, c2, repeatable})}};
}
FPassiveTree LoadTestTree() {
    const auto loaded = FPassiveTree::Load(TreeDocument());
    CHECK(loaded.has_value());
    return *loaded;
}

// --- pure helpers -------------------------------------------------------------------------------
void ListStateTable() {
    // The label table, top-down: rank first, then the blocker; points play no role here.
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::None, 0, 1) == " (Connected)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::None, 0, 5) == " (Connected)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::NotAdjacent, 0, 1) == " (Locked)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::Exclusive, 0, 1) == " (Excluded)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::Root, 0, 1) == " (Locked)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::UnknownNode, 0, 1) == " (Locked)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::MaxRank, 1, 1) == " (Allocated)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::None, 1, 1) == " (Allocated)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::MaxRank, 2, 5) == " (Rank 2/5)");
    CHECK(game::ui::PassiveNodeListState(EAllocationBlocker::None, 3, 5) == " (Rank 3/5)");
}

void HintTable() {
    using game::ui::PassiveNodeHint;
    // Held single-rank node is MaxRank but gets no message; held repeatables below max are None.
    CHECK(PassiveNodeHint(EAllocationBlocker::MaxRank, 1, 5, 2, false, "") == "Maximum rank reached.");
    CHECK(PassiveNodeHint(EAllocationBlocker::MaxRank, 1, 1, 2, false, "").empty());
    CHECK(PassiveNodeHint(EAllocationBlocker::MaxRank, 3, 3, 2, false, "") == "Maximum rank reached.");
    CHECK(PassiveNodeHint(EAllocationBlocker::Exclusive, 0, 1, 2, false, "Warpath") == "Exclusive with Warpath.");
    CHECK(PassiveNodeHint(EAllocationBlocker::NotAdjacent, 0, 1, 2, false, "") == "Connect an adjacent node first.");
    CHECK(PassiveNodeHint(EAllocationBlocker::None, 0, 1, 0, false, "") == "Needs a node point.");
    CHECK(PassiveNodeHint(EAllocationBlocker::None, 0, 1, 2, false, "") == "Not available right now.");
    CHECK(PassiveNodeHint(EAllocationBlocker::None, 0, 1, 2, true, "").empty());
    CHECK(PassiveNodeHint(EAllocationBlocker::None, 1, 3, 0, false, "") == "Needs a node point.");
    CHECK(PassiveNodeHint(EAllocationBlocker::None, 1, 3, 2, false, "") == "Not available right now.");
    CHECK(PassiveNodeHint(EAllocationBlocker::None, 1, 3, 2, true, "").empty());
    CHECK(PassiveNodeHint(EAllocationBlocker::Root, 0, 1, 2, true, "").empty());
    CHECK(PassiveNodeHint(EAllocationBlocker::UnknownNode, 0, 1, 2, true, "").empty());
}

void RowVisibility() {
    using game::ui::PassiveListRowVisible;
    CHECK(PassiveListRowVisible(EAllocationBlocker::None, 0));           // connected
    CHECK(!PassiveListRowVisible(EAllocationBlocker::NotAdjacent, 0));   // locked
    CHECK(!PassiveListRowVisible(EAllocationBlocker::Exclusive, 0));     // excluded
    CHECK(!PassiveListRowVisible(EAllocationBlocker::Root, 0));
    CHECK(PassiveListRowVisible(EAllocationBlocker::None, 2));           // held repeatable
    CHECK(PassiveListRowVisible(EAllocationBlocker::MaxRank, 1));        // held at max rank
    CHECK(PassiveListRowVisible(EAllocationBlocker::NotAdjacent, 1));    // held beats blocked
}

void HitPassiveNodeGeometry() {
    using game::ui::HitPassiveNode;
    const float radius = 4.0f;
    const float epsilon = 0.1f;
    const std::vector<FPanelPoint> single = {{10.0f, 10.0f}};
    CHECK(HitPassiveNode(single, radius, {10.0f, 10.0f}) == std::size_t{0});  // centre → that node
    CHECK(HitPassiveNode(single, radius, {10.0f + radius - epsilon, 10.0f}) == std::size_t{0});
    CHECK(!HitPassiveNode(single, radius, {10.0f + radius + epsilon, 10.0f}).has_value());
    // Overlapping circles: the nearest centre wins; a distance tie keeps the lower index.
    const std::vector<FPanelPoint> pair = {{0.0f, 0.0f}, {6.0f, 0.0f}};
    CHECK(HitPassiveNode(pair, 5.0f, {4.0f, 0.0f}) == std::size_t{1});  // d=2 beats d=4
    CHECK(HitPassiveNode(pair, 5.0f, {3.0f, 0.0f}) == std::size_t{0});  // tie → document order
    CHECK(!HitPassiveNode({}, radius, {0.0f, 0.0f}).has_value());       // empty list
}

void NodeCenterCorners() {
    const float fontPx = 17.0f;
    const FPanelPoint origin{40.0f, 90.0f};
    const FPanelPoint size{540.0f, 404.0f};
    const float margin = game::ui::kPassiveMapMarginEm * fontPx;
    const auto near = [](float a, float b) { return std::fabs(a - b) < 0.05f; };
    FPassiveNode node;
    node.x = 0.0f;
    node.y = 0.0f;
    const FPanelPoint topLeft = game::ui::PassiveNodeCenter(node, origin, size, fontPx);
    CHECK(near(topLeft.x, origin.x + margin));
    CHECK(near(topLeft.y, origin.y + margin));
    node.x = game::ui::kPassiveCoordinateMax;
    node.y = game::ui::kPassiveCoordinateMax;
    const FPanelPoint bottomRight = game::ui::PassiveNodeCenter(node, origin, size, fontPx);
    CHECK(near(bottomRight.x, origin.x + size.x - margin));
    CHECK(near(bottomRight.y, origin.y + size.y - margin));
    node.x = 50.0f;
    node.y = 50.0f;
    const FPanelPoint middle = game::ui::PassiveNodeCenter(node, origin, size, fontPx);
    CHECK(near(middle.x, origin.x + margin + (size.x - margin * 2.0f) * 0.5f));
    CHECK(near(middle.y, origin.y + margin + (size.y - margin * 2.0f) * 0.5f));
}

void EmptyBlockerFallsBack() {
    game::ui::FPassiveTreePanel panel;
    panel.canAllocate = [](std::string_view) { return true; };
    CHECK(game::ui::PassivePanelBlocker(panel, "node.any") == EAllocationBlocker::None);
    panel.canAllocate = [](std::string_view) { return false; };
    CHECK(game::ui::PassivePanelBlocker(panel, "node.any") == EAllocationBlocker::NotAdjacent);
    panel.canAllocate = nullptr;  // empty → the same Locked fallback
    CHECK(game::ui::PassivePanelBlocker(panel, "node.any") == EAllocationBlocker::NotAdjacent);
    // A set blocker always wins over canAllocate.
    panel.blocker = [](std::string_view) { return EAllocationBlocker::Exclusive; };
    CHECK(game::ui::PassivePanelBlocker(panel, "node.any") == EAllocationBlocker::Exclusive);
    panel.blocker = [](std::string_view nodeId) {
        return nodeId == "node.hit" ? EAllocationBlocker::None : EAllocationBlocker::Root;
    };
    CHECK(game::ui::PassivePanelBlocker(panel, "node.hit") == EAllocationBlocker::None);
    CHECK(game::ui::PassivePanelBlocker(panel, "node.other") == EAllocationBlocker::Root);
}

// --- headless ImGui fixture (the hud_legibility_tests recipe) -----------------------------------
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

void PanelDrawsWithBlockerAndNoPoints() {
    const FPassiveTree tree = LoadTestTree();
    const std::vector<std::string> allocated;
    const int points = 0;
    FImGuiFixture imgui;
    ImGui::NewFrame();
    ImGui::Begin("tree panel test");
    game::ui::FPassiveTreePanel panel;
    panel.nodes = &tree.nodes;
    panel.rootNodeId = tree.rootNodeId;
    panel.allocated = &allocated;
    panel.points = points;
    panel.blocker = [&](std::string_view nodeId) { return tree.Blocker(allocated, nodeId); };
    panel.canAllocate = [&](std::string_view nodeId) {
        return points > 0 && tree.CanAllocate(allocated, nodeId);  // the caller's points gate
    };
    panel.allocate = [&](std::string_view) {};
    game::ui::DrawPassiveTreePanel(panel);
    ImGui::End();
    ImGui::Render();
}

} // namespace

int main() {
    // Save safety: no save I/O here, but a direct run must not reach the user's real save either.
    const char* const kSaveDirEnv = "GAME01P_SAVE_DIR";
#ifdef _WIN32
    char* existing = nullptr;
    std::size_t existingSize = 0;
    const bool unset = _dupenv_s(&existing, &existingSize, kSaveDirEnv) != 0 || existing == nullptr
        || *existing == '\0';
    std::free(existing);
    if (unset) _putenv_s(kSaveDirEnv, "out/test-saves/tree_panel_tests");
#else
    if (const char* existing = std::getenv(kSaveDirEnv); existing == nullptr || *existing == '\0') {
        setenv(kSaveDirEnv, "out/test-saves/tree_panel_tests", 1);
    }
#endif
    return game_test::RunTests("tree_panel", {
        {"ListStateTable", ListStateTable},
        {"HintTable", HintTable},
        {"RowVisibility", RowVisibility},
        {"HitPassiveNodeGeometry", HitPassiveNodeGeometry},
        {"NodeCenterCorners", NodeCenterCorners},
        {"EmptyBlockerFallsBack", EmptyBlockerFallsBack},
        {"PanelDrawsWithBlockerAndNoPoints", PanelDrawsWithBlockerAndNoPoints},
    });
}
