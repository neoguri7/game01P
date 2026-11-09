// WP-19b: the run debrief — the decisive-scene builder (three rules, merge, ties, grounded facts), the
// next-run deltas (meta + checkpoint), the cycle/friction lines, and the pinned WP-19a summary text.
//
// The pure builders run on synthetic logs (no boot). Every file-writing test uses a per-test UniqueDir
// under out/test-saves plus the committed GAME01P_SAVE_DIR fallback in main(): a plain `ctest` exports no
// override, and neither the run save nor the meta save may ever reach the real %APPDATA% directory.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "gameplay/combat/FIntent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FEncounterContent.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/meta/FMetaProgress.h"
#include "gameplay/meta/FMetaSave.h"
#include "states/FEmberRunEndState.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

// --- scratch directories -----------------------------------------------------------------------

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

/// A per-test scratch directory under out/test-saves. Every run save / meta save / run log this suite
/// writes lands inside one, so a ctest run never touches the real save directory (or the env override).
std::string UniqueDir(const char* stem) {
    const std::filesystem::path directory = std::filesystem::path("out") / "test-saves" / UniqueStem(stem);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory.string();
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    CHECK(file.good());
}

// --- a booted registry with its own save slot, run log and meta slot ----------------------------

struct FBoot {
    std::string directory;
    std::string checkpoint;
    std::string meta;
    entt::registry registry;
    std::string runSave;

    explicit FBoot(std::string dir) : directory(std::move(dir)) {
        std::filesystem::create_directories(directory);
        checkpoint = directory + "/checkpoint.json";
        meta = directory + "/meta_save.json";
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
    }

    FBoot(const FBoot&) = delete;
    FBoot& operator=(const FBoot&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
    const game::FRunLog& log() { return registry.ctx().get<game::FRunLog>(); }
};

// --- JSON / text helpers -----------------------------------------------------------------------

std::vector<json> EventsOfType(const game::FRunLog& log, std::string_view type) {
    std::vector<json> lines;
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        const auto found = event.find("t");
        if (found != event.end() && found->is_string() && found->get<std::string>() == type) {
            lines.push_back(event);
        }
    }
    return lines;
}

std::string Joined(const std::vector<std::string>& lines) {
    std::string joined;
    for (const auto& line : lines) joined += line + "\n";
    return joined;
}

/// True when `lines` holds exactly `text` as one element (the builders' membership contract).
bool HasLine(const std::vector<std::string>& lines, std::string_view text) {
    return std::ranges::any_of(lines, [text](const std::string& line) { return std::string_view(line) == text; });
}

// --- scene fact helpers ------------------------------------------------------------------------

std::size_t FactCount(const game::FRunScene& scene, std::string_view label) {
    std::size_t count = 0;
    for (const auto& fact : scene.facts) {
        if (std::string_view(fact.label) == label) ++count;
    }
    return count;
}

bool HasFactLabel(const game::FRunScene& scene, std::string_view label) { return FactCount(scene, label) > 0; }

bool HasFact(const game::FRunScene& scene, std::string_view label, std::string_view value) {
    for (const auto& fact : scene.facts) {
        if (std::string_view(fact.label) == label) return std::string_view(fact.value) == value;
    }
    return false;
}

bool HasRule(const game::FRunScene& scene, game::ERunSceneRule rule) {
    return std::ranges::find(scene.rules, rule) != scene.rules.end();
}

const game::FRunScene* SceneWithRule(const std::vector<game::FRunScene>& scenes, game::ERunSceneRule rule) {
    for (const auto& scene : scenes) {
        if (HasRule(scene, rule)) return &scene;
    }
    return nullptr;
}

/// A stable text form of scenes, for purity comparisons.
std::string SceneText(const std::vector<game::FRunScene>& scenes) {
    std::string text;
    for (const auto& scene : scenes) {
        text += scene.title + "|" + std::to_string(scene.depth) + "|" + std::to_string(scene.room) + "|";
        for (const auto rule : scene.rules) text += std::to_string(static_cast<int>(rule)) + ",";
        text += "|";
        for (const auto& fact : scene.facts) text += fact.label + "=" + fact.value + ";";
        text += "\n";
    }
    return text;
}

// --- number grounding --------------------------------------------------------------------------

void CollectNumbers(const json& value, std::set<long long>& numbers) {
    if (value.is_number()) {
        const double asNumber = value.get<double>();
        if (std::floor(asNumber) == asNumber) numbers.insert(static_cast<long long>(asNumber));
    } else if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) CollectNumbers(it.value(), numbers);
    } else if (value.is_array()) {
        for (const auto& entry : value) CollectNumbers(entry, numbers);
    }
}

// ---------------------------------------------------------------------------------------------
// Scene rules
// ---------------------------------------------------------------------------------------------

void SceneRule1IsLargestLossRoom() {
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 3},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 5}}})},
             {"skills_used", {{"skill.strike", 1}}}, {"ko", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "win"}, {"rounds", 6},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 12}},
                                      {{"id", "party.archer"}, {"loss", 3}},
                                      {{"id", "party.healer"}, {"loss", 0}}})},
             {"skills_used", {{"skill.maul", 2}}}, {"encounter", "encounter.ember_pack"},
             {"family", "beasts"}, {"elite", true}, {"ko", false}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 1}, {"picked", "rune.fire"}, {"skipped", false}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 1}, {"picked", ""}, {"skipped", true}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 2}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.mage"}, {"loss", 6}}})}, {"skills_used", {}},
             {"ko", false}},
    };
    const auto scenes = game::BuildRunScenes(log);
    CHECK(scenes.size() == 1u);
    const game::FRunScene& scene = scenes.front();
    CHECK(scene.depth == 1);
    CHECK(scene.room == 1);  // the largest sum (12 + 3 = 15) — not the first room
    CHECK(scene.rules == std::vector<game::ERunSceneRule>{game::ERunSceneRule::LargestLoss});
    CHECK(scene.title == "Heaviest losses");
    CHECK(HasFact(scene, "HP lost", "15"));  // the sum, never one unit's loss
    CHECK(HasFact(scene, "HP lost (party.warrior)", "12"));
    CHECK(HasFact(scene, "HP lost (party.archer)", "3"));
    CHECK(!HasFactLabel(scene, "HP lost (party.healer)"));  // loss 0 emits no per-unit fact
    CHECK(HasFact(scene, "Room", "Depth 1 room 1"));
    CHECK(HasFact(scene, "Result", "win"));
    CHECK(HasFact(scene, "Rounds", "6"));
    CHECK(HasFact(scene, "Skills used (all units)", "skill.maul x2"));
    CHECK(HasFact(scene, "Offer", "rune.fire"));              // the FIRST offer of the room
    CHECK(FactCount(scene, "Offer") == 1);                    // a later skipped offer does not join or overwrite
    CHECK(HasFact(scene, "Encounter", "encounter.ember_pack"));  // the optional room_end keys as facts
    CHECK(HasFact(scene, "Family", "beasts"));
    CHECK(HasFact(scene, "Elite", "yes"));
    CHECK(!HasFactLabel(scene, "Knockout"));  // ko false: no knockout fact
}

void SceneRule2FirstKoRoom() {
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 20}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "win"}, {"rounds", 3},
             {"hp_loss", json::array({{{"id", "party.healer"}, {"loss", 3}}})}, {"skills_used", {}},
             {"ko", true}},
        json{{"t", "room_end"}, {"depth", 2}, {"room", 0}, {"result", "defeat"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.mage"}, {"loss", 5}}})}, {"skills_used", {}},
             {"ko", true}},
    };
    const auto scenes = game::BuildRunScenes(log);
    CHECK(scenes.size() == 2u);  // rule 1 (room 0) + rule 2 (room 1); the later KO is nobody's rule
    CHECK(scenes[0].depth == 1 && scenes[0].room == 0);
    CHECK(scenes[1].depth == 1 && scenes[1].room == 1);  // chronological log order
    CHECK(scenes[1].rules == std::vector<game::ERunSceneRule>{game::ERunSceneRule::FirstKo});
    CHECK(scenes[1].title == "First knockout");
    CHECK(HasFact(scenes[1], "Knockout", "yes"));
    CHECK(!HasFactLabel(scenes[0], "Knockout"));  // the heaviest-loss scene is not a KO room
    for (const auto& scene : scenes) {
        CHECK(!(scene.depth == 2 && scene.room == 0));  // the second KO room never becomes a scene
    }
}

void Scene3UsesFatalIntentFacts() {
    // The heaviest room is a win, so rule 1 does not swallow the defeat room: rule 3 gets its own scene.
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.healer"}, {"loss", 40}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "room_end"}, {"depth", 2}, {"room", 1}, {"result", "defeat"}, {"rounds", 9},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 30}}})},
             {"skills_used", {{"skill.maul", 2}}}, {"ko", true}},
        json{{"t", "run_end"}, {"result", "defeat"}, {"reason", "wipe"}, {"depth", 2},
             {"fatal_intent", {{"skill", "skill.maul"},
                               {"declared_at_commit", 8},
                               {"expected_at_execution", 6},
                               {"actual_damage", 11},
                               {"hit", true}}}},
    };
    const auto scenes = game::BuildRunScenes(log);
    CHECK(scenes.size() == 2u);
    const game::FRunScene* fatal = SceneWithRule(scenes, game::ERunSceneRule::FatalIntent);
    CHECK(fatal != nullptr);
    CHECK(fatal->depth == 2 && fatal->room == 1);
    CHECK(HasFact(*fatal, "Fatal intent", "skill.maul"));
    CHECK(HasFact(*fatal, "Declared at commit", "8"));
    CHECK(HasFact(*fatal, "Expected at execution", "6"));
    CHECK(HasFact(*fatal, "Actual damage", "11"));
    CHECK(HasFact(*fatal, "Outcome", "hit"));
    CHECK(HasFact(*fatal, "Preview change", "8 -> 6"));  // the one logged number pair, honestly labeled
    CHECK(HasFact(*fatal, "Result", "defeat"));          // the room_end facts ride along
    CHECK(BuildRunEndSummary(log).fatalInScene);  // rule 3 fired here → the screen hides its own fatal line

    // Equal preview numbers: nothing to compare, so no `Preview change` fact (never "7 -> 7").
    game::FRunLog equal = log;
    equal.events.back()["fatal_intent"]["declared_at_commit"] = 7;
    equal.events.back()["fatal_intent"]["expected_at_execution"] = 7;
    equal.events.back()["fatal_intent"]["actual_damage"] = 4;
    equal.events.back()["fatal_intent"]["hit"] = false;
    const auto equalScenes = game::BuildRunScenes(equal);  // named local: the pointer points INTO it
    const game::FRunScene* equalFatal = SceneWithRule(equalScenes, game::ERunSceneRule::FatalIntent);
    CHECK(equalFatal != nullptr);
    CHECK(!HasFactLabel(*equalFatal, "Preview change"));
    CHECK(HasFact(*equalFatal, "Outcome", "missed"));
    CHECK(HasFact(*equalFatal, "Actual damage", "4"));

    // Without a defeat room_end there is no rule-3 scene at all, even with a fatal run_end.
    game::FRunLog noDefeat;
    noDefeat.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 9}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "run_end"}, {"result", "defeat"}, {"depth", 1},
             {"fatal_intent", {{"skill", "skill.maul"},
                               {"declared_at_commit", 8},
                               {"expected_at_execution", 6},
                               {"actual_damage", 11},
                               {"hit", true}}}},
    };
    for (const auto& scene : game::BuildRunScenes(noDefeat)) {
        CHECK(!HasRule(scene, game::ERunSceneRule::FatalIntent));
    }
    CHECK(!BuildRunEndSummary(noDefeat).fatalInScene);  // fatal run_end, no defeat room_end → no rule-3 scene
    // ...and with no room_end at all the fatal facts have no room to attach to.
    game::FRunLog noRooms;
    noRooms.events = {
        json{{"t", "run_end"}, {"result", "defeat"}, {"depth", 1},
             {"fatal_intent", {{"skill", "skill.maul"},
                               {"declared_at_commit", 8},
                               {"expected_at_execution", 6},
                               {"actual_damage", 11},
                               {"hit", true}}}},
    };
    CHECK(game::BuildRunScenes(noRooms).empty());
    CHECK(!BuildRunEndSummary(game::FRunLog{}).fatalInScene);  // an empty log carries no fatal scene
}

void ScenesDeduplicatedByRoom() {
    // Rule 1, rule 2 and rule 3 all land on depth 1 room 1: one merged scene, rules ascending, one title.
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.healer"}, {"loss", 2}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "defeat"}, {"rounds", 8},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 9}},
                                      {{"id", "party.archer"}, {"loss", 8}}})},
             {"skills_used", {{"skill.maul", 1}}}, {"ko", true}},
        json{{"t", "run_end"}, {"result", "defeat"}, {"depth", 1},
             {"fatal_intent", {{"skill", "skill.maul"},
                               {"declared_at_commit", 8},
                               {"expected_at_execution", 6},
                               {"actual_damage", 11},
                               {"hit", true}}}},
    };
    const auto merged = game::BuildRunScenes(log);
    CHECK(merged.size() == 1u);
    const game::FRunScene& scene = merged.front();
    CHECK(scene.depth == 1 && scene.room == 1);
    CHECK(scene.rules == (std::vector<game::ERunSceneRule>{game::ERunSceneRule::LargestLoss,
                                                          game::ERunSceneRule::FirstKo,
                                                          game::ERunSceneRule::FatalIntent}));
    CHECK(scene.title == "Heaviest losses / First knockout / The final blow");
    CHECK(HasFact(scene, "HP lost", "17"));
    CHECK(HasFact(scene, "HP lost (party.warrior)", "9"));
    CHECK(HasFact(scene, "HP lost (party.archer)", "8"));
    CHECK(HasFact(scene, "Result", "defeat"));           // rules 1–3 share one room_end, one fact
    CHECK(HasFact(scene, "Fatal intent", "skill.maul")); // and the rule-3 facts join the union
    CHECK(FactCount(scene, "Knockout") == 1);            // merged once, never twice
    CHECK(FactCount(scene, "HP lost") == 1);
    CHECK(FactCount(scene, "Result") == 1);
    // No (label, value) pair repeats anywhere in the merged facts.
    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto& fact : scene.facts) {
        CHECK(pairs.insert(std::make_pair(fact.label, fact.value)).second);
    }

    // Three different rooms → three scenes, in chronological (log) order.
    game::FRunLog spread;
    spread.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 6},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 20}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "win"}, {"rounds", 2},
             {"hp_loss", json::array({{{"id", "party.healer"}, {"loss", 3}}})}, {"skills_used", {}},
             {"ko", true}},
        json{{"t", "room_end"}, {"depth", 2}, {"room", 0}, {"result", "defeat"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.mage"}, {"loss", 5}}})}, {"skills_used", {}},
             {"ko", true}},
        json{{"t", "run_end"}, {"result", "defeat"}, {"depth", 2},
             {"fatal_intent", {{"skill", "skill.bolt"},
                               {"declared_at_commit", 5},
                               {"expected_at_execution", 2},
                               {"actual_damage", 7},
                               {"hit", true}}}},
    };
    const auto scenes = game::BuildRunScenes(spread);
    CHECK(scenes.size() == 3u);
    CHECK(scenes.size() <= game::kRunSceneLimit);  // after merging, never above the cap
    CHECK(scenes[0].depth == 1 && scenes[0].room == 0);
    CHECK(scenes[1].depth == 1 && scenes[1].room == 1);
    CHECK(scenes[2].depth == 2 && scenes[2].room == 0);
    CHECK(HasRule(scenes[0], game::ERunSceneRule::LargestLoss));
    CHECK(HasRule(scenes[1], game::ERunSceneRule::FirstKo));
    CHECK(HasRule(scenes[2], game::ERunSceneRule::FatalIntent));
    CHECK(scenes[2].title == "The final blow");
}

void TieBreaksEarliestRoom() {
    // Three equal hp_loss sums (10, 6 + 4, 10): the strict scan keeps the earliest room.
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 10}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.archer"}, {"loss", 6}},
                                      {{"id", "party.healer"}, {"loss", 4}}})},
             {"skills_used", {}}, {"ko", false}},
        json{{"t", "room_end"}, {"depth", 2}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.mage"}, {"loss", 10}}})}, {"skills_used", {}},
             {"ko", false}},
    };
    const auto scenes = game::BuildRunScenes(log);
    CHECK(scenes.size() == 1u);
    CHECK(scenes.front().depth == 1);
    CHECK(scenes.front().room == 0);
    CHECK(scenes.front().rules == std::vector<game::ERunSceneRule>{game::ERunSceneRule::LargestLoss});
    CHECK(HasFact(scenes.front(), "HP lost", "10"));
    CHECK(HasFact(scenes.front(), "HP lost (party.warrior)", "10"));  // the earliest room's own fact
}

void ScenesAreGrounded() {
    // Every integer a fact prints must occur as a number somewhere in the log's room_end/offer/run_end
    // lines: the debrief never invents a number the log cannot justify.
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.healer"}, {"loss", 30}}})},
             {"skills_used", {{"skill.zap", 3}}}, {"ko", false}},
        json{{"t", "room_end"}, {"depth", 2}, {"room", 1}, {"result", "defeat"}, {"rounds", 8},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 9}}})},
             {"skills_used", {{"skill.maul", 1}}}, {"ko", true}},
        // The offer belongs to (2,1)'s window (directly after its room_end): the position-window rule,
        // not the bare key, decides which room an offer line decorates.
        json{{"t", "offer"}, {"depth", 2}, {"room", 1}, {"picked", "rune.fire"}, {"skipped", false}},
        json{{"t", "run_end"}, {"result", "defeat"}, {"depth", 2},
             {"fatal_intent", {{"skill", "skill.maul"},
                               {"declared_at_commit", 8},
                               {"expected_at_execution", 6},
                               {"actual_damage", 11},
                               {"hit", true}}}},
    };
    const auto scenes = game::BuildRunScenes(log);
    CHECK(scenes.size() == 2u);  // rule 1 (depth 1 room 0) and the merged rule 2+3 (depth 2 room 1)
    CHECK(scenes[1].depth == 2 && scenes[1].room == 1);
    CHECK(HasFact(scenes[1], "Offer", "rune.fire"));  // (2,1)'s window holds its offer line

    std::set<long long> logged;
    for (const auto& event : log.events) {
        const auto found = event.find("t");
        if (found == event.end() || !found->is_string()) continue;
        const std::string& type = found->get<std::string>();
        if (type != "room_end" && type != "offer" && type != "run_end") continue;
        CollectNumbers(event, logged);
    }
    CHECK(!logged.empty());

    const std::regex digits("[0-9]+");
    for (const auto& scene : scenes) {
        for (const auto& fact : scene.facts) {
            for (auto it = std::sregex_iterator(fact.value.begin(), fact.value.end(), digits);
                 it != std::sregex_iterator(); ++it) {
                const long long number = std::stoll(it->str());
                const bool grounded = logged.contains(number);
                if (!grounded) {
                    std::cout << "[report] ungrounded number " << number << " in fact '" << fact.label
                              << ": " << fact.value << "'\n";
                }
                CHECK(grounded);
            }
        }
    }
}

void NoCounterfactualWithoutNumbers() {
    // A full defeat log: all three rules fire and the run-end text carries every section.
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.healer"}, {"loss", 12}}})},
             {"skills_used", {{"skill.zap", 1}}}, {"ko", false}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 0}, {"picked", "rune.fire"}, {"skipped", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "defeat"}, {"rounds", 9},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 30}}})},
             {"skills_used", {{"skill.maul", 2}}}, {"ko", true}},
        json{{"t", "run_end"}, {"result", "defeat"}, {"reason", "wipe"}, {"depth", 1},
             {"ms_to_first_battle", 1500}, {"cycle", 2},
             {"fatal_intent", {{"skill", "skill.maul"},
                               {"declared_at_commit", 8},
                               {"expected_at_execution", 6},
                               {"actual_damage", 11},
                               {"hit", true}}}},
    };
    std::string text = Joined(BuildRunEndSummaryLines(log));
    for (const auto& scene : game::BuildRunScenes(log)) {
        text += Joined(RunSceneLines(scene));
    }
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    for (const char* token : {"would", "could", "should", "if only", "had you"}) {
        CHECK(text.find(token) == std::string::npos);
    }
}

void SceneOmittedWhenFewerFacts() {
    // Zero loss, no KO, no run_end: no rule can fire.
    game::FRunLog quiet;
    quiet.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 0}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array()}, {"skills_used", {}}, {"ko", false}},
    };
    CHECK(game::BuildRunScenes(quiet).empty());

    // A KO room_end missing rounds / skills_used / hp_loss: the scene exists (rule 2) but the absent
    // slots drop their facts — never a "0" or "" placeholder.
    game::FRunLog sparse;
    sparse.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "defeat"}, {"ko", true}},
    };
    const auto scenes = game::BuildRunScenes(sparse);
    CHECK(scenes.size() == 1u);
    const game::FRunScene& scene = scenes.front();
    CHECK(!HasFactLabel(scene, "Rounds"));
    CHECK(!HasFactLabel(scene, "Skills used (all units)"));
    CHECK(!HasFactLabel(scene, "HP lost"));  // hp_loss is absent, not an empty array
    CHECK(HasFact(scene, "Knockout", "yes"));
    CHECK(HasFact(scene, "Room", "Depth 1 room 0"));
    CHECK(HasFact(scene, "Result", "defeat"));
    for (const auto& fact : scene.facts) CHECK(!fact.value.empty());

    // An empty log has no scenes. The "No decisive moment was recorded." sentence belongs to Render,
    // not to the builder — this test asserts builder output only.
    CHECK(game::BuildRunScenes(game::FRunLog{}).empty());
}

void OfferWindowExcludesCampPurchase() {
    // An offer line belongs to a room only inside the position window after that room's room_end (and
    // before the next room_start/room_end/dungeon_clear/camp). A camp rune purchase is logged as an
    // offer carrying the last battle room's (depth, room), so only the window keeps it out — its
    // coordinates match the room.
    game::FRunLog campBuy;
    campBuy.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 2}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 12}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "dungeon_clear"}, {"depth", 1}},
        json{{"t", "camp"}, {"depth", 1}, {"id", "camp.rune_offer"}, {"amount", 0}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 2}, {"picked", "rune.power"}, {"skipped", false}},
    };
    const auto campScenes = game::BuildRunScenes(campBuy);
    CHECK(campScenes.size() == 1u);  // the room is the rule-1 scene (loss 12 > 0)
    CHECK(campScenes.front().depth == 1 && campScenes.front().room == 2);
    CHECK(!HasFactLabel(campScenes.front(), "Offer"));  // the camp purchase is not this room's offer

    // The window's positive shape: an offer directly after its room_end, depth/room matching → attaches.
    game::FRunLog direct;
    direct.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 5}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 0}, {"picked", "rune.fire"}, {"skipped", false}},
    };
    const auto directScenes = game::BuildRunScenes(direct);
    CHECK(directScenes.size() == 1u);
    CHECK(HasFact(directScenes.front(), "Offer", "rune.fire"));

    // A room_start closes the window: the same (depth, room) offer after it attaches to nothing.
    game::FRunLog restarted;
    restarted.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 5}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "room_start"}, {"depth", 1}, {"room", 0}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 0}, {"picked", "rune.fire"}, {"skipped", false}},
    };
    const auto restartedScenes = game::BuildRunScenes(restarted);
    CHECK(restartedScenes.size() == 1u);
    CHECK(!HasFactLabel(restartedScenes.front(), "Offer"));
}

// ---------------------------------------------------------------------------------------------
// The real boot: encounter keys on room_end, cycle on run_end
// ---------------------------------------------------------------------------------------------

void RoomEndCarriesEncounterKeys() {
    FBoot boot(UniqueDir("run-debrief-encounter"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);

    // Room 0: a WP-14a plan (FindRoomPlan reads ctx) → the room_end carries all three keys verbatim.
    CHECK(!game::BuildDungeonBattlefield(registry).empty());
    registry.ctx().insert_or_assign<game::FRoomPlan>(game::FRoomPlan{"encounter.ember_pack", "beasts", true});
    CHECK(game::ResolveRoomVictory(registry, true).has_value());

    auto rooms = EventsOfType(boot.log(), "room_end");
    CHECK(rooms.size() == 1u);
    CHECK(rooms.back()["depth"] == 1);
    CHECK(rooms.back()["room"] == 0);
    CHECK(rooms.back()["encounter"] == "encounter.ember_pack");
    CHECK(rooms.back()["family"] == "beasts");
    CHECK(rooms.back()["elite"] == true);

    // Advance across the fixture's gap event (dungeon 1: battle → event → battle → battle).
    for (int guard = 0; guard < 4; ++guard) {
        const auto* rewards = registry.ctx().find<game::FRewards>();
        if (rewards == nullptr || !rewards->offer.active) break;
        CHECK(game::ResolveRuneOffer(registry, -1));
    }
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    if (boot.run().phase == game::EDungeonPhase::Event) {
        CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ChooseEventLoot).has_value());
    }
    CHECK(boot.run().phase == game::EDungeonPhase::Battle);

    // Room 1: no plan in ctx → none of the three keys (the legacy room).
    CHECK(!game::BuildDungeonBattlefield(registry).empty());
    registry.ctx().erase<game::FRoomPlan>();
    CHECK(game::ResolveRoomVictory(registry, true).has_value());

    rooms = EventsOfType(boot.log(), "room_end");
    CHECK(rooms.size() == 2u);
    CHECK(rooms.back()["depth"] == 1);
    CHECK(rooms.back()["room"] == 1);
    CHECK(!rooms.back().contains("encounter"));
    CHECK(!rooms.back().contains("family"));
    CHECK(!rooms.back().contains("elite"));

    // A wipe writes the run_end; `cycle` rides on it when the sequence is installed (0 on a fresh run).
    for (int guard = 0; guard < 4; ++guard) {
        const auto* rewards = registry.ctx().find<game::FRewards>();
        if (rewards == nullptr || !rewards->offer.active) break;
        CHECK(game::ResolveRuneOffer(registry, -1));
    }
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    const auto* sequence = game::FindRunSequence(registry);

    // The battle accumulation + intent telemetry the real wipe path reads (run_log_tests' setup), so this
    // wipe also produces the fatal-intent facts end to end.
    auto& stats = registry.ctx().emplace<game::FRoomBattleStats>();
    stats.sessionId = 1;
    stats.active = true;
    stats.rounds = 5;
    stats.ko = true;
    stats.hpStart["party.warrior"] = 30;
    stats.hpMin["party.warrior"] = 0;
    auto& intents = registry.ctx().emplace<game::FIntentStore>();
    intents.sessionId = 1;
    intents.stats.lastExecuted.skillId = "skill.maul";
    intents.stats.lastExecuted.expectedAtCommit = 8;
    intents.stats.lastExecuted.expectedAtExecution = 6;
    intents.stats.lastExecuted.actualDamage = 11;
    intents.stats.lastExecuted.hit = true;
    CHECK(game::ResolveRoomVictory(registry, false).has_value());

    const auto ends = EventsOfType(boot.log(), "run_end");
    CHECK(ends.size() == 1u);
    const json& runEnd = ends.back();
    CHECK(runEnd["result"] == "defeat");
    // The fixture root carries dungeon.json's themes, so the boot installs the sequence (FRunSequence is
    // emplaced by InitializeDungeon and FindRunSequence is non-null after it) — pin that branch rather than
    // branching around it, so the cycle producer is actually exercised here.
    CHECK(sequence != nullptr);
    CHECK(runEnd.contains("cycle"));
    CHECK(runEnd["cycle"].is_number_integer());
    CHECK(runEnd["cycle"].get<int>() == sequence->RunCycle(boot.run().cleared));
    CHECK(runEnd["cycle"].get<int>() == 0);  // a run that cleared nothing is cycle 0

    // The producer → builder chain: the wipe's defeat room_end and fatal intent become one merged scene.
    const auto scenes = game::BuildRunScenes(boot.log());
    CHECK(scenes.size() == 1u);
    const game::FRunScene& scene = scenes.front();
    CHECK(scene.depth == 1 && scene.room == 2);  // the room the wipe happened in
    CHECK(scene.rules == (std::vector<game::ERunSceneRule>{game::ERunSceneRule::LargestLoss,
                                                          game::ERunSceneRule::FirstKo,
                                                          game::ERunSceneRule::FatalIntent}));
    CHECK(HasFact(scene, "Fatal intent", "skill.maul"));
    CHECK(HasFact(scene, "Declared at commit", "8"));
    CHECK(HasFact(scene, "Expected at execution", "6"));
    CHECK(HasFact(scene, "Actual damage", "11"));
    CHECK(HasFact(scene, "Outcome", "hit"));
    CHECK(HasFact(scene, "Preview change", "8 -> 6"));
}

// ---------------------------------------------------------------------------------------------
// Next-run deltas (meta + checkpoint)
// ---------------------------------------------------------------------------------------------

void NextRunDeltasConcrete() {
    FBoot boot(UniqueDir("run-debrief-deltas"));
    // checkpoint.json is the run-start source today (D-21): cleared 2, re-read by a second initialize.
    WriteText(boot.checkpoint, json{{"schema_version", 1}, {"cleared", 2}}.dump());
    CHECK(game::InitializeDungeon(boot.registry, boot.checkpoint));
    CHECK(boot.registry.ctx().get<game::FDungeonCheckpoint>().error.empty());

    // An existing meta save: 4 held seals, a rank-1 checkpoint of 6, a lifetime grand count, one owned
    // unlock row and the earned-seal grants that satisfy the catalog gates (clear = +1, boss = +2).
    game::FMetaSave save = game::DefaultMetaSave();
    save.seals = 4;
    save.unlocks = {"skill.hook"};
    auto& lane = save.ladder["1"];
    lane.checkpoint = 6;
    lane.bestDungeon = 6;
    lane.granted = {"theme.ember_crypt/0#clear", "theme.ember_crypt/1#clear", "theme.ember_crypt/2#clear",
                    "theme.ember_crypt/0#boss", "theme.ember_crypt/1#boss"};
    save.stats.grand = 3;
    WriteText(boot.meta, game::MetaSaveToJson(save).dump());
    CHECK(game::InitializeMetaProgress(boot.registry, boot.meta));
    CHECK(!game::MetaDisabled(boot.registry));

    const FNextRunDeltas deltas = NextRunDeltas(boot.registry);
    CHECK(!deltas.metaDisabled);
    CHECK(deltas.seals == 4);
    CHECK(deltas.ladderCheckpoint == game::MetaLadderCheckpoint(boot.registry, 1));
    CHECK(deltas.ladderCheckpoint == 6);
    CHECK(deltas.checkpointCleared == 2);  // FDungeonCheckpoint::cleared, not the ladder value
    CHECK(deltas.grandCount == 3);

    // Unlock candidates = exactly the Purchasable rows: owned rows, gate-locked rows and rows above the
    // held seals are excluded.
    std::vector<std::string> expected;
    for (const auto& offer : game::BuildUnlockOffers(boot.registry)) {
        if (offer.status == game::EUnlockStatus::Purchasable) {
            const std::string cost =
                offer.row.cost == 1 ? "1 seal" : std::to_string(offer.row.cost) + " seals";
            expected.push_back(offer.row.id + " (" + cost + ")");
        }
    }
    CHECK(!expected.empty());  // the authored numbers must make the comparison non-vacuous
    CHECK(deltas.unlockCandidates == expected);
    const std::string candidates = Joined(deltas.unlockCandidates);
    CHECK(candidates.find("skill.hook") == std::string::npos);        // owned
    CHECK(candidates.find("skill.storm") == std::string::npos);       // cost 5 > the 4 held seals
    CHECK(candidates.find("party.rogue_stub") == std::string::npos);  // locked roster stub (unavailable)

    // Every number is spelled out in the lines.
    const std::string lines = Joined(NextRunDeltaLines(deltas));
    CHECK(lines.find("Seals: 4") != std::string::npos);
    CHECK(lines.find("checkpoint: 6") != std::string::npos);
    CHECK(lines.find("1 to 3") != std::string::npos);  // checkpointCleared + 1 (D-21)
    CHECK(lines.find("Achievements: 3") != std::string::npos);
    CHECK(lines.find("Unlock candidates:") != std::string::npos);
    CHECK(lines.find(expected.front()) != std::string::npos);  // "{id} ({cost} seals)"

    // A second session: 5 held seals and only 5 lifetime-earned. Storm's catalog gate (6) outranks its
    // affordable cost (5) — a candidate must respect the gate, not just the balance — while shackle's
    // gate (5) equals the earned total and stays purchasable.
    FBoot gated(UniqueDir("run-debrief-deltas-gate"));
    game::FMetaSave gatedSave = game::DefaultMetaSave();
    gatedSave.seals = 5;
    gatedSave.ladder["1"].granted = {"theme.ember_crypt/0#clear", "theme.ember_crypt/0#boss",
                                     "theme.ember_crypt/1#boss"};  // lifetime: 1 + 2 + 2 = 5
    WriteText(gated.meta, game::MetaSaveToJson(gatedSave).dump());
    CHECK(game::InitializeMetaProgress(gated.registry, gated.meta));

    const FNextRunDeltas gatedDeltas = NextRunDeltas(gated.registry);
    CHECK(gatedDeltas.seals == 5);
    std::vector<std::string> gatedExpected;
    for (const auto& offer : game::BuildUnlockOffers(gated.registry)) {
        if (offer.status == game::EUnlockStatus::Purchasable) {
            const std::string cost =
                offer.row.cost == 1 ? "1 seal" : std::to_string(offer.row.cost) + " seals";
            gatedExpected.push_back(offer.row.id + " (" + cost + ")");
        }
    }
    CHECK(gatedDeltas.unlockCandidates == gatedExpected);
    const std::string gatedCandidates = Joined(gatedDeltas.unlockCandidates);
    CHECK(gatedCandidates.find("skill.storm") == std::string::npos);   // gate 6 > the 5 earned; cost 5 is affordable
    CHECK(gatedCandidates.find("skill.shackle") != std::string::npos); // gate 5 == the 5 earned; cost 4 <= 5
}

void MetaDisabledShowsPlaceholder() {
    // No meta store at all: the deltas degrade to the placeholder plus the checkpoint lines.
    FBoot boot(UniqueDir("run-debrief-meta-off"));
    CHECK(game::MetaDisabled(boot.registry));
    CHECK(game::MetaGrandCount(boot.registry) == 0);
    CHECK(game::MetaLadderCheckpoint(boot.registry) == 0);

    const FNextRunDeltas deltas = NextRunDeltas(boot.registry);
    CHECK(deltas.metaDisabled);
    CHECK(deltas.seals == 0);
    CHECK(deltas.unlockCandidates.empty());
    CHECK(deltas.ladderCheckpoint == 0);
    CHECK(deltas.checkpointCleared == 0);
    CHECK(deltas.grandCount == 0);

    const std::string lines = Joined(NextRunDeltaLines(deltas));
    CHECK(lines.find("Unlocks and seals are shown when the unlock system is active.") != std::string::npos);
    CHECK(lines.find("Seals:") == std::string::npos);
    CHECK(lines.find("Unlock candidates") == std::string::npos);
    CHECK(lines.find("Ladder") == std::string::npos);
    CHECK(lines.find("Achievements") == std::string::npos);
}

void AchievementsUseGrandCount() {
    FBoot boot(UniqueDir("run-debrief-grand"));
    game::FMetaSave save = game::DefaultMetaSave();
    save.ladder["1"].checkpoint = 4;
    save.stats.grand = 5;
    WriteText(boot.meta, game::MetaSaveToJson(save).dump());
    CHECK(game::InitializeMetaProgress(boot.registry, boot.meta));

    const auto* stored = game::FindMetaSave(boot.registry);
    CHECK(stored != nullptr);
    CHECK(stored->stats.grand == 5);  // MetaGrandCount reads exactly this field
    CHECK(game::MetaGrandCount(boot.registry) == 5);

    const FNextRunDeltas deltas = NextRunDeltas(boot.registry);
    CHECK(deltas.grandCount == 5);
    CHECK(Joined(NextRunDeltaLines(deltas)).find("Achievements: 5 grand breakthrough") != std::string::npos);

    CHECK(game::MetaLadderCheckpoint(boot.registry, 1) == 4);
    CHECK(game::MetaLadderCheckpoint(boot.registry, 2) == 0);  // no rank-2 row
    CHECK(game::MetaLadderCheckpoint(boot.registry, 0) == 0);  // rank < 1
}

// ---------------------------------------------------------------------------------------------
// ①② The ledger: what the run gained and what it loses vs keeps (the 3-part acceptance)
// ---------------------------------------------------------------------------------------------

void RunEndLedgerSeparatesLostAndKept() {
    // Pure: no boot — a run, a log and the deltas are the only inputs.
    game::FDungeonRun run;
    run.runes = {"rune.a", "rune.b"};
    run.materials = 3;
    run.party.resize(2);
    run.party[0].level = 3;
    run.party[0].allocatedNodes = {"node.a", "node.b"};
    run.party[1].level = 1;  // no allocated nodes

    game::FRunLog log;
    log.events = {
        json{{"t", "dungeon_clear"}, {"depth", 1}},
        json{{"t", "camp"}, {"depth", 1}, {"amount", 4}},
        json{{"t", "dungeon_clear"}, {"depth", 2}},
        json{{"t", "camp"}, {"depth", 2}, {"amount", 1}},
    };

    FNextRunDeltas deltas;
    deltas.metaDisabled = false;
    deltas.checkpointCleared = 2;
    deltas.rosterSize = 4;
    deltas.ownedUnlocks = 1;

    const FRunEndLedger ledger = BuildRunEndLedger(&run, log, deltas);
    // ① gained: the run's held facts.
    CHECK(HasLine(ledger.gained, "Dungeons cleared: 2"));
    CHECK(HasLine(ledger.gained, "Materials: 5 spent at camp, 3 unspent"));  // camp 4 + 1, balance 3
    CHECK(HasLine(ledger.gained, "Levels gained: 2"));                       // (3-1) + (1-1)
    CHECK(HasLine(ledger.gained, "Passive nodes gained: 2"));
    bool runesLine = false;
    for (const auto& line : ledger.gained) {
        if (line.find("rune.a") != std::string::npos && line.find("rune.b") != std::string::npos) {
            runesLine = true;
        }
    }
    CHECK(runesLine);  // the runes line names every id the run holds
    // ② lost vs kept: the run-scoped numbers are lost, the meta/checkpoint numbers stay. The runes line
    // is a count only — the id list stays in ① (critic round).
    CHECK(HasLine(ledger.lost, "Runes lost: 2"));
    for (const auto& line : ledger.lost) {
        CHECK(line.find("rune.a") == std::string::npos);
        CHECK(line.find("rune.b") == std::string::npos);
    }
    CHECK(HasLine(ledger.lost, "Unspent materials lost: 3"));
    CHECK(HasLine(ledger.lost, "Levels lost: 2"));
    CHECK(HasLine(ledger.lost, "Passive nodes lost: 2"));
    CHECK(HasLine(ledger.kept, "Checkpoint kept: 2 dungeons cleared"));
    CHECK(HasLine(ledger.kept, "Roster kept: 4 heroes"));
    CHECK(HasLine(ledger.kept, "Unlocks kept: 1 owned"));

    // Meta disabled: the roster/unlock numbers are meta facts and are not claimed — only the checkpoint line.
    FNextRunDeltas off = deltas;
    off.metaDisabled = true;
    const FRunEndLedger offLedger = BuildRunEndLedger(&run, log, off);
    CHECK(offLedger.kept.size() == 1u);
    CHECK(HasLine(offLedger.kept, "Checkpoint kept: 2 dungeons cleared"));

    // No run: only the log fact (dungeons cleared) remains on the gained side; nothing is "lost".
    const FRunEndLedger noRun = BuildRunEndLedger(nullptr, log, deltas);
    CHECK(noRun.gained.size() == 1u);
    CHECK(HasLine(noRun.gained, "Dungeons cleared: 2"));
    CHECK(noRun.lost.empty());
}

// ---------------------------------------------------------------------------------------------
// Cycle ④ and friction ⑤ lines
// ---------------------------------------------------------------------------------------------

void CycleShownWhenPresent() {
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 3}}})}, {"skills_used", {}},
             {"ko", false}},
        json{{"t", "room_end"}, {"depth", 2}, {"room", 1}, {"result", "defeat"}, {"rounds", 7},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 9}}})}, {"skills_used", {}},
             {"ko", true}},
        json{{"t", "run_end"}, {"result", "defeat"}, {"depth", 2}, {"cycle", 2}},
    };
    const FRunEndSummary summary = BuildRunEndSummary(log);
    CHECK(summary.cycle.size() == 1u);
    if (!summary.cycle.empty()) CHECK(summary.cycle.front() == "Cycle 2 - deepest depth 2");
    CHECK(Joined(BuildRunEndSummaryLines(log)).find("Cycle 2 - deepest depth 2") != std::string::npos);

    game::FRunLog zero = log;
    zero.events.back()["cycle"] = 0;  // 0 = off: no line
    CHECK(BuildRunEndSummary(zero).cycle.empty());
    CHECK(Joined(BuildRunEndSummaryLines(zero)).find("Cycle ") == std::string::npos);

    game::FRunLog absent = log;
    absent.events.back().erase("cycle");  // absent: no line either
    CHECK(BuildRunEndSummary(absent).cycle.empty());
    CHECK(Joined(BuildRunEndSummaryLines(absent)).find("Cycle ") == std::string::npos);
}

void FrictionShown() {
    game::FRunLog log;
    log.events = {
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "defeat"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 12}}})}, {"skills_used", {}},
             {"ko", true}},
        json{{"t", "run_end"}, {"result", "defeat"}, {"depth", 1}, {"ms_to_first_battle", 1500}},
    };
    const FRunEndSummary summary = BuildRunEndSummary(log);
    CHECK(summary.friction.size() == 1u);
    if (!summary.friction.empty()) {
        CHECK(summary.friction.front().starts_with("Time to first battle: 1.5 s"));
        CHECK(summary.friction.front().find("(dev metric)") != std::string::npos);
    }
    CHECK(Joined(BuildRunEndSummaryLines(log)).find("Time to first battle: 1.5 s") != std::string::npos);

    game::FRunLog absent = log;
    absent.events.back().erase("ms_to_first_battle");
    CHECK(BuildRunEndSummary(absent).friction.empty());
    CHECK(Joined(BuildRunEndSummaryLines(absent)).find("Time to first battle") == std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// Purity + the pinned WP-19a text
// ---------------------------------------------------------------------------------------------

void SummaryPureOverLog() {
    // The run_log_tests MinimalSummaryShows log: the new fields must not move its metrics/rooms/fatal
    // text, and every builder stays pure over the const log.
    game::FRunLog log;
    log.events = {
        json{{"t", "run_start"}, {"frame", 10}, {"ms", 100}},
        json{{"t", "dungeon_enter"}, {"depth", 1}},
        json{{"t", "room_start"}, {"depth", 1}, {"room", 0}, {"frame", 20}, {"ms", 150}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 0}, {"result", "win"}, {"rounds", 4},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 0}}})},
             {"intents", {{"committed", 4}, {"whiffed", 0}}}, {"ko", false}},
        json{{"t", "offer"}, {"depth", 1}, {"room", 0}, {"picked", "rune.fire"}, {"skipped", false}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 1}, {"result", "win"}, {"rounds", 6},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 12}}})},
             {"intents", {{"committed", 4}, {"whiffed", 0}}}, {"ko", true}},
        json{{"t", "camp"}, {"depth", 1}, {"id", "upgrade.attack"}, {"amount", 4}},
        json{{"t", "room_end"}, {"depth", 1}, {"room", 2}, {"result", "win"}, {"rounds", 5},
             {"hp_loss", json::array({{{"id", "party.warrior"}, {"loss", 0}}})},
             {"intents", {{"committed", 4}, {"whiffed", 0}}}, {"ko", false}},
    };
    const game::FRunLog& constLog = log;

    const FRunEndSummary first = BuildRunEndSummary(constLog);
    const FRunEndSummary second = BuildRunEndSummary(constLog);
    CHECK(first.metrics == second.metrics);
    CHECK(first.rooms == second.rooms);
    CHECK(first.fatal == second.fatal);
    CHECK(first.scenes == second.scenes);
    CHECK(first.cycle == second.cycle);
    CHECK(first.friction == second.friction);

    const std::string text = Joined(BuildRunEndSummaryLines(constLog));
    CHECK(Joined(BuildRunEndSummaryLines(constLog)) == text);  // a second full call is byte-identical
    CHECK(text.find("Average rounds per room: 5.0") != std::string::npos);
    CHECK(text.find("Damage-free rooms: 2 of 3 (67%)") != std::string::npos);
    CHECK(text.find("Rooms with a KO: 1 of 3 (33%)") != std::string::npos);
    CHECK(text.find("Intent whiff rate: 0%") != std::string::npos);
    CHECK(text.find("Decisions per dungeon: 2.0") != std::string::npos);
    CHECK(text.find("picked rune.fire") != std::string::npos);
    CHECK(text.find("camp spent 4") != std::string::npos);
    CHECK(text.find("No enemy intent was recorded.") != std::string::npos);

    const auto scenesA = game::BuildRunScenes(constLog);
    const auto scenesB = game::BuildRunScenes(constLog);
    CHECK(!scenesA.empty());  // room 1 is rule 1 and rule 2 at once
    CHECK(SceneText(scenesA) == SceneText(scenesB));
}

} // namespace

int main() {
    // Save safety (same pattern as run_log_tests): a plain `ctest` exports no GAME01P_SAVE_DIR, so commit
    // an override before any dungeon or meta service can touch the real save under %APPDATA%.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "run_debrief_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("run debrief", {
        {"SceneRule1IsLargestLossRoom", SceneRule1IsLargestLossRoom},
        {"SceneRule2FirstKoRoom", SceneRule2FirstKoRoom},
        {"Scene3UsesFatalIntentFacts", Scene3UsesFatalIntentFacts},
        {"ScenesDeduplicatedByRoom", ScenesDeduplicatedByRoom},
        {"TieBreaksEarliestRoom", TieBreaksEarliestRoom},
        {"ScenesAreGrounded", ScenesAreGrounded},
        {"NoCounterfactualWithoutNumbers", NoCounterfactualWithoutNumbers},
        {"SceneOmittedWhenFewerFacts", SceneOmittedWhenFewerFacts},
        {"OfferWindowExcludesCampPurchase", OfferWindowExcludesCampPurchase},
        {"RoomEndCarriesEncounterKeys", RoomEndCarriesEncounterKeys},
        {"NextRunDeltasConcrete", NextRunDeltasConcrete},
        {"MetaDisabledShowsPlaceholder", MetaDisabledShowsPlaceholder},
        {"AchievementsUseGrandCount", AchievementsUseGrandCount},
        {"RunEndLedgerSeparatesLostAndKept", RunEndLedgerSeparatesLostAndKept},
        {"CycleShownWhenPresent", CycleShownWhenPresent},
        {"FrictionShown", FrictionShown},
        {"SummaryPureOverLog", SummaryPureOverLog},
    });
}
