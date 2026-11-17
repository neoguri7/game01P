// WP-12c HUD legibility tests. Everything asserted runs the code the game runs: the Free/Zone text rule,
// the single size table and the heading-name measurement live in FUiTheme and are used by the HUD view; the
// board-plate caret/outline/lift helpers, the status-gem shapes and the selection-outline insets are the
// same functions the view and the grid renderer call. The click test and the intent-handoff test drive the
// real EmberCombatHudSystem against a real battle (the intent_tests boot recipe, as in combat_info_ui_tests).
#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/EngineState.h"
#include "core/InputState.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/EmberCombatHudSystem.h"
#include "ecs/systems/EmberCombatInfo.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FStatusContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "presentation/FIsoView.h"
#include "presentation/FSettings.h"
#include "presentation/FUiFrame.h"
#include "presentation/FUiTheme.h"
#include "states/FEmberBattleState.h"

namespace {

using game::ecs::FCombatInfoSnapshot;
using game::ecs::FIntentBadge;
using game::ui::EHudText;
using game::ui::FRect;
using game::ui::FUiLayout;
using game::ui::HudTextPx;

/// Per-case wall cost, printed when the case's scope exits (also on a failed CHECK, via unwinding): the
/// suite must stay far under ctest's per-test timeout, and the lead reads these lines to see the cost.
struct FCaseTimer {
    explicit FCaseTimer(const char* label) : name(label) {}
    ~FCaseTimer() {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        char line[128]{};
        std::snprintf(line, sizeof line, "  [time] %s %.2f s\n", name, seconds);
        std::cout << line;
    }
    const char* name;
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
};

// ---------------------------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------------------------

std::filesystem::path SaveDir() {
    if (const char* value = std::getenv("GAME01P_SAVE_DIR"); value != nullptr && *value != '\0') {
        return std::filesystem::path(value);
    }
    return std::filesystem::path("out");
}

/// A headless ImGui context with the real theme applied. Applying the theme (theme loader with the
/// dynamic-atlas renderer flag) also RESETS FUiTheme's font registry to this new context: a fixture
/// that skipped it after a previous one was destroyed would read a stale heading font through the
/// freed context's address (g_fonts aliasing across contexts — the round-2 crash in GetFontBaked).
class FImGuiFixture {
public:
    FImGuiFixture() {
        context = ImGui::CreateContext();
        CHECK(context != nullptr);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = {1280.f, 800.f};
        io.DeltaTime = 1.f / 60.f;
        io.IniFilename = nullptr;
        // ImGui 1.92 dynamic atlases: without this renderer flag NewFrame asserts the atlas is built (the
        // legacy backend contract). Declaring it is also what lets ApplyTheme read the bundled files
        // without a rendering backend, exactly like the game's themed boot.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        game::ui::ApplyTheme();
    }
    ~FImGuiFixture() { ImGui::DestroyContext(context); }
    FImGuiFixture(const FImGuiFixture&) = delete;
    FImGuiFixture& operator=(const FImGuiFixture&) = delete;

private:
    ImGuiContext* context{nullptr};
};

/// A real Ember battle with the whole turn pipeline (same recipe as combat_info_ui_tests). The HUD's
/// per-frame context (window, input, layout) is ready so `hud.update` runs out of the box.
struct FBattleFixture {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    FEmberBattleState battle;
    std::filesystem::path checkpoint;
    std::vector<entt::entity> everyone;

    explicit FBattleFixture(std::uint32_t seed = 7, int depth = 1, int room = 0) {
        std::filesystem::create_directories(SaveDir());
        static int counter = 0;
        checkpoint = SaveDir() / ("hud-legibility-checkpoint-" + std::to_string(counter++) + ".json");
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        game::InitializeEventBus(registry);
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        turns.onRegister(registry);
        game::StartDungeonRun(registry, seed, true);
        auto& run = registry.ctx().get<game::FDungeonRun>();
        run.depth = depth;
        run.room = room;
        battle.OnEnter(registry);
        for (const auto entity : registry.view<game::FHealth>()) {
            everyone.push_back(entity);
        }
        auto* window = registry.ctx().find<game::FEngineWindowState>();
        if (window == nullptr) window = &registry.ctx().emplace<game::FEngineWindowState>();
        window->width = 1280;
        window->height = 800;
        game::FInputState::Initialize(registry);
        CHECK(game::ui::InitializeUiFrame(registry));
    }

    ~FBattleFixture() {
        battle.OnExit(registry);
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(checkpoint.string() + ".tmp", error);
    }

    FBattleFixture(const FBattleFixture&) = delete;
    FBattleFixture& operator=(const FBattleFixture&) = delete;

    game::gameplay::FContentRegistry& content() { return registry.ctx().get<game::gameplay::FContentRegistry>(); }
    game::FIntentStore* store() { return registry.ctx().find<game::FIntentStore>(); }
    game::FTurnSnapshot snapshot() { return registry.ctx().get<game::FTurnRuntime>().controller.snapshot(); }
    game::FCombatRuntime& combat() { return registry.ctx().get<game::FCombatRuntime>(); }
    game::FTurnRuntime& runtime() { return registry.ctx().get<game::FTurnRuntime>(); }
    game::FInputState& input() { return registry.ctx().get<game::FInputState>(); }
    game::FBattleOutcome& outcome() { return registry.ctx().get<game::FBattleOutcome>(); }

    /// Kill the generated roster and clear the terrain: the test places its own fight.
    void arena() {
        registry.ctx().get<game::FGridTerrain>().cells.fill(game::ETile::Floor);
        for (const auto entity : everyone) {
            (void)game::ApplyDamageToUnit(registry, entity, 100000);
        }
    }

    entt::entity place(entt::entity unit, game::FGridPosition cell, int hpPercent = 100) {
        registry.remove<game::FDead>(unit);
        auto& health = registry.get<game::FHealth>(unit);
        health.current = std::max(1, health.max * hpPercent / 100);
        registry.get<game::FGridPosition>(unit) = cell;
        registry.emplace_or_replace<game::FTurnParticipant>(unit);
        refill(unit);
        return unit;
    }

    void refill(entt::entity unit) {
        auto& points = registry.get<game::FActionPoints>(unit);
        points.move = points.moveMax;
        points.skill = points.skillMax;
    }

    void skills(entt::entity unit, std::vector<std::string> ids) {
        auto& loadout = registry.get<game::FSkillLoadout>(unit);
        loadout.slots.clear();
        for (std::string& id : ids) {
            loadout.slots.push_back({std::move(id), {}});
        }
    }

    void speed(entt::entity unit, std::int32_t value) { registry.get<game::FSpeed>(unit).value = value; }

    entt::entity party(std::string_view id) {
        for (const auto entity : everyone) {
            if (registry.get<game::FUnitData>(entity).definition.id == id) return entity;
        }
        throw std::runtime_error("no unit " + std::string(id));
    }

    std::vector<entt::entity> enemies() {
        std::vector<entt::entity> result;
        for (const auto entity : everyone) {
            if (registry.all_of<game::FEnemy>(entity)) result.push_back(entity);
        }
        return result;
    }

    void beginFrame() {
        if (auto* bus = registry.ctx().find<game::FEventBus>()) bus->beginFrame();
    }

    /// One engine frame start->flow. Round events are frame-scoped, so every driven frame starts the bus.
    void frame(float dt = 0.5f) {
        beginFrame();
        turns.update(registry, dt);
        flow.update(registry, dt);
    }

    void drain() { actions.update(registry, 0.0f); }
};

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// ---------------------------------------------------------------------------------------------
// 1. The Free/Zone text model and the size table
// ---------------------------------------------------------------------------------------------

void HudTextFollowsFreeScope() {
    FCaseTimer timer(__func__);
    FImGuiFixture imgui;
    for (const float scale : {game::kTextScaleNormal, 1.15f, game::kTextScaleLargest}) {
        game::ui::ApplyTextScale(scale);
        CHECK(std::fabs(HudTextPx(18.f, 1.f, EHudText::Free) - 18.f * scale) < 1e-4f);
        CHECK(std::fabs(HudTextPx(18.f, 1.f, EHudText::Zone) - 18.f) < 1e-4f);
        CHECK(std::fabs(HudTextPx(15.f, 2.f, EHudText::Zone) - 30.f) < 1e-4f);
        CHECK(std::fabs(HudTextPx(15.f, 2.f, EHudText::Free) - 30.f * scale) < 1e-4f);
    }
}

void MinSizeTableReported() {
    FCaseTimer timer(__func__);
    const auto rows = game::ui::HudTextSizeTable();
    CHECK(!rows.empty());

    // Class A: the XAG-critical gameplay numbers stay at or above 18 design px.
    int classA = 0;
    for (const auto& site : rows) {
        CHECK(!site.id.empty());
        if (!site.classA) continue;
        ++classA;
        CHECK(!site.residualGap);
        if (site.basePx < 18.f) {
            std::cout << "  class A site " << site.id << " is " << site.basePx << " px (< 18)\n";
        }
        CHECK(site.basePx >= 18.f);
    }
    CHECK(classA == 7);  // intent / preview / pop / threat / HP / skill key / skill AP

    // Class B: the WP's explicit 84% report — 15px, and the list equals the WP's list in its order.
    std::vector<std::string_view> gaps;
    for (const auto& site : rows) {
        if (!site.residualGap) continue;
        CHECK(!site.classA);
        CHECK(std::fabs(site.basePx - 15.f) < .01f);
        gaps.push_back(site.id);
    }
    constexpr std::array<std::string_view, 11> kExpectedGaps{
        "ribbon_round", "ribbon_marker", "unit_role",  "move_skill",   "planning",  "status_summary",
        "status_dot",   "skip",          "journal",     "hover_tooltip", "topbar_title"};
    CHECK(gaps.size() == kExpectedGaps.size());
    CHECK(std::equal(gaps.begin(), gaps.end(), kExpectedGaps.begin()));

    // The two annotated non-gap Class B rows: the "+N" hierarchy tier and the Cinzel name (font row).
    const auto site = [&rows](std::string_view id) -> const game::ui::FHudTextSite* {
        for (const auto& row : rows) {
            if (row.id == id) return &row;
        }
        return nullptr;
    };
    const auto* plus = site("intent_plus");
    CHECK(plus != nullptr && std::fabs(plus->basePx - 12.f) < .01f && !plus->classA && !plus->residualGap);
    const auto* name = site("unit_name");
    CHECK(name != nullptr && std::fabs(name->basePx - 15.f) < .01f && !name->classA && !name->residualGap);
    std::size_t nonGapLabels = 0;
    for (const auto& row : rows) {
        if (!row.classA && !row.residualGap) ++nonGapLabels;
    }
    CHECK(nonGapLabels == 2);
}

// ---------------------------------------------------------------------------------------------
// 2. Name and role never overlap at 1.25 (the w7-ux "ROWANArcher" defect)
// ---------------------------------------------------------------------------------------------

void NameAndRoleNeverOverlap() {
    FCaseTimer timer(__func__);
    FImGuiFixture imgui;
    ImGuiIO& io = ImGui::GetIO();
    CHECK(io.FontDefault != nullptr);              // the bundled Alegreya (body)
    CHECK(game::ui::HeadingFont() != nullptr);     // the bundled Cinzel (name)
    game::ui::ApplyTextScale(game::kTextScaleLargest);
    ImGui::NewFrame();

    // The helper must return the width ImGui itself lays out for PushFont(heading, size) text.
    ImGui::PushFont(game::ui::HeadingFont(), 15.f);
    const float laidOut = ImGui::CalcTextSize("Lumen").x;
    ImGui::PopFont();
    const float measured = game::ui::HeadingTextWidth("Lumen", 15.f);
    // ImGui lays a pushed font out with its rounded width (IM_TRUNC(measured + 0.99999f)); the helper
    // reports the unrounded CalcTextSizeA measurement. The layout width is therefore the measurement
    // rounded UP and never equal to it: measured <= laidOut < measured + 1. (Round 1 asserted equality —
    // wrong by construction.)
    const float slack = laidOut - measured;
    if (!(slack >= -.01f && slack < 1.f)) {
        std::cout << "  helper " << measured << " px vs ImGui layout " << laidOut << " px\n";
    }
    CHECK(slack >= -.01f && slack < 1.f);

    // The shipped unit names through the real asset boundary: the party block the run installs from and
    // the generation pool / units table the rooms draw their enemies from. No hard-coded name.
    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    const auto* assets = registry.ctx().find<game::FAssetManager>();
    CHECK(assets != nullptr);
    const auto digest = assets->digest("data/dungeon");
    CHECK(digest != nullptr);
    const auto& document = digest->json();
    CHECK(game::gameplay::FGameplayServices::Initialize(registry));
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();

    std::vector<std::string> names;
    for (const auto& member : document.at("party")) {
        names.push_back(member.at("display_name").get<std::string>());
    }
    const auto& generation = document.at("generation");
    for (const auto& enemy : generation.at("enemies")) {
        names.push_back(enemy.at("display_name").get<std::string>());
    }
    names.push_back(generation.at("boss_name").get<std::string>());
    for (const auto& unit : content.units.rows) {
        names.push_back(unit.displayName);
    }
    std::ranges::sort(names);
    names.erase(std::unique(names.begin(), names.end()), names.end());
    CHECK(!names.empty());

    // The HUD's split convention ("Name / Role", EmberCombatHudView.cpp FirstName/RoleName).
    const auto split = [](const std::string& full) {
        const auto at = full.find(" /");
        if (at == std::string::npos) return std::pair{full, std::string{}};
        std::size_t start = at + 2;
        while (start < full.size() && full[start] == ' ') ++start;
        return std::pair{full.substr(0, at), full.substr(start)};
    };

    ImFont* body = io.FontDefault;
    float worst = 0.f;
    std::string worstName;
    for (const std::string& full : names) {
        const auto [name, role] = split(full);
        const float nameWidth = game::ui::HeadingTextWidth(name.c_str(), 15.f);
        const float roleWidth = role.empty() ? 0.f : body->CalcTextSizeA(15.f, 1000.f, 0.f, role.c_str()).x;
        const float used = nameWidth + (role.empty() ? 0.f : 6.f + roleWidth);
        if (used > worst) {
            worst = used;
            worstName = full;
        }
        if (!role.empty()) {
            // The w7-ux guarantee: the name + gap + role line fits the readout's content box
            // (zone.w - 32u = 118px at the design window). Every shipped pair passes.
            CHECK(used <= 118.f);
        } else {
            // Role-less names may use half of the panel's 16u side padding (118 + 8 = 126px) and must
            // never reach the frame; the boss "Stone guardian" measures 120.62px at the effective size.
            CHECK(used <= 126.f);
        }
    }
    std::cout << "  longest name+role at 1.25: \"" << worstName << "\" = " << worst << " px (budget 118; role-less cap 126)\n";
}

// ---------------------------------------------------------------------------------------------
// 3. Ribbon step and marker (the 15px row against the authored bar)
// ---------------------------------------------------------------------------------------------

void RibbonRoundStepMeasured() {
    FCaseTimer timer(__func__);
    FImGuiFixture imgui;
    ImGui::NewFrame();
    game::ui::ApplyTextScale(game::kTextScaleNormal);

    // The body-font measurement the ribbon's LabelWidth performs at the new 15px row.
    const float px = HudTextPx(game::ui::hud::kRibbonRoundPx, 1.f, EHudText::Zone);
    const float widest = ImGui::GetFont()->CalcTextSizeA(px, 1000.f, 0.f, "ROUND 12").x;
    std::cout << "  \"ROUND 12\" at 15px = " << widest << " px\n";
    CHECK(widest >= 51.9f - .5f);  // the WP's font reference at 15px (Alegreya, 4151 glyph units)
    // The view advances by max(54u, measured + 10u): at this size the measurement binds, so the step is
    // at least the WP's 61.9 reference and the old bare 54u advance can no longer be used at round 12.
    CHECK(widest + 10.f > 54.f);
    CHECK(widest + 10.f >= 61.9f - .5f);
}

void MarkerFitsZone() {
    FCaseTimer timer(__func__);
    FImGuiFixture imgui;
    ImGui::NewFrame();

    const float px = HudTextPx(game::ui::hud::kRibbonMarkerPx, 1.f, EHudText::Zone);
    const float width = ImGui::GetFont()->CalcTextSizeA(px, 1000.f, 0.f, game::ecs::kNextRoundMarker).x;
    std::cout << "  next-round marker at 15px = " << width << " px (zone budget 296)\n";
    // The view's IM_ASSERT against the authored 320px bar minus 24u; the test pins the same numbers.
    CHECK(width <= 320.f - 24.f);
    CHECK(width >= 229.3f - 2.f && width <= 229.3f + 2.f);
}

// ---------------------------------------------------------------------------------------------
// 4. Board plates: caret anchor, outline, lift cap
// ---------------------------------------------------------------------------------------------

/// Drive one real render frame with an enemy intent over the warrior's cell and read the background draw
/// list: the caret must run from the plate's bottom edge down to the HP bar's top line, and the plate must
/// stay clear of the bar. The bar geometry comes from FIsoView.h, the same constants DrawUnit draws with.
void PlateClearsOccupantHpBar() {
    FCaseTimer timer(__func__);
    FImGuiFixture imgui;
    FBattleFixture fixture;

    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {5, 4});
    fixture.skills(imp, {"skill.strike"});
    fixture.frame(0.1f);

    game::FIntent intent;
    intent.mode = game::EIntentMode::Unit;
    intent.skillId = "skill.strike";
    intent.target = warrior;
    intent.aim = fixture.registry.get<game::FGridPosition>(warrior);
    intent.attack = true;
    intent.committedRound = fixture.snapshot().roundNumber;
    CHECK(fixture.store() != nullptr);
    fixture.store()->intents[imp] = intent;

    game::ecs::EmberCombatHudSystem hud;
    hud.boundTurn = fixture.snapshot().currentTurn->turnSerial;
    hud.update(fixture.registry, 0.f);
    const game::ecs::FCombatInfoSnapshot* info = game::ecs::FindCombatInfoSnapshot(fixture.registry);
    CHECK(info != nullptr && !info->intents.empty());

    ImGui::NewFrame();
    // Geometry readout: AA fill writes an inner/outer vertex pair per corner (the outer ring alpha-0) and
    // offsets the corners by the fringe, so turn it off for this frame — the caret then lands verbatim.
    ImGui::GetBackgroundDrawList()->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    hud.render(fixture.registry);

    const auto view = game::ui::FindBattleIsoView(fixture.registry, fixture.registry.ctx().get<game::FGridMetrics>());
    CHECK(view.has_value());
    const glm::vec2 center = view->cellCenter(5, 5);
    const float barTop = center.y - (game::kUnitHeadAboveFeet - game::kUnitHealthBarInset) * view->scale;
    const float unit = game::ui::FindUiLayout(fixture.registry)->scale;

    // The caret is the only kIntentNumber geometry in the cell-centre column: two vertices on the plate's
    // bottom edge and two on the bar's top line. Read the draw list while the frame is still open.
    const ImU32 caretColor = IM_COL32(255, 226, 190, 255);
    const ImDrawList* draw = ImGui::GetBackgroundDrawList();
    int tips = 0;
    float plateBottom = -1e9f;
    for (const ImDrawVert& vertex : draw->VtxBuffer) {
        if (vertex.col != caretColor) continue;
        if (std::fabs(vertex.pos.x - center.x) > 3.f * unit) continue;
        if (vertex.pos.y < barTop - 200.f * view->scale || vertex.pos.y > barTop + .01f) continue;
        if (std::fabs(vertex.pos.y - barTop) < .01f) ++tips;
        else plateBottom = std::max(plateBottom, vertex.pos.y);
    }
    std::cout << "  bar top " << barTop << ", plate bottom " << plateBottom << ", caret tips " << tips << "\n";
    CHECK(tips == 2);                       // the caret reaches the HP bar's top line
    CHECK(plateBottom < barTop - 1.f);      // ...and the plate itself stays clear of the bar
    CHECK(barTop < center.y);               // the anchor really is the bar (occupied cell), not the centre
}

void PlateHasOutlineAndCaret() {
    FCaseTimer timer(__func__);
    FImGuiFixture imgui;
    ImGui::NewFrame();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    // Exact vertices: AA fill writes an inner/outer pair per corner (the outer ring alpha-0) and offsets
    // the corners by the fringe — this case pins the helper's own points.
    draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;

    const ImVec2 plateMin{100.f, 100.f};
    const ImVec2 plateMax{140.f, 120.f};
    const ImVec2 anchor{120.f, 140.f};
    const int before = draw->VtxBuffer.Size;
    game::ui::DrawPlateOutline(draw, plateMin, plateMax, 3.f, game::ui::palette::kBronze);
    const int outlineEnd = draw->VtxBuffer.Size;
    game::ui::DrawPlateCaret(draw, {120.f, 120.f}, anchor, 1.f, IM_COL32(255, 226, 190, 255));
    const int caretEnd = draw->VtxBuffer.Size;

    // Outline present: vertices sitting on the plate's left/right edges.
    CHECK(outlineEnd > before);
    bool leftEdge = false;
    bool rightEdge = false;
    for (int index = before; index < outlineEnd; ++index) {
        const ImVec2 point = draw->VtxBuffer[index].pos;
        if (std::fabs(point.x - plateMin.x) <= 1.f && point.y >= plateMin.y - 1.f && point.y <= plateMax.y + 1.f) {
            leftEdge = true;
        }
        if (std::fabs(point.x - plateMax.x) <= 1.f && point.y >= plateMin.y - 1.f && point.y <= plateMax.y + 1.f) {
            rightEdge = true;
        }
    }
    CHECK(leftEdge && rightEdge);

    // The caret is the last quad: from the plate's bottom centre (4u wide) to the anchor (2u wide).
    CHECK(caretEnd >= outlineEnd + 4);
    const ImVec2 topLeft = draw->VtxBuffer[caretEnd - 4].pos;
    const ImVec2 topRight = draw->VtxBuffer[caretEnd - 3].pos;
    const ImVec2 bottomRight = draw->VtxBuffer[caretEnd - 2].pos;
    const ImVec2 bottomLeft = draw->VtxBuffer[caretEnd - 1].pos;
    CHECK(topLeft.y == 120.f && topRight.y == 120.f && bottomRight.y == 140.f && bottomLeft.y == 140.f);
    CHECK(topLeft.x == 118.f && topRight.x == 122.f);
    CHECK(bottomRight.x == 121.f && bottomLeft.x == 119.f);
    CHECK(topLeft.y < bottomLeft.y);  // the caret points from the plate down to the anchor
}

void PreviewLiftCappedAtTwoSteps() {
    FCaseTimer timer(__func__);
    // The same function both overlays call: position i lifts by min(i, 2), so a 3rd+ plate stops rising.
    CHECK(game::ui::PlateLiftSteps(0) == 0);
    CHECK(game::ui::PlateLiftSteps(1) == 1);
    CHECK(game::ui::PlateLiftSteps(2) == 2);
    CHECK(game::ui::PlateLiftSteps(3) == 2);
    CHECK(game::ui::PlateLiftSteps(7) == 2);

    // ...and both overlays really read it (a re-introduced unbounded lift would break the cap).
    const std::string source = ReadText("src/ecs/systems/EmberCombatHudView.cpp");
    CHECK(!source.empty());
    std::size_t uses = 0;
    for (std::size_t at = source.find("PlateLiftSteps"); at != std::string::npos;
         at = source.find("PlateLiftSteps", at + 1)) {
        ++uses;
    }
    CHECK(uses >= 2);  // IntentOverlays + PreviewOverlays
}

// ---------------------------------------------------------------------------------------------
// 5. The unit readout inside the panel frame (WP-27h — the frame band is fixed screen px)
// ---------------------------------------------------------------------------------------------

/// Every readout glyph (name, role, HP numbers, M/S, SKIP + the joined status summary) must sit inside the
/// hud_panel picture's inner rect: the 16px top/bottom and 13px left/right frame bands are fixed SCREEN px
/// (ui::hud::kPanelFrameXPx/YPx) and never scale with u. One real render frame per (window, text scale)
/// case; glyph quads come out of the draw data (font-atlas commands, 6 indices / 4 vertices per glyph; a
/// quad is untextured when all four corners carry the atlas white-pixel UV). "Kestrel / Sentinel" is ~18
/// characters and still fits the w7-ux 118px name+role budget NameAndRoleNeverOverlap pins.
void ReadoutTextInsidePanelFrame() {
    FCaseTimer timer(__func__);
    struct FCase {
        int width;
        int height;
        float textScale;
    };
    constexpr FCase kCases[]{{1280, 800, 1.f}, {1280, 800, 1.25f}, {1920, 1080, 1.f}, {1920, 1080, 1.25f}};
    // kLethalText (file-local in EmberCombatHudView.cpp): the decided-turn-loss colour; inside the unit
    // zone only the SKIP marker uses it.
    constexpr ImU32 kSkipColor = IM_COL32(255, 70, 60, 255);
    for (const FCase& test : kCases) {
        FImGuiFixture imgui;
        FBattleFixture fixture;
        fixture.arena();
        const auto actor = fixture.place(fixture.party("party.archer"), {5, 5});
        fixture.registry.get<game::FUnitData>(actor).definition.displayName = "Kestrel / Sentinel";
        const auto imp = fixture.place(fixture.enemies()[0], {5, 4});
        fixture.skills(actor, {"skill.strike"});
        fixture.speed(actor, 100);
        fixture.speed(imp, 1);
        fixture.frame(0.1f);
        CHECK(fixture.snapshot().currentTurn->actor == actor);

        // Fixture content (never ship balance): Burn is the dot half of the summary; the on_apply control's
        // roll is already decided (skipPending), so the readout must show SKIP in front of the summary.
        auto& statuses = fixture.registry.emplace_or_replace<game::FActiveStatuses>(actor);
        statuses.effects = {{"status.burn", 3, 3, false}, {"status.paralyze", 2, 50, true}};

        auto& window = fixture.registry.ctx().get<game::FEngineWindowState>();
        window.width = test.width;
        window.height = test.height;
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = {static_cast<float>(test.width), static_cast<float>(test.height)};
        game::ui::ApplyTextScale(test.textScale);

        game::ecs::EmberCombatHudSystem hud;
        hud.boundTurn = fixture.snapshot().currentTurn->turnSerial;
        hud.update(fixture.registry, 0.f);
        ImGui::NewFrame();
        hud.render(fixture.registry);
        ImGui::Render();

        const game::ui::FUiLayout* layout = game::ui::FindUiLayout(fixture.registry);
        CHECK(layout != nullptr);
        const FRect& zone = layout->zones.unit;
        const float u = layout->scale;
        const float frameLeft = zone.x + game::ui::hud::kPanelFrameXPx;
        const float frameTop = zone.y + game::ui::hud::kPanelFrameYPx;
        const float frameRight = zone.right() - game::ui::hud::kPanelFrameXPx;
        const float frameBottom = zone.bottom() - game::ui::hud::kPanelFrameYPx;

        // Glyph quads out of the draw data: commands bound to the font atlas only (shapes draw the atlas
        // white pixel on all four corners). Raw positions, not the clipped ones. The headless test atlas is
        // never uploaded by a backend, so GetTexID() would hit the "wasn't uploaded to graphics system"
        // assert (imgui.h:3926-3935); identify atlas commands by their texture-data pointer instead. Every
        // atlas page is listed in TexList, and a non-atlas texture (e.g. the nine-slice) carries only a raw
        // _TexID with _TexData == nullptr.
        const ImVec2 white = io.Fonts->TexUvWhitePixel;
        const auto isFontAtlasTexture = [fonts = io.Fonts](const ImTextureRef& ref) {
            if (ref._TexData == nullptr) return false;
            for (const ImTextureData* page : fonts->TexList) {
                if (page == ref._TexData) return true;
            }
            return false;
        };
        struct FGlyph {
            float x0{0.f};
            float y0{0.f};
            float x1{0.f};
            float y1{0.f};
            float centreY{0.f};
            ImU32 colour{0};
            ImVec4 clip{};
        };
        std::vector<FGlyph> glyphs;
        const ImDrawData* data = ImGui::GetDrawData();
        CHECK(data != nullptr);
        for (int list = 0; list < data->CmdListsCount; ++list) {
            const ImDrawList* draw = data->CmdLists[list];
            for (const ImDrawCmd& command : draw->CmdBuffer) {
                if (command.UserCallback != nullptr || !isFontAtlasTexture(command.TexRef) ||
                    command.ElemCount == 0) {
                    continue;
                }
                // A merged command mixes primitives: ImDrawList emits triangles (3 indices) and quads (6
                // indices, exactly (i, i+1, i+2, i, i+2, i+3)). Group by the index pattern instead of
                // stepping blindly by 6 — a blind step over a fill's triangles fuses shape and glyph
                // vertices into bogus quads.
                for (unsigned int at = 0; at < command.ElemCount;) {
                    const ImDrawIdx* order = draw->IdxBuffer.Data + command.IdxOffset + at;
                    const bool isQuad = at + 6 <= command.ElemCount && order[3] == order[0] &&
                                        order[4] == order[2] &&
                                        order[5] == static_cast<ImDrawIdx>(order[2] + 1);
                    if (!isQuad) {
                        at += 3;  // a triangle (fill / CPU-AA edge): never a glyph; resync after it
                        continue;
                    }
                    at += 6;
                    const int base = static_cast<int>(command.VtxOffset);
                    const ImDrawVert& a = draw->VtxBuffer[base + order[0]];
                    const ImDrawVert& b = draw->VtxBuffer[base + order[1]];
                    const ImDrawVert& c = draw->VtxBuffer[base + order[2]];
                    const ImDrawVert& d = draw->VtxBuffer[base + order[5]];
                    const auto isWhite = [&white](const ImVec2& uv) { return uv.x == white.x && uv.y == white.y; };
                    if (isWhite(a.uv) && isWhite(b.uv) && isWhite(c.uv) && isWhite(d.uv)) continue;
                    FGlyph glyph;
                    glyph.colour = a.col;
                    glyph.clip = command.ClipRect;
                    glyph.x0 = std::min({a.pos.x, b.pos.x, c.pos.x, d.pos.x});
                    glyph.y0 = std::min({a.pos.y, b.pos.y, c.pos.y, d.pos.y});
                    glyph.x1 = std::max({a.pos.x, b.pos.x, c.pos.x, d.pos.x});
                    glyph.y1 = std::max({a.pos.y, b.pos.y, c.pos.y, d.pos.y});
                    glyph.centreY = (glyph.y0 + glyph.y1) * .5f;
                    const float centreX = (glyph.x0 + glyph.x1) * .5f;
                    if (centreX < zone.x || centreX > zone.right() || glyph.centreY < zone.y ||
                        glyph.centreY > zone.bottom()) {
                        continue;
                    }
                    glyphs.push_back(glyph);
                }
            }
        }
        float minY = 1e9f;
        float maxY = -1e9f;
        const ImU32 nameColour = ImGui::GetColorU32(game::ui::palette::kGoldV);
        int skipQuads = 0;
        int summaryQuads = 0;
        int nameQuads = 0;
        int verticalViolations = 0;
        int horizontalViolations = 0;
        for (const FGlyph& glyph : glyphs) {
            minY = std::min(minY, glyph.y0);
            maxY = std::max(maxY, glyph.y1);
            // VERTICAL: the raw (unclipped) ink stays inside the frame's inner rect — a clipped-away
            // bottom would otherwise hide a real overflow.
            if (glyph.y0 < frameTop - .5f || glyph.y1 > frameBottom + .5f) {
                ++verticalViolations;
                std::cout << "  [FAIL detail] glyph y [" << glyph.y0 << "," << glyph.y1 << "] x [" << glyph.x0
                          << "," << glyph.x1 << "] outside y [" << frameTop << "," << frameBottom << "] colour "
                          << glyph.colour << "\n";
            }
            // HORIZONTAL: what the command's clip rect actually shows must stay inside it too; a quad
            // fully clipped away is ignored.
            const float shownLeft = std::max(glyph.x0, glyph.clip.x);
            const float shownRight = std::min(glyph.x1, glyph.clip.z);
            if (shownRight > shownLeft && (shownLeft < frameLeft - .5f || shownRight > frameRight + .5f)) {
                ++horizontalViolations;
                std::cout << "  [FAIL detail] glyph x [" << shownLeft << "," << shownRight << "] outside ["
                          << frameLeft << "," << frameRight << "] colour " << glyph.colour << "\n";
            }
            if (glyph.colour == kSkipColor) ++skipQuads;
            if (glyph.colour == game::ui::palette::kMuted && glyph.centreY >= zone.y + 60.f * u) ++summaryQuads;
            if (glyph.colour == nameColour) ++nameQuads;
        }
        // The [report] must survive a failing CHECK (the harness throws out of the case), so it prints before
        // them; a clean failure prints the measured picture instead of vanishing with the exception.
        std::cout << "  [report] " << test.width << "x" << test.height << " text " << test.textScale << ": u=" << u
                  << " zone [" << zone.x << "," << zone.y << ".." << zone.right() << "," << zone.bottom() << "]"
                  << " frame [" << frameLeft << "," << frameTop << ".." << frameRight << "," << frameBottom << "]"
                  << " glyphs " << glyphs.size();
        if (!glyphs.empty()) {
            std::cout << " y [" << minY << "," << maxY << "]";
        }
        std::cout << " skip " << skipQuads << " summary " << summaryQuads << " name " << nameQuads << " violations "
                  << verticalViolations << "," << horizontalViolations << "\n";
        // Non-vacuous: the SKIP marker (one quad per letter), the summary row and the name really rendered.
        CHECK(!glyphs.empty());
        CHECK(verticalViolations == 0);
        CHECK(horizontalViolations == 0);
        CHECK(skipQuads >= 4);
        CHECK(summaryQuads > 0);
        CHECK(nameQuads > 0);
        for (const FGlyph& skip : glyphs) {
            if (skip.colour != kSkipColor) continue;
            for (const FGlyph& name : glyphs) {
                if (name.colour != nameColour) continue;
                const bool overlap =
                    skip.x0 < name.x1 && name.x0 < skip.x1 && skip.y0 < name.y1 && name.y0 < skip.y1;
                CHECK(!overlap);
            }
        }
        // The fixture (and its ImGui context) dies at the end of this iteration, so the suite's text scale
        // is restored here, while the context is still alive.
        game::ui::ApplyTextScale(1.f);
    }
}

// ---------------------------------------------------------------------------------------------
// 6. Non-colour channels: status gem shapes and the selection outlines
// ---------------------------------------------------------------------------------------------

void StatusGemShapePerKind() {
    FCaseTimer timer(__func__);
    using game::ui::EStatusGemShape;
    // Five kinds -> five distinct shapes; unknown kinds get the hollow ring.
    CHECK(game::ui::StatusGemShapeFor(game::gameplay::kStatusKindDot) == EStatusGemShape::Circle);
    CHECK(game::ui::StatusGemShapeFor(game::gameplay::kStatusKindControl) == EStatusGemShape::Triangle);
    CHECK(game::ui::StatusGemShapeFor(game::gameplay::kStatusKindStat) == EStatusGemShape::Square);
    CHECK(game::ui::StatusGemShapeFor(game::gameplay::kStatusKindMoveLock) == EStatusGemShape::Diamond);
    CHECK(game::ui::StatusGemShapeFor("unknown.kind") == EStatusGemShape::Ring);

    // Draw-data check: each shape leaves its own vertex sequence.
    FImGuiFixture imgui;
    ImGui::NewFrame();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    constexpr std::array<EStatusGemShape, 5> kShapes{EStatusGemShape::Circle, EStatusGemShape::Triangle,
                                                     EStatusGemShape::Square, EStatusGemShape::Diamond,
                                                     EStatusGemShape::Ring};
    std::array<std::vector<ImVec2>, kShapes.size()> drawn;
    for (std::size_t index = 0; index < kShapes.size(); ++index) {
        const int before = draw->VtxBuffer.Size;
        game::ui::DrawStatusGem(draw, {200.f, 200.f}, 3.f, kShapes[index], IM_COL32(240, 130, 50, 255));
        CHECK(draw->VtxBuffer.Size > before);
        for (int at = before; at < draw->VtxBuffer.Size; ++at) {
            drawn[index].push_back(draw->VtxBuffer[at].pos);
        }
    }
    for (std::size_t left = 0; left < kShapes.size(); ++left) {
        for (std::size_t right = left + 1; right < kShapes.size(); ++right) {
            bool distinct = drawn[left].size() != drawn[right].size();
            if (!distinct) {
                distinct = !std::equal(drawn[left].begin(), drawn[left].end(), drawn[right].begin(),
                                       [](ImVec2 a, ImVec2 b) { return a.x == b.x && a.y == b.y; });
            }
            CHECK(distinct);
        }
    }
}

void AffectedCellsUseDoubleOutline() {
    FCaseTimer timer(__func__);
    const auto reachable = game::ui::SelectionOutlineInsets(game::ui::ESelectionCue::Reachable);
    const auto affected = game::ui::SelectionOutlineInsets(game::ui::ESelectionCue::Affected);
    CHECK(reachable.size() == 1);
    CHECK(affected.size() == 2);
    CHECK(reachable[0] == 1.f);
    CHECK(affected[0] == 1.f && affected[1] == 3.f);

    // The grid renderer iterates the same table for both cues (source contract, like SubmitPathSites).
    const std::string source = ReadText("src/ecs/systems/EmberGridRenderSystem.cpp");
    CHECK(!source.empty());
    CHECK(source.find("SelectionOutlineInsets(ui::ESelectionCue::Reachable)") != std::string::npos);
    CHECK(source.find("SelectionOutlineInsets(ui::ESelectionCue::Affected)") != std::string::npos);
}

void HoverCursorExempt() {
    FCaseTimer timer(__func__);
    const auto hovered = game::ui::SelectionOutlineInsets(game::ui::ESelectionCue::Hovered);
    CHECK(hovered.size() == 2);  // already double: 0 + 2
    CHECK(hovered[0] == 0.f && hovered[1] == 2.f);

    // The exemption is the pointer-cursor affordance, not a range verdict — named in the renderer source.
    const std::string source = ReadText("src/ecs/systems/EmberGridRenderSystem.cpp");
    CHECK(!source.empty());
    CHECK(source.find("pointer-cursor affordance") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// 7. Click = select, through the real view
// ---------------------------------------------------------------------------------------------

void ClickSelectsSkill() {
    FCaseTimer timer(__func__);
    FImGuiFixture imgui;
    FBattleFixture fixture;

    // The archer acts; an enemy 3 cells away is out of strike range but inside arrow range.
    fixture.arena();
    const auto archer = fixture.place(fixture.party("party.archer"), {5, 5});
    const auto imp = fixture.place(fixture.enemies()[0], {8, 5});
    fixture.skills(archer, {"skill.strike", "skill.shove", "skill.arrow", "skill.volley"});
    fixture.speed(archer, 100);
    fixture.speed(imp, 1);
    fixture.frame(0.1f);
    CHECK(fixture.snapshot().currentTurn->actor == archer);

    const game::gameplay::FContentRegistry& content = fixture.content();
    const auto& slots = fixture.registry.get<game::FSkillLoadout>(archer).slots;
    CHECK(slots.size() >= 3);

    const auto view = game::ui::FindBattleIsoView(fixture.registry, fixture.registry.ctx().get<game::FGridMetrics>());
    CHECK(view.has_value());
    auto& input = fixture.input();
    input.mousePos = view->cellCenter(8, 5);  // hover the enemy: the snapshot builds the preview from it
    input.mouseLeftPressed = false;

    game::ecs::EmberCombatHudSystem hud;
    hud.boundTurn = fixture.snapshot().currentTurn->turnSerial;

    // Frame 0: the default slot 0 (strike, range 1) cannot reach (8,5): no preview.
    hud.update(fixture.registry, 0.f);
    const game::ecs::FCombatInfoSnapshot* before = game::ecs::FindCombatInfoSnapshot(fixture.registry);
    CHECK(before != nullptr);
    CHECK(fixture.combat().selectedSkill == 0);
    CHECK(before->preview.empty());

    // A real click on slot 2's rect through the ImGui view, driven like the real loop. ImGui 1.92
    // trickles queued input (ConfigInputTrickleEventQueue): a position and a button queued together are
    // applied over different frames, and the hovered window comes from the previous frame's window list,
    // so the press needs a frame where the mouse is already there and the Dock window is hoverable.
    const FUiLayout* layout = game::ui::FindUiLayout(fixture.registry);
    CHECK(layout != nullptr);
    const FRect slot = game::ui::SkillSlotRect(layout->zones.skills, layout->scale, 2);
    ImGuiIO& io = ImGui::GetIO();
    const auto frame = [&] {
        ImGui::NewFrame();
        hud.render(fixture.registry);
        ImGui::Render();
    };
    frame();  // create the Dock window
    frame();  // it is hoverable from this frame on
    io.AddMousePosEvent(slot.x + slot.w * .5f, slot.y + slot.h * .5f);
    frame();  // A: the mouse arrives
    io.AddMouseButtonEvent(0, true);
    frame();  // B: press
    io.AddMouseButtonEvent(0, false);
    frame();  // C: release — the click fires here
    frame();  // D: idle settle
    CHECK(hud.selectedSkill == 2);  // the view click owns the selection

    // The next update mirrors the click into the runtime and the snapshot (the 1-frame handoff).
    hud.update(fixture.registry, 0.f);
    CHECK(fixture.combat().selectedSkill == 2);
    const game::ecs::FCombatInfoSnapshot* byClick = game::ecs::FindCombatInfoSnapshot(fixture.registry);
    CHECK(byClick != nullptr);
    const auto resolved = game::ResolveSkillSlot(slots[2], content);
    CHECK(resolved.has_value());
    const auto expected = game::PreviewSkill(fixture.registry, archer, *resolved,
                                             fixture.registry.get<game::FGridPosition>(imp),
                                             fixture.registry.get<game::FGridPosition>(archer));
    CHECK(expected.has_value());
    CHECK(!byClick->preview.empty());
    CHECK(byClick->preview.size() == expected->targets.size());
    for (std::size_t index = 0; index < byClick->preview.size(); ++index) {
        const game::FPreviewTarget& target = expected->targets[index];
        CHECK(byClick->preview[index].cell == fixture.registry.get<game::FGridPosition>(target.unit));
        CHECK(byClick->preview[index].damage == target.damage);
        CHECK(byClick->preview[index].heal == target.heal);
        CHECK(byClick->preview[index].lethal == target.lethal);
    }
    std::cout << "  click selected slot 2; preview badges " << byClick->preview.size() << "\n";

    // The keyboard path selects the same slot and yields the same snapshot (key "3" = slot index 2).
    hud.selectedSkill = 0;
    fixture.combat().selectedSkill = 0;
    input.keysJustPressed[SDL_SCANCODE_3] = true;
    hud.update(fixture.registry, 0.f);
    input.keysJustPressed[SDL_SCANCODE_3] = false;
    CHECK(fixture.combat().selectedSkill == 2);
    const game::ecs::FCombatInfoSnapshot* byKey = game::ecs::FindCombatInfoSnapshot(fixture.registry);
    CHECK(byKey != nullptr);
    CHECK(byKey->preview.size() == byClick->preview.size());
    for (std::size_t index = 0; index < byKey->preview.size(); ++index) {
        CHECK(byKey->preview[index].cell == byClick->preview[index].cell);
        CHECK(byKey->preview[index].damage == byClick->preview[index].damage);
    }
}

// ---------------------------------------------------------------------------------------------
// 8. Intent persistence across the turn handoff (the WP-12c §6 contract)
// ---------------------------------------------------------------------------------------------

void IntentsPersistAcrossTurnHandoff() {
    FCaseTimer timer(__func__);
    FBattleFixture fixture;
    fixture.arena();
    const auto warrior = fixture.place(fixture.party("party.warrior"), {5, 5});
    fixture.speed(warrior, 100);
    std::vector<entt::entity> ids = fixture.enemies();
    std::ranges::sort(ids, [](entt::entity left, entt::entity right) {
        return entt::to_integral(left) < entt::to_integral(right);
    });
    CHECK(ids.size() >= 2);
    // Two enemies are enough for the contract ("one executes, the other keeps its badge"); a third would
    // only add driven frames to the sweep.
    const auto first = fixture.place(ids[0], {5, 4});
    const auto second = fixture.place(ids[1], {4, 5});
    for (const entt::entity enemy : {first, second}) {
        fixture.skills(enemy, {"skill.strike"});
        fixture.speed(enemy, 1);
    }
    fixture.frame(0.1f);
    CHECK(fixture.snapshot().currentTurn->actor == warrior);

    game::FIntentStore* store = fixture.store();
    CHECK(store != nullptr);
    CHECK(!store->intents.empty());

    game::ecs::EmberCombatHudSystem hud;
    hud.boundTurn = fixture.snapshot().currentTurn->turnSerial;
    hud.update(fixture.registry, 0.f);
    const game::ecs::FCombatInfoSnapshot* info = game::ecs::FindCombatInfoSnapshot(fixture.registry);
    CHECK(info != nullptr);
    CHECK(!info->intents.empty());
    const std::size_t committed = store->intents.size();
    std::cout << "  round 1 committed intents: " << committed << ", badges " << info->intents.size() << "\n";

    // The player ends the turn through the runtime — the same request the END TURN button pushes.
    fixture.runtime().requests.push_back(game::FEndTurn{*fixture.snapshot().currentTurn});

    bool sawEnemyTurn = false;
    std::size_t executions = 0;
    std::size_t previous = store->intents.size();
    std::vector<std::string> trace;
    for (int frames = 0; frames < 240; ++frames) {
        fixture.frame(0.25f);
        fixture.drain();
        hud.update(fixture.registry, 0.f);

        const game::ecs::FCombatInfoSnapshot* live = game::ecs::FindCombatInfoSnapshot(fixture.registry);
        CHECK(live != nullptr);
        const game::FTurnSnapshot turn = fixture.snapshot();

        // Per-frame evidence line for the report.
        std::size_t stacked = 0;
        for (const FIntentBadge& badge : live->intents) stacked += static_cast<std::size_t>(badge.stacked);
        const entt::entity actor = turn.currentTurn ? turn.currentTurn->actor : entt::null;
        const bool enemyActor = actor != entt::null && fixture.registry.all_of<game::FEnemy>(actor);
        if (enemyActor) sawEnemyTurn = true;
        trace.push_back("frame " + std::to_string(frames) + " actor " +
                        (actor != entt::null ? game::UnitDisplayName(fixture.registry, actor) : std::string("none")) +
                        " store " + std::to_string(store->intents.size()) + " badges " +
                        std::to_string(live->intents.size()) + " stacked " + std::to_string(stacked));

        // The contract (verified against BuildCombatInfoSnapshot): every stored intent becomes exactly one
        // badge (`stacked` starts at 1 and the same-cell collapse adds it), so Σ stacked equals the store
        // size whenever the snapshot was rebuilt from this store. A mismatch while the session is Running
        // therefore means a frame cleared the snapshot instead — the WP-12c §6 reproduction, not slack.
        CHECK(stacked == store->intents.size());
        for (const auto& [enemy, intent] : store->intents) {
            game::FGridPosition aim = intent.aim;
            if (intent.mode == game::EIntentMode::Unit && intent.target != entt::null) {
                if (const auto* cell = fixture.registry.try_get<game::FGridPosition>(intent.target)) aim = *cell;
            }
            bool covered = false;
            for (const FIntentBadge& badge : live->intents) {
                if (badge.cell == aim) covered = true;
            }
            CHECK(covered);
        }
        if (store->intents.size() < previous) executions += previous - store->intents.size();
        previous = store->intents.size();

        if (fixture.outcome().resolved) break;
        if (sawEnemyTurn && actor != entt::null && fixture.registry.all_of<game::FPartyMember>(actor)) break;
    }
    // Summary, not the full per-frame dump: a frame with missing badges would have failed the two CHECKs
    // above, so a passing run reports the handoff endpoints plus the sweep totals.
    CHECK(!trace.empty());
    std::cout << "  trace " << trace.size() << " frames; first [" << trace.front() << "]\n";
    std::cout << "  trace last [" << trace.back() << "]\n";
    CHECK(sawEnemyTurn);
    CHECK(executions >= 1);  // the sweep watched real executions, not a static frame
    std::cout << "  executions observed " << executions << " of " << committed << " committed\n";
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);  // an aborted case must still show every [ ok ] / [time] line
    // Save safety: ctest exports GAME01P_SAVE_DIR, but a manual run must never touch the user's real save.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "hud_legibility_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("hud legibility", {
        {"HudTextFollowsFreeScope", HudTextFollowsFreeScope},
        {"MinSizeTableReported", MinSizeTableReported},
        {"NameAndRoleNeverOverlap", NameAndRoleNeverOverlap},
        {"RibbonRoundStepMeasured", RibbonRoundStepMeasured},
        {"MarkerFitsZone", MarkerFitsZone},
        {"PlateClearsOccupantHpBar", PlateClearsOccupantHpBar},
        {"PlateHasOutlineAndCaret", PlateHasOutlineAndCaret},
        {"PreviewLiftCappedAtTwoSteps", PreviewLiftCappedAtTwoSteps},
        {"ReadoutTextInsidePanelFrame", ReadoutTextInsidePanelFrame},
        {"StatusGemShapePerKind", StatusGemShapePerKind},
        {"AffectedCellsUseDoubleOutline", AffectedCellsUseDoubleOutline},
        {"HoverCursorExempt", HoverCursorExempt},
        {"ClickSelectsSkill", ClickSelectsSkill},
        {"IntentsPersistAcrossTurnHandoff", IntentsPersistAcrossTurnHandoff},
    });
}
