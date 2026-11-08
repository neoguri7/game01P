// WP-24b session UX tests: the four-tab compendium (built from the content registry, nothing hand-authored),
// text scale persistence and the 1.25 HUD zone fit, the text scale never touching gameplay, the world
// faction shape marker, the six-row non-colour cue checklist, the onboarding walk (data-driven, replayable,
// non-blocking) and the title/menu wiring.
#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/Logger.h"
#include "ecs/systems/EmberCombatInfo.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/grid/FGridTypes.h"
#include "presentation/FCompendiumView.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FSettings.h"
#include "presentation/FTitleMenuView.h"
#include "presentation/FUiFrame.h"
#include "presentation/FUiLayout.h"
#include "presentation/FUiTheme.h"
#include "states/FEmberModalHostState.h"

namespace {

using game::FOnboardingContent;
using game::FOnboardingDecision;
using game::FOnboardingProgress;
using game::FOnboardingStep;
using game::FSettings;
using game::gameplay::FContentRegistry;
using game::gameplay::FRuneContent;
using game::gameplay::FSkillContent;
using game::gameplay::FStatusContent;
using game::gameplay::FUnitContent;
using game::ui::FRect;
using game::ui::FUiLayout;
using game::ui::FUiLayoutSpec;

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/// Sets GAME01P_SAVE_DIR for its lifetime and restores the previous value (even when a CHECK throws).
class FSavedEnv {
public:
    explicit FSavedEnv(const std::filesystem::path& value) {
        if (const char* previous = std::getenv(game::kSettingsSaveDirEnv)) saved = previous;
        SetEnv(value.string());
    }
    ~FSavedEnv() {
        if (saved.empty()) ClearEnv();
        else SetEnv(saved);
    }
    FSavedEnv(const FSavedEnv&) = delete;
    FSavedEnv& operator=(const FSavedEnv&) = delete;

    static void SetEnv(const std::string& value) {
#ifdef _WIN32
        _putenv_s(game::kSettingsSaveDirEnv, value.c_str());
#else
        setenv(game::kSettingsSaveDirEnv, value.c_str(), 1);
#endif
    }
    static void ClearEnv() {
#ifdef _WIN32
        _putenv_s(game::kSettingsSaveDirEnv, "");
#else
        unsetenv(game::kSettingsSaveDirEnv);
#endif
    }

private:
    std::string saved;
};

[[nodiscard]] std::filesystem::path IsolatedDir() {
    const char* dir = std::getenv(game::kSettingsSaveDirEnv);
    CHECK(dir != nullptr && *dir != '\0');
    return std::filesystem::path(dir);
}

[[nodiscard]] std::filesystem::path FreshDir(const char* name) {
    const std::filesystem::path dir = IsolatedDir() / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    CHECK(!error);
    return dir;
}

/// A headless ImGui context. No renderer backend on purpose: font advances still measure, which is all the
/// layout tests need. `io.IniFilename` stays null so no imgui.ini is ever written.
class FImGuiFixture {
public:
    FImGuiFixture() {
        context = ImGui::CreateContext();
        CHECK(context != nullptr);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = {1280.f, 800.f};
        io.DeltaTime = 1.f / 60.f;
        io.IniFilename = nullptr;
    }
    ~FImGuiFixture() { ImGui::DestroyContext(context); }
    FImGuiFixture(const FImGuiFixture&) = delete;
    FImGuiFixture& operator=(const FImGuiFixture&) = delete;

    void FinishFonts() { ImGui::GetIO().Fonts->Build(); }

private:
    ImGuiContext* context{nullptr};
};

/// Assets + gameplay content. The compendium reads the SHIPPED tables, so the counts are compared to the
/// same registry rather than written down (other lanes add content in parallel).
struct FContentFixture {
    entt::registry registry;
    FContentFixture() {
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
    }
    [[nodiscard]] const FContentRegistry& content() const { return registry.ctx().get<FContentRegistry>(); }
};

/// The shipped onboarding document through the real asset boundary.
struct FOnboardingFixture {
    entt::registry registry;
    FOnboardingFixture() {
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        game::LoadOnboardingContent(registry);
    }
    [[nodiscard]] const FOnboardingContent& content() const { return *game::FindOnboardingContent(registry); }
};

[[nodiscard]] const FUnitContent* FindUnit(const FContentRegistry& content, std::string_view id) {
    return content.units.find(id);
}

[[nodiscard]] FOnboardingDecision Step(const FOnboardingContent& content, const FOnboardingProgress& progress,
                                       std::optional<game::EOnboardingTrigger> trigger) {
    return game::OnboardingStepFor(content, progress, trigger);
}

// ---------------------------------------------------------------------------------------------
// Compendium
// ---------------------------------------------------------------------------------------------

void CompendiumEntriesComplete() {
    FContentFixture fixture;
    const FContentRegistry& content = fixture.content();
    const auto countEnemies = [&content] {
        std::size_t enemies = 0;
        for (const FUnitContent& unit : content.units.rows) {
            if (game::IsEnemyUnit(unit)) ++enemies;
        }
        return enemies;
    };

    const auto skills = game::BuildCompendiumTab(fixture.registry, game::ECompendiumTab::Skills);
    const auto runes = game::BuildCompendiumTab(fixture.registry, game::ECompendiumTab::Runes);
    const auto statuses = game::BuildCompendiumTab(fixture.registry, game::ECompendiumTab::Statuses);
    const auto enemies = game::BuildCompendiumTab(fixture.registry, game::ECompendiumTab::Enemies);
    CHECK(skills.size() == content.skills.rows.size());
    CHECK(runes.size() == content.runes.rows.size());
    CHECK(statuses.size() == content.statuses.rows.size());
    CHECK(enemies.size() == countEnemies());
    // The shipped tables are not empty, so the equalities above are not vacuous.
    CHECK(!skills.empty() && !runes.empty() && !statuses.empty() && !enemies.empty());
    // Every row is generated: a title and a stable id, no placeholder.
    for (const auto* tab : {&skills, &runes, &statuses, &enemies}) {
        for (const game::FCompendiumEntry& entry : *tab) {
            CHECK(!entry.id.empty());
            CHECK(!entry.title.empty());
        }
    }
}

void TabsMatchRegistry() {
    FContentFixture fixture;
    const FContentRegistry& content = fixture.content();

    const auto ids = [&fixture](game::ECompendiumTab tab) {
        std::vector<std::string> result;
        for (const game::FCompendiumEntry& entry : game::BuildCompendiumTab(fixture.registry, tab)) {
            result.push_back(entry.id);
        }
        return result;
    };
    const auto tableIds = [](const auto& table) {
        std::vector<std::string> result;
        for (const auto& row : table.rows) result.push_back(row.id);
        return result;
    };
    CHECK(ids(game::ECompendiumTab::Skills) == tableIds(content.skills));
    CHECK(ids(game::ECompendiumTab::Runes) == tableIds(content.runes));
    CHECK(ids(game::ECompendiumTab::Statuses) == tableIds(content.statuses));
    std::vector<std::string> enemyIds;
    for (const FUnitContent& unit : content.units.rows) {
        if (game::IsEnemyUnit(unit)) enemyIds.push_back(unit.id);
    }
    CHECK(ids(game::ECompendiumTab::Enemies) == enemyIds);

    // Four named tabs, no two the same.
    std::array<std::string, game::kCompendiumTabCount> names;
    for (int index = 0; index < game::kCompendiumTabCount; ++index) {
        names[static_cast<std::size_t>(index)] =
            game::CompendiumTabName(static_cast<game::ECompendiumTab>(index));
    }
    for (std::size_t index = 0; index < names.size(); ++index) {
        CHECK(!names[index].empty());
        for (std::size_t other = index + 1; other < names.size(); ++other) CHECK(names[index] != names[other]);
    }
}

void EnemyRolesAreDerived() {
    FContentFixture fixture;
    const FContentRegistry& content = fixture.content();
    const auto role = [&content](const char* id) {
        const FUnitContent* unit = FindUnit(content, id);
        CHECK(unit != nullptr);
        return game::DeriveUnitRole(content, *unit);
    };
    // The role is a pure function of the skill profile: support tag > zone shape > long range > melee.
    CHECK(role("enemy.guardian") == "Melee");   // cleave + strike, both range 1
    CHECK(role("enemy.imp") == "Melee");        // strike
    CHECK(role("enemy.imp_archer") == "Ranged");  // quick shot, range 4
    CHECK(role("enemy.imp_shaman") == "Zone");  // firebomb is a zone (beats its ranged bolt)
    // The healer is a playable mage-class unit, not an enemy; the derivation still sees its Heal tag.
    const FUnitContent* healer = FindUnit(content, "char.healer");
    CHECK(healer != nullptr);
    CHECK(!game::IsEnemyUnit(*healer));
    CHECK(game::DeriveUnitRole(content, *healer) == "Support");

    for (const FUnitContent& unit : content.units.rows) {
        if (!game::IsEnemyUnit(unit)) continue;
        const std::string unitRole = game::DeriveUnitRole(content, unit);
        CHECK(unitRole == "Melee" || unitRole == "Ranged" || unitRole == "Zone" || unitRole == "Support");
    }

    // The enemy tab shows role/skills/stats and no appearance depth (single pool = no information).
    for (const game::FCompendiumEntry& entry :
         game::BuildCompendiumTab(fixture.registry, game::ECompendiumTab::Enemies)) {
        bool hasRole = false;
        for (const auto& [label, value] : entry.facts) {
            CHECK(label != "Depth");
            CHECK(label != "Appears at depth");
            if (label == "Role") hasRole = true;
        }
        CHECK(hasRole);
        CHECK(entry.body.find("Skills:") == 0);
    }
}

// ---------------------------------------------------------------------------------------------
// Settings: text scale + onboarding flag (WP-24a IO contract)
// ---------------------------------------------------------------------------------------------

void TextScalePersists() {
    const auto dir = FreshDir("text-scale");
    FSavedEnv override(dir);
    entt::registry registry;
    game::LoadSettings(registry);
    CHECK(game::CurrentSettings(registry).textScale == game::kTextScaleNormal);

    game::SetTextScale(registry, game::kTextScaleLarge);
    CHECK(game::CurrentSettings(registry).textScale == game::kTextScaleLarge);

    entt::registry reloaded;
    game::LoadSettings(reloaded);
    CHECK(game::CurrentSettings(reloaded).textScale == game::kTextScaleLarge);
    // The file stores the snapped set member at two decimals, not the float's 1.149999976158142.
    {
        std::ifstream file(dir / game::kSettingsFileName);
        CHECK(file.good());
        const auto stored = nlohmann::json::parse(file);
        CHECK(stored.at("text_scale").get<double>() == 1.15);
    }

    // Cycle 1.0 -> 1.15 -> 1.25 -> 1.0, saving at every step.
    game::CycleTextScale(reloaded);
    CHECK(game::CurrentSettings(reloaded).textScale == game::kTextScaleLargest);
    game::CycleTextScale(reloaded);
    CHECK(game::CurrentSettings(reloaded).textScale == game::kTextScaleNormal);
    game::CycleTextScale(reloaded);
    CHECK(game::CurrentSettings(reloaded).textScale == game::kTextScaleLarge);

    // 1.15 does not survive JSON exactly; the stored value snaps to the set member.
    const auto file = dir / game::kSettingsFileName;
    std::string writeError;
    CHECK(game::WriteSaveFile(file, nlohmann::json{{"schema_version", 1}, {"text_scale", 1.15}}, writeError));
    entt::registry snapped;
    game::LoadSettings(snapped);
    CHECK(game::CurrentSettings(snapped).textScale == game::kTextScaleLarge);

    // Anything outside the set warns and keeps the default.
    CHECK(game::WriteSaveFile(file, nlohmann::json{{"schema_version", 1}, {"text_scale", 1.4}}, writeError));
    entt::registry refused;
    game::LoadSettings(refused);
    CHECK(game::CurrentSettings(refused).textScale == game::kTextScaleNormal);
}

void SettingsRoundTrip() {
    const auto dir = FreshDir("round-trip");
    {
        FSavedEnv override(dir);
        entt::registry registry;
        game::LoadSettings(registry);
        game::SetAnimSpeed(registry, game::kAnimSpeedFast);
        game::SetSkipAnimations(registry, true);
        game::SetTextScale(registry, game::kTextScaleLargest);
        game::SetOnboardingDone(registry, true);
    }
    {
        FSavedEnv override(dir);
        entt::registry reloaded;
        game::LoadSettings(reloaded);
        const FSettings& settings = game::CurrentSettings(reloaded);
        CHECK(settings.animSpeed == game::kAnimSpeedFast);
        CHECK(settings.skipAnimations);
        CHECK(settings.textScale == game::kTextScaleLargest);
        CHECK(settings.onboardingDone);
    }
    std::ifstream file(dir / game::kSettingsFileName);
    CHECK(file.good());
    const auto document = nlohmann::json::parse(file);
    CHECK(document.at("schema_version").get<int>() == game::kSettingsSchemaVersion);
    CHECK(document.at("anim_speed").get<float>() == game::kAnimSpeedFast);
    CHECK(document.at("skip_animations").get<bool>());
    CHECK(document.at("text_scale").get<float>() == game::kTextScaleLargest);
    CHECK(document.at("onboarding_done").get<bool>());
}

void UnknownKeysPreserved() {
    const auto dir = FreshDir("unknown-keys");
    const auto file = dir / game::kSettingsFileName;
    const nlohmann::json original{{"schema_version", 1}, {"anim_speed", 1.0}, {"skip_animations", false},
                                  {"text_scale", 1.0},   {"onboarding_done", false},
                                  {"future_key", {{"nested", 7}}}, {"another", "keep me"}};
    std::string writeError;
    CHECK(game::WriteSaveFile(file, original, writeError));

    FSavedEnv override(dir);
    entt::registry registry;
    game::LoadSettings(registry);
    game::SetTextScale(registry, game::kTextScaleLargest);
    game::SetOnboardingDone(registry, true);

    std::ifstream reloaded(file);
    CHECK(reloaded.good());
    const auto document = nlohmann::json::parse(reloaded);
    CHECK(document.at("future_key").at("nested").get<int>() == 7);
    CHECK(document.at("another").get<std::string>() == "keep me");
    CHECK(document.at("text_scale").get<float>() == game::kTextScaleLargest);
    CHECK(document.at("onboarding_done").get<bool>());
}

void SettingsUseIsolatedDir() {
    const auto dir = FreshDir("isolated-dir");
    FSavedEnv override(dir);
    entt::registry registry;
    game::LoadSettings(registry);
    game::SetTextScale(registry, game::kTextScaleLarge);
    const auto expected = dir / game::kSettingsFileName;
    CHECK(registry.ctx().get<game::FSettingsLocation>().path == std::optional(expected));
    CHECK(std::filesystem::exists(expected));
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        (void)entry;
        ++files;
    }
    CHECK(files == 1);
}

// ---------------------------------------------------------------------------------------------
// Text scale: layout fit and the gameplay separation
// ---------------------------------------------------------------------------------------------

void TextScaleKeepsZones() {
    FImGuiFixture imgui;
    ImGuiIO& io = ImGui::GetIO();
    // The bundled body font makes the measurement match the game (Alegreya metrics, not the built-in font).
    if (ImFont* body = io.Fonts->AddFontFromFileTTF("assets/fonts/Alegreya.ttf", 17.f)) {
        io.FontDefault = body;
    } else {
        std::cout << "  (bundled Alegreya.ttf not loadable in this headless context; using the built-in font)\n";
    }
    imgui.FinishFonts();

    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    const auto* assets = registry.ctx().find<game::FAssetManager>();
    CHECK(assets != nullptr);
    const auto digest = assets->digest("data/ui_layout");
    CHECK(digest != nullptr);
    const auto parsed = game::ui::ParseUiLayoutSpec(*digest);
    CHECK(parsed.has_value());
    const FUiLayoutSpec& spec = *parsed;

    game::ui::ApplyTextScale(game::kTextScaleLargest);
    CHECK(ImGui::GetStyle().FontScaleMain == game::kTextScaleLargest);
    ImGui::NewFrame();

    for (const auto& [width, height] : std::array<std::pair<float, float>, 2>{{{1280.f, 800.f}, {1920.f, 1080.f}}}) {
        const FUiLayout layout = game::ui::ComputeUiLayout(spec, width, height);
        const float u = layout.scale;

        // Width of one HUD line at its authored size, scaled by the layout and then by FontScaleMain.
        const auto lineWidth = [](const char* text, float size) {
            ImGui::PushFont(nullptr, size);
            const float measured = ImGui::CalcTextSize(text).x;
            ImGui::PopFont();
            return measured;
        };
        const auto expectWidth = [&](const FRect& zone, const char* text, float authoredSize, float reserve,
                                     const char* what) {
            const float measured = lineWidth(text, authoredSize * u);
            const float available = zone.w - reserve * u;
            if (measured > available) {
                std::cout << "  [" << static_cast<int>(width) << "x" << static_cast<int>(height) << "] " << what
                          << ": " << measured << " px > " << available << " px\n";
            }
            CHECK(measured <= available);
        };
        // The lead's fallback criterion: an authored single-line font at 1.25 must still fit the zone height.
        const auto expectHeight = [&](const FRect& zone, float authoredSize, const char* what) {
            const float scaled = authoredSize * u * game::kTextScaleLargest;
            if (scaled > zone.h + 1.5f) {
                std::cout << "  [" << static_cast<int>(width) << "x" << static_cast<int>(height) << "] " << what
                          << ": " << scaled << " px > zone height " << zone.h << " px\n";
            }
            CHECK(scaled <= zone.h + 1.5f);
        };

        // Height: the tallest authored font of each fixed HUD cluster.
        expectHeight(layout.zones.title, 15.f, "title");
        expectHeight(layout.zones.turnOrder, 15.f, "turn order");
        expectHeight(layout.zones.status, 13.f, "status");
        expectHeight(layout.zones.party, 18.f, "party");
        expectHeight(layout.zones.unit, 18.f, "unit");
        expectHeight(layout.zones.skills, 20.f, "skills");
        expectHeight(layout.zones.endTurn, 20.f, "end turn");
        expectHeight(layout.zones.journal, 15.f, "journal");
        expectHeight(layout.zones.hints, 22.f, "hints");
        expectHeight(layout.zones.targets, 15.f, "targets");

        // Width: the fixed strings the HUD draws inside a zone (incl. the skeleton's own turn-order assert).
        expectWidth(layout.zones.title, "EMBER DUNGEON", 15.f, 8.f, "title heading");
        expectWidth(layout.zones.status, "DEPTH 01  |  ROOM 1 / 3", 13.f, 8.f, "status depth");
        expectWidth(layout.zones.status, "MATERIALS 0  |  CHECKPOINT 0  |  NODES 0", 12.f, 8.f, "status resources");
        expectWidth(layout.zones.turnOrder, game::ecs::kNextRoundMarker, 15.f, 24.f, "turn marker");
        expectWidth(layout.zones.skills, "ROOM RESOLVED", 20.f, 0.f, "skills banner");
        expectWidth(layout.zones.unit, "Lumen", 15.f, 0.f, "unit name");
        expectWidth(layout.zones.hints, "?", 22.f, 0.f, "hint mark");
        expectWidth(layout.zones.targets, "Rain of Arrows", 15.f, 32.f, "target card name");
        expectWidth(layout.zones.targets, "Range 4  |  AP 2  |  spread", 15.f, 32.f, "target card stats");

        // The title's second row is a seed readout whose length follows the data (up to 10 seed digits). It
        // may overrun the 200px recess at 1.25, so the asserted invariant is the one that matters: it never
        // reaches the next zone (turn order starts at x=480 on the 1280 canvas).
        const float seedLine = lineWidth("Collapsed barracks  /  seed 4294967295", 12.f * u) + 8.f * u;
        CHECK(layout.zones.title.x + seedLine < layout.zones.turnOrder.x);

        // The onboarding strip is inside the arena and never covers a HUD zone, at both window shapes.
        CHECK(layout.arena.contains(layout.onboarding.strip));
        for (const FRect* zone : {&layout.zones.title, &layout.zones.turnOrder, &layout.zones.status,
                                  &layout.zones.party, &layout.zones.unit, &layout.zones.skills,
                                  &layout.zones.endTurn, &layout.zones.journal, &layout.zones.hints,
                                  &layout.zones.targets}) {
            CHECK(!layout.onboarding.strip.intersects(*zone));
        }
    }
    ImGui::EndFrame();
}

void OnboardingPlacementsClear() {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    const auto* assets = registry.ctx().find<game::FAssetManager>();
    CHECK(assets != nullptr);
    const auto digest = assets->digest("data/ui_layout");
    CHECK(digest != nullptr);
    const auto parsed = game::ui::ParseUiLayoutSpec(*digest);
    CHECK(parsed.has_value());
    const FUiLayoutSpec& spec = *parsed;

    for (const auto& [width, height] :
         std::array<std::pair<float, float>, 3>{{{1160.f, 800.f}, {1280.f, 800.f}, {1920.f, 1080.f}}}) {
        const FUiLayout layout = game::ui::ComputeUiLayout(spec, width, height);
        const auto& card = layout.onboarding;
        const auto& zones = layout.zones;

        // Both anchors stay on screen and inside the arena (the arena is clear of the HUD bands, so a card
        // anchored inside it can never cover a band).
        CHECK(layout.screen.contains(card.strip));
        CHECK(layout.arena.contains(card.strip));
        CHECK(layout.screen.contains(card.side));
        CHECK(layout.arena.contains(card.side));
        CHECK(card.strip.x >= layout.arena.x && card.strip.right() <= layout.arena.right());
        // The side card sits to the right of the centered run-phase modal column and never touches it.
        CHECK(card.side.x >= card.modalColumn.right());
        CHECK(!card.side.intersects(card.modalColumn));
        // ...and it is wide enough to hold a readable line.
        CHECK(card.side.w >= game::ui::kOnboardingSideMinWidth);

        // Neither anchor intersects any HUD zone (heights are the anchor's full available extent; the drawn
        // card is smaller, so any wrapped text is clear too).
        for (const FRect* zone : {&zones.title, &zones.turnOrder, &zones.status, &zones.party, &zones.unit,
                                  &zones.skills, &zones.endTurn, &zones.journal, &zones.hints, &zones.targets}) {
            CHECK(!card.strip.intersects(*zone));
            CHECK(!card.side.intersects(*zone));
        }
    }

    // The clearance band matches the centered, unscaled run-phase modals (580 screen px at every window size).
    const FUiLayout narrow = game::ui::ComputeUiLayout(spec, 1160.f, 800.f);
    CHECK(narrow.onboarding.modalColumn.w == 580.f);
    CHECK(narrow.onboarding.modalColumn.x == 290.f);
    const FUiLayout wide = game::ui::ComputeUiLayout(spec, 1920.f, 1080.f);
    CHECK(wide.onboarding.modalColumn.x == (1920.f - 580.f) * .5f);
}

void ScaleNeverTouchesSimulation() {
    // The presentation-only promise is structural: no gameplay translation unit reads the settings or the ImGui
    // font scale. (The behavioural half lives in settings_tests' AnimSpeedScalesPresentationOnly; a second
    // full seeded fight here would only repeat the same rules engine.)
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("src/gameplay")) {
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension();
        if (extension == ".h" || extension == ".cpp") files.push_back(entry.path());
    }
    CHECK(files.size() > 10);  // sanity: the scan found the gameplay tree
    for (const auto& file : files) {
        const std::string text = ReadText(file);
        CHECK(text.find("FSettings") == std::string::npos);
        CHECK(text.find("FontScaleMain") == std::string::npos);
        CHECK(text.find("textScale") == std::string::npos);
    }
}

// ---------------------------------------------------------------------------------------------
// Accessibility: shape marker + non-colour cue checklist
// ---------------------------------------------------------------------------------------------

void FactionMarkerDiffersByShape() {
    using game::ui::EFactionMarker;
    using game::ui::FactionMarkerFor;
    using game::ui::FactionMarkerOutline;
    CHECK(FactionMarkerFor(true) == EFactionMarker::Square);
    CHECK(FactionMarkerFor(false) == EFactionMarker::Triangle);

    const auto square = FactionMarkerOutline(FactionMarkerFor(true), 100.f, 50.f, 4.f, 4.f);
    const auto triangle = FactionMarkerOutline(FactionMarkerFor(false), 100.f, 50.f, 4.f, 4.f);
    CHECK(square.count == 4);
    CHECK(triangle.count == 3);

    // The badge shapes are screen-axis aligned (a grid diamond would read as a highlighted cell): the square
    // has two equal top corners and two equal left corners; the triangle has one apex and a flat base.
    CHECK(std::fabs(square.points[0].y - square.points[1].y) < .01f);
    CHECK(std::fabs(square.points[2].x - square.points[1].x) < .01f);
    CHECK(std::fabs(square.points[0].x - square.points[3].x) < .01f);
    CHECK(std::fabs(square.points[0].x - 96.f) < .01f && std::fabs(square.points[1].x - 104.f) < .01f);
    CHECK(std::fabs(square.points[0].y - 46.f) < .01f && std::fabs(square.points[2].y - 54.f) < .01f);
    CHECK(triangle.points[0].x == 100.f && std::fabs(triangle.points[0].y - 46.f) < .01f);  // apex on top
    CHECK(std::fabs(triangle.points[1].y - triangle.points[2].y) < .01f);                      // flat base
    CHECK(std::fabs(triangle.points[1].x - 104.f) < .01f && std::fabs(triangle.points[2].x - 96.f) < .01f);
    // The shapes differ by vertex count and by the top edge (square has two top points, triangle one apex).
    CHECK(square.count != triangle.count);
    CHECK(square.points[1].y == square.points[0].y);
    CHECK(triangle.points[2].y != triangle.points[0].y);
}

void InfoCueChecklistComplete() {
    const auto rows = game::ui::InfoCueChecklist();
    CHECK(rows.size() == 6);
    constexpr std::array<game::ui::EInfoCueStatus, 6> kExpected{
        game::ui::EInfoCueStatus::Covered,  // world faction badge above the head + colour ring
        game::ui::EInfoCueStatus::Covered,  // board ranges: move single outline / hit double outline (WP-12c)
        game::ui::EInfoCueStatus::Covered,  // HUD faction -> name and text
        game::ui::EInfoCueStatus::Covered,  // AP gems -> letters, counts, numbers
        game::ui::EInfoCueStatus::Covered,  // result banner/log -> text
        game::ui::EInfoCueStatus::Covered,  // status gems: one shape per kind (WP-12c)
    };
    for (std::size_t index = 0; index < rows.size(); ++index) {
        CHECK(!rows[index].id.empty());
        CHECK(!rows[index].what.empty());
        CHECK(!rows[index].cue.empty());
        CHECK(rows[index].status == kExpected[index]);
    }
    // Both former follow-up rows now name their non-colour channel, so neither can regress silently.
    CHECK(rows[1].id == "board.ranges");
    CHECK(rows[1].cue.find("double outline") != std::string_view::npos);
    CHECK(rows[5].id == "unit.statuses");
    CHECK(rows[5].cue.find("shape per kind") != std::string_view::npos);
}

// ---------------------------------------------------------------------------------------------
// Onboarding
// ---------------------------------------------------------------------------------------------

void OnboardingFiresOnce() {
    FOnboardingFixture fixture;
    const FOnboardingContent& content = fixture.content();
    CHECK(content.steps.size() == 3);
    CHECK(content.steps[0].at == game::EOnboardingTrigger::BattleFirst);
    CHECK(content.steps[1].at == game::EOnboardingTrigger::RewardFirst);
    CHECK(content.steps[2].at == game::EOnboardingTrigger::CampFirst);
    for (const FOnboardingStep& step : content.steps) {
        CHECK(!step.text.empty());
        // The body font has no Hangul glyphs: on-screen text must stay ASCII/English.
        CHECK(std::all_of(step.text.begin(), step.text.end(), [](unsigned char c) { return c < 0x80; }));
    }

    FOnboardingProgress progress;
    FOnboardingDecision decision = Step(content, progress, game::EOnboardingTrigger::BattleFirst);
    CHECK(decision.shownIndex == 0);
    progress = decision.progress;
    // The same trigger on the next frame keeps showing the same step (it fires once, not once per frame).
    decision = Step(content, progress, game::EOnboardingTrigger::BattleFirst);
    CHECK(decision.shownIndex == 0);
    progress = decision.progress;

    // Leaving room 0 for the reward screen moves past the battle step and shows the reward hint.
    decision = Step(content, progress, game::EOnboardingTrigger::RewardFirst);
    CHECK(decision.shownIndex == 1);
    progress = decision.progress;
    // Returning to the next battle must not replay the first hint.
    decision = Step(content, progress, std::nullopt);
    CHECK(decision.shownIndex == -1);
    progress = decision.progress;
    decision = Step(content, progress, game::EOnboardingTrigger::CampFirst);
    CHECK(decision.shownIndex == 2);
    progress = decision.progress;
    // Leaving the last step completes the walk exactly once.
    decision = Step(content, progress, std::nullopt);
    CHECK(decision.shownIndex == -1);
    CHECK(decision.completed);
    progress = decision.progress;
    decision = Step(content, progress, game::EOnboardingTrigger::BattleFirst);
    CHECK(decision.shownIndex == -1);
    CHECK(!decision.completed);

    // Unknown triggers and broken rows are dropped at load (a private root shadows the shipped document).
    const auto root = FreshDir("onboarding-validation");
    std::filesystem::create_directories(root / "data");
    {
        std::ofstream file(root / "data" / "onboarding.json");
        file << R"({"schema_version": 1, "steps": [
            {"at": "battle_first", "text": "Move hint"},
            {"at": "boss_first", "text": "unknown trigger"},
            {"at": "reward_first"},
            {"at": "camp_first", "text": ""},
            {"at": "reward_first", "text": "Take a rune or skip."}
        ]})";
        CHECK(file.good());
    }
    entt::registry shadowed;
    auto roots = ShipAssetRoots();
    roots.insert(roots.begin(), root.string());
    game::FAssetManager::Initialize(shadowed, std::move(roots));
    game::LoadOnboardingContent(shadowed);
    const FOnboardingContent* parsed = game::FindOnboardingContent(shadowed);
    CHECK(parsed != nullptr);
    CHECK(parsed->steps.size() == 2);
    CHECK(parsed->steps[0].at == game::EOnboardingTrigger::BattleFirst);
    CHECK(parsed->steps[1].at == game::EOnboardingTrigger::RewardFirst);
    CHECK(parsed->steps[1].text == "Take a rune or skip.");
}

void OnboardingStepsInOrder() {
    FOnboardingFixture fixture;
    const FOnboardingContent& content = fixture.content();
    FOnboardingProgress progress;

    FOnboardingDecision decision = Step(content, progress, game::EOnboardingTrigger::BattleFirst);
    CHECK(decision.shownIndex == 0);
    progress = decision.progress;
    decision = Step(content, progress, game::EOnboardingTrigger::RewardFirst);
    CHECK(decision.shownIndex == 1);
    progress = decision.progress;
    decision = Step(content, progress, game::EOnboardingTrigger::CampFirst);
    CHECK(decision.shownIndex == 2);

    // A deep continue that starts at camp skips the unreached steps (index rule), it does not block the walk.
    FOnboardingProgress deep;
    const FOnboardingDecision jumped = Step(content, deep, game::EOnboardingTrigger::CampFirst);
    CHECK(jumped.shownIndex == 2);
    const FOnboardingDecision left = Step(content, jumped.progress, std::nullopt);
    CHECK(left.completed);
}

void OnboardingReplayToggle() {
    const auto dir = FreshDir("onboarding-replay");
    FSavedEnv override(dir);
    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    game::LoadSettings(registry);
    game::SetOnboardingDone(registry, true);
    CHECK(game::CurrentSettings(registry).onboardingDone);
    // Pretend the walk already finished in this session: Replay must clear the in-session progress too.
    registry.ctx().insert_or_assign<FOnboardingProgress>(FOnboardingProgress{3, 2});
    game::ReplayOnboarding(registry);
    CHECK(!game::CurrentSettings(registry).onboardingDone);
    const FOnboardingProgress& reset = registry.ctx().get<FOnboardingProgress>();
    CHECK(reset.nextIndex == 0);
    CHECK(reset.shownIndex == -1);
    {
        std::ifstream file(dir / game::kSettingsFileName);
        CHECK(file.good());
        const auto document = nlohmann::json::parse(file);
        CHECK(!document.at("onboarding_done").get<bool>());
    }

    // On the next expedition the first step shows again.
    registry.ctx().emplace<game::FDungeonRun>();
    registry.ctx().emplace<game::FGridMetrics>();
    game::LoadOnboardingContent(registry);
    const FOnboardingDecision decision = Step(*game::FindOnboardingContent(registry),
                                              registry.ctx().get<FOnboardingProgress>(),
                                              game::EOnboardingTrigger::BattleFirst);
    CHECK(decision.shownIndex == 0);
    CHECK(!decision.completed);
}

void HintDoesNotBlockModal() {
    FImGuiFixture imgui;
    imgui.FinishFonts();

    FUiLayout layout;  // only the onboarding placement is used by DrawOnboardingHint
    layout.scale = 1.f;
    layout.onboarding.strip = {320.f, 360.f, 300.f, 200.f};  // deliberately overlaps the modal button
    layout.onboarding.padding = 8.f;

    ImGuiIO& io = ImGui::GetIO();
    bool clicked = false;
    bool buttonHovered = false;
    int hintVertices = 0;

    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::OpenPopup("Hint modal");
        ImGui::SetNextWindowPos({300.f, 300.f});
        ImGui::SetNextWindowSize({400.f, 300.f});
        if (ImGui::BeginPopupModal("Hint modal", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::SetCursorScreenPos({340.f, 380.f});
            clicked = ImGui::Button("Continue", {160.f, 40.f}) || clicked;
            buttonHovered = ImGui::IsItemHovered() || buttonHovered;
            ImGui::EndPopup();
        }
        // The hint is drawn after the modal, into the foreground draw list and directly over the button.
        const int before = ImGui::GetForegroundDrawList()->VtxBuffer.Size;
        game::DrawOnboardingHint(layout, "Move: 1 AP for up to 2 cells.", false);
        hintVertices = ImGui::GetForegroundDrawList()->VtxBuffer.Size - before;
        // A draw list creates no window, so nothing can even be hit-tested for the hint.
        CHECK(ImGui::FindWindowByName("Onboarding hint") == nullptr);
        ImGui::Render();
    };

    frame();  // create the popup
    frame();  // the popup is fully visible from this frame on
    io.AddMousePosEvent(420.f, 400.f);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    CHECK(buttonHovered);   // the hint never takes the hover from the modal button...
    CHECK(clicked);         // ...and the click lands on the button, not on the hint
    CHECK(hintVertices > 0);  // ...while the card really was drawn over it
}

void OverlayRunsEveryFrame() {
    const auto dir = FreshDir("overlay-frames");
    FSavedEnv override(dir);
    FImGuiFixture imgui;
    imgui.FinishFonts();

    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    game::LoadSettings(registry);
    CHECK(game::ui::InitializeUiFrame(registry));
    auto& window = registry.ctx().emplace<game::FEngineWindowState>();
    window.width = 1280;
    window.height = 800;
    registry.ctx().emplace<game::FDungeonUiState>();
    registry.ctx().emplace<game::FDungeonRun>();  // phase Battle, depth 1, room 0 -> battle_first
    registry.ctx().emplace<game::FGridMetrics>();

    game::Time time;
    game::SystemManager systems;
    const auto hintDrawn = [] { return ImGui::GetForegroundDrawList()->VtxBuffer.Size > 0; };
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        game::RenderDungeonOverlay(registry, time, systems);
        CHECK(hintDrawn());
        CHECK(ImGui::GetStyle().FontScaleMain == game::CurrentSettings(registry).textScale);
        ImGui::Render();
    }

    // The scale the settings carry reaches ImGui through the overlay (the one FontScaleMain assignment).
    registry.ctx().get<FSettings>().textScale = game::kTextScaleLargest;
    ImGui::NewFrame();
    game::RenderDungeonOverlay(registry, time, systems);
    CHECK(ImGui::GetStyle().FontScaleMain == game::kTextScaleLargest);
    CHECK(hintDrawn());
    ImGui::Render();

    // Without the battle grid (title/hub/preparation) the strip is never drawn, even with a run object in
    // the context. This is the "never on the title screen" guard.
    registry.ctx().erase<game::FGridMetrics>();
    ImGui::NewFrame();
    game::RenderDungeonOverlay(registry, time, systems);
    CHECK(!hintDrawn());
    ImGui::Render();

    // onboarding_done suppresses it even on an expedition screen.
    registry.ctx().emplace<game::FGridMetrics>();
    registry.ctx().get<FSettings>().onboardingDone = true;
    ImGui::NewFrame();
    game::RenderDungeonOverlay(registry, time, systems);
    CHECK(!hintDrawn());
    ImGui::Render();
}

// ---------------------------------------------------------------------------------------------
// Title wiring and modal close
// ---------------------------------------------------------------------------------------------

/// A booted registry with the title rows and views registered (the real composition root).
struct FTitleFixture {
    FImGuiFixture imgui;
    entt::registry registry;
    std::filesystem::path dir;

    explicit FTitleFixture(const char* name) : dir(FreshDir(name)) {
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, (dir / "checkpoint.json").string()));
        auto& window = registry.ctx().emplace<game::FEngineWindowState>();
        window.width = 1280;
        window.height = 800;
        imgui.FinishFonts();
        game::InitializeDungeonUi(registry);
    }
};

void CompendiumOpensFromTitle() {
    const auto dir = FreshDir("title-compendium");
    FSavedEnv override(dir);
    FTitleFixture fixture("menu");

    const auto& menu = fixture.registry.ctx().get<game::FTitleMenuRegistry>();
    bool found = false;
    for (const game::FTitleMenuItem& item : menu.items) {
        if (item.id != game::kCompendiumModalId) continue;
        found = true;
        CHECK(item.label == "COMPENDIUM");
        CHECK(item.visible);
        CHECK(item.transition.kind == EStateChangeKind::Push);
        CHECK(item.transition.targetId == game::kCompendiumModalId);
    }
    CHECK(found);
    CHECK(game::FindModalView(fixture.registry, game::kCompendiumModalId) != nullptr);

    // Render the real modal for three frames (the first two settle the popup's auto-fit) and capture its text
    // log: tabs, the selected entry and the close row all reach the render path.
    const std::string logPath = (fixture.dir / "compendium-ui.log").string();
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        if (frame == 2) ImGui::LogToFile(0, logPath.c_str());
        game::RenderCompendiumView(fixture.registry);
        if (frame == 2) ImGui::LogFinish();
        ImGui::Render();
    }
    CHECK(ImGui::FindWindowByName("Compendium") != nullptr);
    const std::string log = ReadText(logPath);
    CHECK(log.find("COMPENDIUM") != std::string::npos);
    CHECK(log.find("Skills (") != std::string::npos);  // the active tab label carries its row count
    CHECK(log.find("Runes (") != std::string::npos);
    CHECK(log.find("Statuses (") != std::string::npos);
    CHECK(log.find("Enemies (") != std::string::npos);
    CHECK(log.find("Heavy Strike") != std::string::npos);  // the first shipped skill row
    CHECK(log.find("Close") != std::string::npos);
}

void ModalPopsOnClose() {
    const auto dir = FreshDir("title-compendium-close");
    FSavedEnv override(dir);
    FTitleFixture fixture("menu");

    ImGuiIO& io = ImGui::GetIO();
    const auto draw = [&fixture] {
        ImGui::NewFrame();
        game::RenderCompendiumView(fixture.registry);
        ImGui::Render();
    };
    draw();  // the popup's first frame is hidden while it auto-fits
    draw();  // ...and the second frame finishes that settle
    ImGui::NewFrame();
    game::RenderCompendiumView(fixture.registry);
    // The Close button is the last row of the popup: full content width, 40 px tall, one padding above the
    // popup's bottom edge. The exact screen point is what a player would click.
    ImGuiWindow* popup = ImGui::FindWindowByName("Compendium");
    CHECK(popup != nullptr);
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 closeCenter{popup->Pos.x + popup->Size.x * .5f,
                             popup->Pos.y + popup->Size.y - style.WindowPadding.y - 20.f};
    ImGui::Render();

    io.AddMousePosEvent(closeCenter.x, closeCenter.y);
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    // The view asked its host to pop; the host (Menu mode) turns that request into a Pop transition.
    FEmberModalHostState host(std::string(game::kCompendiumModalId), EHostMode::Menu);
    const FStateTransition transition = host.Update(fixture.registry, 1.f / 60.f);
    CHECK(transition.kind == EStateChangeKind::Pop);
}

} // namespace

int main() {
    // Save safety: ctest exports no GAME01P_SAVE_DIR, so commit an isolated directory before any settings or
    // dungeon service can touch the user's real save/settings location.
    if (const char* existing = std::getenv(game::kSettingsSaveDirEnv); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "compendium_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s(game::kSettingsSaveDirEnv, value.c_str());
#else
        setenv(game::kSettingsSaveDirEnv, value.c_str(), 1);
#endif
    }
    return game_test::RunTests("compendium", {
        {"CompendiumEntriesComplete", CompendiumEntriesComplete},
        {"TabsMatchRegistry", TabsMatchRegistry},
        {"EnemyRolesAreDerived", EnemyRolesAreDerived},
        {"TextScalePersists", TextScalePersists},
        {"SettingsRoundTrip", SettingsRoundTrip},
        {"UnknownKeysPreserved", UnknownKeysPreserved},
        {"SettingsUseIsolatedDir", SettingsUseIsolatedDir},
        {"TextScaleKeepsZones", TextScaleKeepsZones},
        {"OnboardingPlacementsClear", OnboardingPlacementsClear},
        {"ScaleNeverTouchesSimulation", ScaleNeverTouchesSimulation},
        {"FactionMarkerDiffersByShape", FactionMarkerDiffersByShape},
        {"InfoCueChecklistComplete", InfoCueChecklistComplete},
        {"OnboardingFiresOnce", OnboardingFiresOnce},
        {"OnboardingStepsInOrder", OnboardingStepsInOrder},
        {"OnboardingReplayToggle", OnboardingReplayToggle},
        {"HintDoesNotBlockModal", HintDoesNotBlockModal},
        {"OverlayRunsEveryFrame", OverlayRunsEveryFrame},
        {"CompendiumOpensFromTitle", CompendiumOpensFromTitle},
        {"ModalPopsOnClose", ModalPopsOnClose},
    });
}
