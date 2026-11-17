// WP-09h F6: the Unlocks tab's offer detail (the "For" column + the hover tooltip body) — the seam
// FPreparationView.h declares for the view and for tests. The helper is pure content/rule reads (no
// ImGui), so this suite runs headless against the SHIPPED content: every skill/rune catalog row must name
// its authored owners, a roster row carries neither For text nor a tooltip body, and no tooltip line may
// leak a raw content id.
//
// Worlds: the shipped boot is {per-test override root, assets} — the real roster/unlock/skill tables (the
// frozen fixture is never spliced under the shipped tables; a mixed root cannot boot — preparation_tests
// header). The negative case controls EVERY layer it reads (units pools, roster rows, rune table) with
// test-authored overrides, so a later shipped-content edit cannot flip its outcome (WP-00 rule).
//
// Everything a case writes lives under out/ and is removed when the case ends; no test touches %APPDATA%.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunStart.h"
#include "gameplay/meta/FMetaProgress.h"
#include "presentation/FDungeonViews.h"
#include "presentation/FPreparationView.h"

namespace {

using nlohmann::json;

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

/// A per-test scratch directory under out/, removed when the case ends (pass or fail). Every file a test
/// writes — the asset overrides and both save slots — lives inside it.
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

/// Overrides written into <scratch>/data BEFORE the content loads (units.json, roster.json, runes.json).
using FDataOverrides = std::function<void(const std::filesystem::path& dataDir)>;

/// A booted registry for the offer detail: the temp override root first, then the shipped content (no
/// fixture — see the file header). The checkpoint and the meta save live inside the scratch root, so the
/// case is fully isolated.
struct FUnlockBoot {
    FScratchRoot scratch;   // declared first: destroyed last, after the registry released its files
    entt::registry registry;

    explicit FUnlockBoot(const char* stem, const FDataOverrides& overrides = {}) : scratch(stem) {
        if (overrides) overrides(scratch.root / "data");
        game::FAssetManager::Initialize(registry, {scratch.root.string(), kShipAssetRoot});
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, (scratch.root / "checkpoint.json").string()));
    }
    FUnlockBoot(const FUnlockBoot&) = delete;
    FUnlockBoot& operator=(const FUnlockBoot&) = delete;

    [[nodiscard]] bool InitMeta() {
        return game::InitializeMetaProgress(registry, (scratch.root / "meta_save.json").string());
    }
};

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

/// "A, B" split the way the helper joins ("A" -> {"A"}; "" -> {}).
std::vector<std::string> SplitByComma(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t comma = text.find(", ", start);
        if (comma == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, comma - start));
        start = comma + 2;
    }
    return parts;
}

const game::FRosterChoice* FindRosterChoice(const std::vector<game::FRosterChoice>& choices,
                                            const std::string& name) {
    for (const game::FRosterChoice& choice : choices) {
        if (choice.name == name) return &choice;
    }
    return nullptr;
}

/// The reverse check the rune "For" column promises: the member's AUTHORED pool carries a skill for which
/// `runeId` passes ValidateSkillSlot (the same rule call the helper makes).
bool PoolFitsRune(const game::gameplay::FContentRegistry& content, const std::string& unitId,
                  const std::string& runeId) {
    const game::gameplay::FUnitContent* unit = content.units.find(unitId);
    if (unit == nullptr) return false;
    for (const std::string& skillId : unit->skillIds) {
        if (!game::ValidateSkillSlot(game::FSkillSlot{skillId, {runeId}}, content).has_value()) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// F6: tooltips and the "For" column (shipped world)
// ---------------------------------------------------------------------------------------------

void OfferTooltipsCarryNoRawIds() {
    FUnlockBoot boot("unlock-view-rawid");
    CHECK(boot.InitMeta());

    const std::vector<game::FUnlockOffer> offers = game::BuildUnlockOffers(boot.registry);
    CHECK(!offers.empty());
    int skillRows = 0;
    int runeRows = 0;
    int rosterRows = 0;
    for (const game::FUnlockOffer& offer : offers) {
        CHECK(!offer.row.id.empty());
        const game::FUnlockOfferDetail detail = game::UnlockOfferDetail(boot.registry, offer.row);
        if (offer.row.kind == "skill") {
            ++skillRows;
            CHECK(!detail.tooltipLines.empty());  // the raw id is never the only tooltip content
        }
        if (offer.row.kind == "rune") {
            ++runeRows;
            CHECK(!detail.tooltipLines.empty());
        }
        if (offer.row.kind == "roster") {
            ++rosterRows;
            // The recruit's own name is the Name column; a roster row serves nobody, so "For" stays empty
            // and there is no tooltip body (review round 1).
            CHECK(detail.forText.empty());
            CHECK(detail.tooltipLines.empty());
        }
        for (const std::string& line : detail.tooltipLines) {
            CHECK(line.find(offer.row.id) == std::string::npos);
        }
    }
    // The shipped catalog carries all three kinds; any zero would make the per-kind sweep vacuous.
    CHECK(skillRows > 0);
    CHECK(runeRows > 0);
    CHECK(rosterRows > 0);
}

void RuneTooltipStartsFromTheSummary() {
    FUnlockBoot boot("unlock-view-summary");
    CHECK(boot.InitMeta());
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();

    int runeRows = 0;
    for (const game::FMetaUnlockRow& row : game::MetaUnlockCatalog(boot.registry)) {
        if (row.kind != "rune") continue;
        ++runeRows;
        const game::gameplay::FRuneContent* rune = content.runes.find(row.id);
        CHECK(rune != nullptr);
        const game::FUnlockOfferDetail detail = game::UnlockOfferDetail(boot.registry, row);
        CHECK(!detail.tooltipLines.empty());
        // Exactly one line: the summary is the single source for the effect text AND the requirement rules
        // (review round 1 — no separate requires_*_tags line) — byte-equal to the reward card's summary.
        CHECK(detail.tooltipLines.size() == 1);
        CHECK(detail.tooltipLines[0] == game::RuneEffectSummary(*rune, &content));
    }
    CHECK(runeRows > 0);
}

void RuneForColumnNamesMembersThatCanEquipIt() {
    FUnlockBoot boot("unlock-view-for");
    CHECK(boot.InitMeta());
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const std::vector<game::FRosterChoice> roster = game::BuildRosterChoices(boot.registry);

    int runeRows = 0;
    for (const game::FMetaUnlockRow& row : game::MetaUnlockCatalog(boot.registry)) {
        if (row.kind != "rune") continue;
        ++runeRows;
        const game::FUnlockOfferDetail detail = game::UnlockOfferDetail(boot.registry, row);
        // F6: the rune's For column must not be blank — a rune nobody can judge is the defect.
        CHECK(!detail.forText.empty());
        for (const std::string& name : SplitByComma(detail.forText)) {
            // Reverse check: every name printed must REALLY fit the rune — its unit's authored pool
            // carries a skill ValidateSkillSlot accepts with this rune attached.
            const game::FRosterChoice* choice = FindRosterChoice(roster, name);
            CHECK(choice != nullptr);
            CHECK(PoolFitsRune(content, choice->unitId, row.id));
        }
    }
    CHECK(runeRows > 0);
}

void RuneForColumnEmptiesWhenNoPoolSkillFits() {
    // The negative control, fully test-authored: every non-locked member's pool is exactly skill.strike,
    // the roster is exactly the four shipped members, and two runes are authored — one the pool fits
    // (positive control) and one whose requirement tag no pool skill carries. The two tags are DERIVED
    // from the shipped table (strike's own first tag; a vocabulary tag strike lacks), so a content edit
    // that reaches this case fails loudly at the derivation CHECK instead of flipping the outcome.
    const json shipSkills = ReadJsonFile(std::filesystem::path(kShipAssetRoot) / "data" / "skills.json");
    std::vector<std::string> strikeTags;
    for (const json& row : shipSkills.at("skills")) {
        const auto tags = row.find("tags");
        if (tags == row.end() || !tags->is_array()) continue;  // tags is an optional field
        if (row.at("id").get<std::string>() == "skill.strike") {
            strikeTags = tags->get<std::vector<std::string>>();
        }
    }
    CHECK(!strikeTags.empty());
    std::string otherTag;
    for (const json& row : shipSkills.at("skills")) {
        const auto tags = row.find("tags");
        if (tags == row.end() || !tags->is_array()) continue;
        for (const std::string& tag : tags->get<std::vector<std::string>>()) {
            if (std::find(strikeTags.begin(), strikeTags.end(), tag) == strikeTags.end()) {
                otherTag = tag;
                break;
            }
        }
        if (!otherTag.empty()) break;
    }
    CHECK(!otherTag.empty());  // a vocabulary tag skill.strike lacks — the loader accepts it

    FUnlockBoot boot("unlock-view-nofit", [&](const std::filesystem::path& data) {
        // units.json: the four party units' pools shrink to skill.strike.
        json units = ReadJsonFile(std::filesystem::path(kShipAssetRoot) / "data" / "units.json");
        for (json& row : units.at("units")) {
            const std::string id = row.at("id").get<std::string>();
            if (id == "char.warrior" || id == "char.archer" || id == "char.mage" || id == "char.healer") {
                row["skills"] = json::array({"skill.strike"});
            }
        }
        WriteJsonFile(data / "units.json", units);

        // roster.json: exactly the four shipped members — no row a later content edit could add.
        WriteTextFile(data / "roster.json", R"({"schema_version": 1, "roster": [
            {"id": "party.warrior", "name": "Alden", "enabled": true},
            {"id": "party.archer", "name": "Rowan", "enabled": true},
            {"id": "party.mage", "name": "Mira", "enabled": true},
            {"id": "party.healer", "name": "Lumen", "enabled": true}
        ]})");

        // runes.json: the shipped table plus the two control runes.
        json runes = ReadJsonFile(std::filesystem::path(kShipAssetRoot) / "data" / "runes.json");
        runes["runes"].push_back(json{{"id", "rune.test_fit"},
                                      {"display_name", "Test Fit Rune"},
                                      {"power_delta", 1},
                                      {"requires_all_tags", json::array({strikeTags[0]})}});
        runes["runes"].push_back(json{{"id", "rune.test_nofit"},
                                      {"display_name", "Test No-Fit Rune"},
                                      {"power_delta", 1},
                                      {"requires_all_tags", json::array({otherTag})}});
        WriteJsonFile(data / "runes.json", runes);
    });

    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    // The overrides really took effect — a missed override would make the negative assertion vacuous.
    const game::gameplay::FUnitContent* warrior = content.units.find("char.warrior");
    CHECK(warrior != nullptr);
    CHECK(warrior->skillIds.size() == 1);
    CHECK(warrior->skillIds[0] == "skill.strike");
    CHECK(content.runes.find("rune.test_fit") != nullptr);
    CHECK(content.runes.find("rune.test_nofit") != nullptr);

    // Positive control first: if the fitting rune names nobody, the pool/report path is broken and the
    // negative below would have passed for the wrong reason.
    const game::FUnlockOfferDetail fit =
        game::UnlockOfferDetail(boot.registry, game::FMetaUnlockRow{.id = "rune.test_fit", .kind = "rune"});
    CHECK(!fit.forText.empty());
    const game::FUnlockOfferDetail noFit =
        game::UnlockOfferDetail(boot.registry, game::FMetaUnlockRow{.id = "rune.test_nofit", .kind = "rune"});
    CHECK(noFit.forText.empty());
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
            (std::filesystem::current_path() / "out" / "test-saves" / "unlock_view_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("unlock_view", {
        {"OfferTooltipsCarryNoRawIds", OfferTooltipsCarryNoRawIds},
        {"RuneTooltipStartsFromTheSummary", RuneTooltipStartsFromTheSummary},
        {"RuneForColumnNamesMembersThatCanEquipIt", RuneForColumnNamesMembersThatCanEquipIt},
        {"RuneForColumnEmptiesWhenNoPoolSkillFits", RuneForColumnEmptiesWhenNoPoolSkillFits},
    });
}
