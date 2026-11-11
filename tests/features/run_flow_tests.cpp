// WP-02a: the run transition table, the per-run event log and the save wiring.
//
// The rules are neutral: FRunFlow returns a FRunResult and never an app-layer state type. The app layer
// maps the result to its state stack in WP-02b. Every test here asserts the run state, the save file on
// disk or the event log — not "did not crash".
//
// The event log is per-run (StartRun/ContinueDungeonRun install a fresh one); the listeners live in the
// registry context and survive a run restart. The save is written as run → event → save, so a listener's
// change lands in the same save.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FSaveSections.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

constexpr std::array<game::EDungeonPhase, 5> kCorePhases = {
    game::EDungeonPhase::Battle, game::EDungeonPhase::Reward, game::EDungeonPhase::Event,
    game::EDungeonPhase::Camp, game::EDungeonPhase::Defeat};

// --- scratch files -----------------------------------------------------------------------------

std::string UniquePath(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return "out/" + std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
}

void RemoveFiles(const std::string& checkpoint) {
    std::error_code error;
    std::filesystem::remove(checkpoint, error);
    std::filesystem::remove(checkpoint + ".run.json", error);
    std::filesystem::remove(checkpoint + ".run.json.tmp", error);
}

json ReadJsonFile(const std::string& path) {
    std::ifstream file(path);
    CHECK(file.good());
    json document;
    file >> document;
    return document;
}

std::string ReadBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// --- a booted registry with its own checkpoint and save slot -----------------------------------

struct FBoot {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;

    explicit FBoot(std::string path) : checkpoint(std::move(path)) {
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
    }

    ~FBoot() {
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave + ".tmp", error);
    }
};

// --- a save section with one counter (the save-wiring tests) ----------------------------------

struct FSectionCounter {
    int value{0};
};

game::FSaveSectionSerializer CountingSection() {
    game::FSaveSectionSerializer serializer;
    serializer.id = "counter";
    serializer.version = 1;
    serializer.pack = [](entt::registry& registry) {
        return json{{"value", registry.ctx().get<FSectionCounter>().value}};
    };
    serializer.unpack = [](entt::registry& registry, const json& section) {
        registry.ctx().insert_or_assign<FSectionCounter>(FSectionCounter{section.value("value", 0)});
    };
    return serializer;
}

// --- transition table --------------------------------------------------------------------------

void TransitionTableWalks() {
    FBoot boot(UniquePath("run-flow-walk"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.phase == game::EDungeonPhase::Battle);
    CHECK(run.depth == 1 && run.room == 0);

    // Battle victory → Reward (ShowModal kRunModalReward). The rune is an offer now: the room opens it, and
    // only a pick/skip closes it (WP-17).
    auto result = game::ResolveRoomVictory(registry, true);
    CHECK(result.has_value());
    CHECK(result->exit == game::ERunExit::ShowModal && result->modalId == std::string(game::kRunModalReward));
    CHECK(run.phase == game::EDungeonPhase::Reward);
    CHECK(run.runes.empty());
    CHECK(registry.ctx().get<game::FRewards>().offer.active);
    CHECK(game::ResolveRuneOffer(registry, 0));
    CHECK(run.runes.size() == 1u);

    // Reward(0) → Event (ShowModal kRunModalEvent).
    result = game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward);
    CHECK(result.has_value() && result->exit == game::ERunExit::ShowModal &&
          result->modalId == std::string(game::kRunModalEvent));
    CHECK(run.phase == game::EDungeonPhase::Event);

    // Event → Battle(room 1) (NextRoom).
    result = game::ApplyRunIntent(registry, game::ERunIntent::ChooseEventLoot);
    CHECK(result.has_value() && result->exit == game::ERunExit::NextRoom && result->modalId.empty());
    CHECK(run.phase == game::EDungeonPhase::Battle && run.room == 1 && run.advanceRequested);

    // Room 1 victory → Reward; Reward(1) → Battle(room 2) (NextRoom).
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(run.phase == game::EDungeonPhase::Reward && run.room == 1);
    CHECK(game::ResolveRuneOffer(registry, -1));
    result = game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward);
    CHECK(result.has_value() && result->exit == game::ERunExit::NextRoom);
    CHECK(run.phase == game::EDungeonPhase::Battle && run.room == 2);

    // Room 2 victory → Reward; Reward(2) → Camp (ShowModal kRunModalCamp), cleared=depth.
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, -1));
    result = game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward);
    CHECK(result.has_value() && result->exit == game::ERunExit::ShowModal &&
          result->modalId == std::string(game::kRunModalCamp));
    CHECK(run.phase == game::EDungeonPhase::Camp && run.cleared == 1 && run.cleared == run.depth);

    // Camp → Battle(depth+1, room 0) (NextRoom).
    result = game::ApplyRunIntent(registry, game::ERunIntent::LeaveCamp);
    CHECK(result.has_value() && result->exit == game::ERunExit::NextRoom);
    CHECK(run.phase == game::EDungeonPhase::Battle && run.depth == 2 && run.room == 0 && run.advanceRequested);

    // The walk recorded each victory, each offer decision and the shrine choice in order, then the dungeon
    // clear at the camp.
    const auto& log = registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.size() == 10u);
    CHECK(log.nextSeq == 11);
    CHECK(log.tail[0].kind == game::ERunEventKind::RoomCleared && log.tail[0].seq == 1);
    CHECK(log.tail[1].kind == game::ERunEventKind::RewardPicked && log.tail[1].seq == 2);
    CHECK(!log.tail[1].id.empty() && log.tail[1].amount == 0);
    CHECK(log.tail[2].kind == game::ERunEventKind::EventChosen && log.tail[2].seq == 3);
    CHECK(log.tail[2].id == "event.quiet_shrine" && log.tail[2].amount == 1);
    CHECK(log.tail[3].kind == game::ERunEventKind::RoomCleared && log.tail[3].seq == 4);
    CHECK(log.tail[4].kind == game::ERunEventKind::RewardPicked && log.tail[4].seq == 5);
    CHECK(log.tail[4].id.empty() && log.tail[4].amount == 1);
    CHECK(log.tail[5].kind == game::ERunEventKind::RoomCleared && log.tail[5].seq == 6);
    CHECK(log.tail[6].kind == game::ERunEventKind::RewardPicked && log.tail[6].seq == 7);
    CHECK(log.tail[7].kind == game::ERunEventKind::DungeonCleared && log.tail[7].seq == 8);
    CHECK(log.tail[7].depth == 1);  // DungeonCleared carries the depth that was cleared
    CHECK(log.tail[7].room == 2);
    CHECK(log.tail[8].kind == game::ERunEventKind::CampEntered);
    CHECK(log.tail[9].kind == game::ERunEventKind::CampLeft);
}

/// The transition table has one writer: only FRunFlow.cpp assigns a run's phase. Documented exceptions:
/// FDungeonSave.cpp is the save parser (it builds a run from a document), and FDungeonRun.cpp writes the
/// FDungeonSave mirror (`save.phase = run->phase`), which is not a run.
void TransitionTableSingleWriter() {
    namespace fs = std::filesystem;
    std::vector<std::string> violations;
    int assignments = 0;
    for (const auto& entry : fs::recursive_directory_iterator("src")) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        if (path.extension() != ".cpp" && path.extension() != ".h") continue;
        if (path.filename() == "FDungeonSave.cpp") continue;  // save parser owns one run construction
        const bool allowed = path.filename() == "FRunFlow.cpp";
        std::ifstream file(path);
        std::string line;
        int lineNumber = 0;
        while (std::getline(file, line)) {
            ++lineNumber;
            // Mirror writes to the FDungeonSave struct, not to the run.
            if (line.find("save.phase") != std::string::npos || line.find("save->phase") != std::string::npos) continue;
            for (std::size_t i = 0; i + 5 <= line.size(); ++i) {
                if (line.compare(i, 5, "phase") != 0) continue;
                const bool member = i > 0 && (line[i - 1] == '.' ||
                    (line[i - 1] == '>' && i > 1 && line[i - 2] == '-'));
                if (!member) continue;
                if (i + 5 < line.size() &&
                    (std::isalnum(static_cast<unsigned char>(line[i + 5])) || line[i + 5] == '_')) continue;
                std::size_t cursor = i + 5;
                while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) ++cursor;
                if (cursor >= line.size() || line[cursor] != '=') continue;
                if (cursor + 1 < line.size() && line[cursor + 1] == '=') continue;  // comparison
                ++assignments;
                if (!allowed) {
                    violations.push_back(path.string() + ":" + std::to_string(lineNumber) + ": " + line);
                }
            }
        }
    }
    CHECK(assignments > 0);  // FRunFlow.cpp must be the writer, not an empty table
    for (const auto& violation : violations) std::cout << "    " << violation << '\n';
    CHECK(violations.empty());
}

void WrongPhaseIsRejected() {
    FBoot boot(UniquePath("run-flow-wrong"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const std::string battleSave = ReadBytes(boot.runSave);
    auto& log = registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.empty() && log.nextSeq == 1);

    // Intents that belong to another phase are refused with no state, save or event change.
    CHECK(!game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(!game::ApplyRunIntent(registry, game::ERunIntent::ChooseEventRest).has_value());
    CHECK(!game::ApplyRunIntent(registry, game::ERunIntent::LeaveCamp).has_value());
    CHECK(!game::ApplyRunIntent(registry, game::ERunIntent::RetryAfterDefeat).has_value());
    CHECK(run.phase == game::EDungeonPhase::Battle && run.room == 0 && run.depth == 1);
    CHECK(ReadBytes(boot.runSave) == battleSave);
    CHECK(log.tail.empty() && log.nextSeq == 1);

    // Enter the reward step, then refuse the intents of another phase.
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(run.phase == game::EDungeonPhase::Reward);
    const std::string rewardSave = ReadBytes(boot.runSave);
    CHECK(!game::ApplyRunIntent(registry, game::ERunIntent::AbandonRun).has_value());
    CHECK(!game::ApplyRunIntent(registry, game::ERunIntent::LeaveCamp).has_value());
    CHECK(!game::ResolveRoomVictory(registry, true).has_value());
    CHECK(run.phase == game::EDungeonPhase::Reward);
    CHECK(ReadBytes(boot.runSave) == rewardSave);
    CHECK(log.tail.size() == 1u);  // only the RoomCleared event from the victory
}

// --- exits that drop the run ------------------------------------------------------------------

/// Walks the three rooms of a fresh run to the camp: battle → reward → event → battle → reward →
/// battle → reward → camp.
void WalkToCamp(entt::registry& registry) {
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, -1));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ChooseEventLoot).has_value());
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, -1));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    CHECK(game::ResolveRuneOffer(registry, -1));
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Camp);
}

/// Every WP §1 exit row: the exact result, the run's presence, the save file and the last event.
void ExitTransitionsDropTheRun() {
    // 1. A battle defeat opens the RunEnd modal, drops the save, and leaves the run in Defeat.
    {
        FBoot boot(UniquePath("run-flow-exit-defeat"));
        auto& registry = boot.registry;
        game::StartRun(registry, kSeed, true);
        CHECK(std::filesystem::exists(boot.runSave));

        auto result = game::ResolveRoomVictory(registry, false);
        CHECK(result.has_value());
        CHECK(result->exit == game::ERunExit::ShowModal);
        CHECK(result->modalId == std::string(game::kRunModalRunEnd));
        CHECK(registry.ctx().contains<game::FDungeonRun>());
        CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Defeat);
        CHECK(!std::filesystem::exists(boot.runSave));
        CHECK(!registry.ctx().get<game::FDungeonSave>().exists);
        const auto& log = registry.ctx().get<game::FRunEventLog>();
        CHECK(log.tail.size() == 1u);
        CHECK(log.tail.back().kind == game::ERunEventKind::RunEnded);

        // RetryAfterDefeat drops the lost run and asks the prepare step to start a new one.
        result = game::ApplyRunIntent(registry, game::ERunIntent::RetryAfterDefeat);
        CHECK(result.has_value());
        CHECK(result->exit == game::ERunExit::ToPrepare);
        CHECK(!registry.ctx().contains<game::FDungeonRun>());

        // With no run in the context the error is the distinct "no run" message, not a phase message.
        const auto noRun = game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward);
        CHECK(!noRun.has_value());
        CHECK(noRun.error() == "There is no run.");
        CHECK(!game::ResolveRoomVictory(registry, true).has_value());
    }

    // 2. A second defeat, then LeaveRun from Defeat: the title, and the run is erased.
    {
        FBoot boot(UniquePath("run-flow-exit-leave-defeat"));
        auto& registry = boot.registry;
        game::StartRun(registry, kSeed, true);
        CHECK(game::ResolveRoomVictory(registry, false).has_value());
        CHECK(!std::filesystem::exists(boot.runSave));
        const auto& log = registry.ctx().get<game::FRunEventLog>();
        CHECK(log.tail.size() == 1u);
        CHECK(log.tail.back().kind == game::ERunEventKind::RunEnded);

        const auto result = game::ApplyRunIntent(registry, game::ERunIntent::LeaveRun);
        CHECK(result.has_value());
        CHECK(result->exit == game::ERunExit::ToTitle);
        CHECK(!registry.ctx().contains<game::FDungeonRun>());
        CHECK(log.tail.back().kind == game::ERunEventKind::RunEnded);  // leaving appends nothing
    }

    // 3. Battle AbandonRun: the listener sees the run in the context before the erase; the save is deleted.
    {
        FBoot boot(UniquePath("run-flow-exit-abandon-battle"));
        auto& registry = boot.registry;
        game::StartRun(registry, kSeed, true);
        bool runPresent = false;
        bool sawRunEnded = false;
        game::RegisterRunEventListener(registry, [&](const game::FRunEvent& event) {
            runPresent = registry.ctx().contains<game::FDungeonRun>();
            if (event.kind == game::ERunEventKind::RunEnded) sawRunEnded = true;
        });

        const auto result = game::ApplyRunIntent(registry, game::ERunIntent::AbandonRun);
        CHECK(result.has_value());
        CHECK(result->exit == game::ERunExit::ToTitle);
        CHECK(!registry.ctx().contains<game::FDungeonRun>());
        CHECK(!std::filesystem::exists(boot.runSave));
        CHECK(!registry.ctx().get<game::FDungeonSave>().exists);
        CHECK(sawRunEnded);
        CHECK(runPresent);  // the run was still installed when the event was delivered
        CHECK(registry.ctx().get<game::FRunEventLog>().tail.back().kind == game::ERunEventKind::RunEnded);
    }

    // 4. Camp AbandonRun: the same exit from the camp.
    {
        FBoot boot(UniquePath("run-flow-exit-abandon-camp"));
        auto& registry = boot.registry;
        game::StartRun(registry, kSeed, true);
        WalkToCamp(registry);
        CHECK(std::filesystem::exists(boot.runSave));
        bool runPresent = false;
        bool sawRunEnded = false;
        game::RegisterRunEventListener(registry, [&](const game::FRunEvent& event) {
            runPresent = registry.ctx().contains<game::FDungeonRun>();
            if (event.kind == game::ERunEventKind::RunEnded) sawRunEnded = true;
        });

        const auto result = game::ApplyRunIntent(registry, game::ERunIntent::AbandonRun);
        CHECK(result.has_value());
        CHECK(result->exit == game::ERunExit::ToTitle);
        CHECK(!registry.ctx().contains<game::FDungeonRun>());
        CHECK(!std::filesystem::exists(boot.runSave));
        CHECK(sawRunEnded && runPresent);
        CHECK(registry.ctx().get<game::FRunEventLog>().tail.back().kind == game::ERunEventKind::RunEnded);
    }

    // 5. Battle LeaveRun: the save survives and still says "battle"; the run is erased.
    {
        FBoot boot(UniquePath("run-flow-exit-leave"));
        auto& registry = boot.registry;
        game::StartRun(registry, kSeed, true);

        const auto result = game::ApplyRunIntent(registry, game::ERunIntent::LeaveRun);
        CHECK(result.has_value());
        CHECK(result->exit == game::ERunExit::ToTitle);
        CHECK(!registry.ctx().contains<game::FDungeonRun>());
        CHECK(std::filesystem::exists(boot.runSave));
        CHECK(ReadJsonFile(boot.runSave)["run"]["phase"] == "battle");
        CHECK(registry.ctx().get<game::FDungeonSave>().exists);
        CHECK(registry.ctx().get<game::FRunEventLog>().tail.empty());  // leaving appends nothing
    }

    // 6. A failed save leaves the run installed and every field untouched.
    {
        FBoot boot(UniquePath("run-flow-exit-leave-fail"));
        auto& registry = boot.registry;
        game::StartRun(registry, kSeed, true);
        const std::string previous = ReadBytes(boot.runSave);

        game::FSaveSectionSerializer broken;
        broken.id = "broken-exit";
        broken.version = 1;
        broken.pack = [](entt::registry&) -> json { throw std::runtime_error("cannot pack"); };
        broken.unpack = [](entt::registry&, const json&) {};
        game::RegisterSaveSection(registry, broken);

        const auto& run = registry.ctx().get<game::FDungeonRun>();
        const auto seed = run.seed;
        const auto depth = run.depth;
        const auto room = run.room;
        const auto cleared = run.cleared;
        const auto materials = run.materials;
        const auto phase = run.phase;
        const auto runes = run.runes;
        const auto advance = run.advanceRequested;

        const auto result = game::ApplyRunIntent(registry, game::ERunIntent::LeaveRun);
        CHECK(!result.has_value());
        CHECK(!result.error().empty());
        CHECK(registry.ctx().contains<game::FDungeonRun>());
        CHECK(run.seed == seed && run.depth == depth && run.room == room && run.cleared == cleared &&
              run.materials == materials && run.phase == phase && run.runes == runes &&
              run.advanceRequested == advance);
        CHECK(ReadBytes(boot.runSave) == previous);
        CHECK(!registry.ctx().get<game::FDungeonSave>().error.empty());
        CHECK(registry.ctx().get<game::FRunEventLog>().tail.empty());  // a failed save appends nothing
    }
}

// --- result neutrality -------------------------------------------------------------------------

void RunResultIsNeutral() {
    static_assert(std::is_enum_v<game::ERunExit>);
    static_assert(std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(game::FRunResult{}.exit)>>,
                                 game::ERunExit>);
    static_assert(std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(game::FRunResult{}.modalId)>>,
                                 std::string>);

    std::ifstream header("src/gameplay/dungeon/FRunFlow.h");
    CHECK(header.good());
    const std::string text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
    for (const char* forbidden : {"EStateChangeKind", "FStateTransition", "states/", "presentation/"}) {
        CHECK(text.find(forbidden) == std::string::npos);
    }
}

// --- phase adapters ----------------------------------------------------------------------------

void PhaseIdRoundTrip() {
    for (const auto phase : kCorePhases) {
        const auto id = game::PhaseId(phase);
        CHECK(id != nullptr);
        CHECK(game::PhaseFromId(id) == phase);
    }
    CHECK(!game::PhaseFromId("bogus").has_value());
    CHECK(!game::PhaseFromId("").has_value());
    CHECK(!game::PhaseFromId("Battle").has_value());
}

void IsKnownPhaseIdCore() {
    for (const auto phase : kCorePhases) CHECK(game::IsKnownPhaseId(game::PhaseId(phase)));
    CHECK(!game::IsKnownPhaseId("somewhere"));
    CHECK(!game::IsKnownPhaseId(""));
    CHECK(!game::IsKnownPhaseId("Battle"));
}

void SavedPhaseNamesUnchanged() {
    for (const auto phase : kCorePhases) {
        CHECK(std::string_view(game::PhaseId(phase)) == game::DungeonPhaseName(phase));
    }
    CHECK(std::string_view(game::PhaseId(game::EDungeonPhase::Battle)) == "battle");
    CHECK(std::string_view(game::PhaseId(game::EDungeonPhase::Reward)) == "reward");
    CHECK(std::string_view(game::PhaseId(game::EDungeonPhase::Event)) == "event");
    CHECK(std::string_view(game::PhaseId(game::EDungeonPhase::Camp)) == "camp");
    CHECK(std::string_view(game::PhaseId(game::EDungeonPhase::Defeat)) == "defeat");
}

void PhaseTypeStaysEnumUntilWP08() {
    static_assert(std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(game::FDungeonRun{}.phase)>>,
                                 game::EDungeonPhase>);
    static_assert(std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(game::FDungeonSave{}.phase)>>,
                                 game::EDungeonPhase>);
}

// --- event log ---------------------------------------------------------------------------------

void RunEventLogOrdered() {
    FBoot boot(UniquePath("run-flow-events"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    auto& log = registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.empty() && log.nextSeq == 1);

    game::AppendRunEvent(registry, game::ERunEventKind::RoomCleared, "room.1", 3);
    game::AppendRunEvent(registry, game::ERunEventKind::BossDefeated, "boss.x", 7);
    game::AppendRunEvent(registry, game::ERunEventKind::RewardPicked, "rune.fire", 0);

    CHECK(log.tail.size() == 3u);
    CHECK(log.tail[0].seq == 1 && log.tail[1].seq == 2 && log.tail[2].seq == 3);
    CHECK(log.tail[0].kind == game::ERunEventKind::RoomCleared && log.tail[0].id == "room.1" && log.tail[0].amount == 3);
    CHECK(log.tail[1].kind == game::ERunEventKind::BossDefeated && log.tail[1].id == "boss.x" && log.tail[1].amount == 7);
    CHECK(log.tail[2].kind == game::ERunEventKind::RewardPicked && log.tail[2].id == "rune.fire");
    CHECK(log.tail[0].depth == 1 && log.tail[0].room == 0);  // copied from the run
    CHECK(log.nextSeq == 4);
}

void RunEventLogDeliversOnce() {
    FBoot boot(UniquePath("run-flow-deliver"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    int calls = 0;
    game::FRunEvent received;
    game::RegisterRunEventListener(registry, [&](const game::FRunEvent& event) { ++calls; received = event; });

    game::AppendRunEvent(registry, game::ERunEventKind::RoomCleared, "room", 1);
    CHECK(calls == 1);
    CHECK(received.seq == 1 && received.kind == game::ERunEventKind::RoomCleared && received.id == "room" &&
          received.amount == 1);

    game::AppendRunEvent(registry, game::ERunEventKind::RewardPicked, "rune", 2);
    CHECK(calls == 2);
    CHECK(received.seq == 2 && received.kind == game::ERunEventKind::RewardPicked && received.amount == 2);
}

void ListenerAppendIsRejected() {
    FBoot boot(UniquePath("run-flow-reentrant"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    int firstCalls = 0;
    int secondCalls = 0;
    game::RegisterRunEventListener(registry, [&](const game::FRunEvent&) {
        ++firstCalls;
        game::AppendRunEvent(registry, game::ERunEventKind::CampSpent);  // dropped: delivering
    });
    game::RegisterRunEventListener(registry, [&](const game::FRunEvent&) { ++secondCalls; });

    game::AppendRunEvent(registry, game::ERunEventKind::RoomCleared);
    CHECK(firstCalls == 1);
    CHECK(secondCalls == 1);
    const auto& log = registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.size() == 1u);
    CHECK(log.tail.front().kind == game::ERunEventKind::RoomCleared);
    CHECK(!registry.ctx().get<game::FRunEventListeners>().delivering);
}

void ThrowingListenerKeepsDelivering() {
    FBoot boot(UniquePath("run-flow-throw"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    int calls = 0;
    game::RegisterRunEventListener(registry, [](const game::FRunEvent&) { throw std::runtime_error("boom"); });
    game::RegisterRunEventListener(registry, [&](const game::FRunEvent&) { ++calls; });

    game::AppendRunEvent(registry, game::ERunEventKind::RoomCleared);
    CHECK(calls == 1);
    CHECK(!registry.ctx().get<game::FRunEventListeners>().delivering);
}

void ListenerChangeIsInSameSave() {
    FBoot boot(UniquePath("run-flow-listener-save"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    registry.ctx().emplace<FSectionCounter>(FSectionCounter{0});
    game::RegisterSaveSection(registry, CountingSection());
    game::RegisterRunEventListener(registry, [&registry](const game::FRunEvent& event) {
        if (event.kind == game::ERunEventKind::RoomCleared) registry.ctx().get<FSectionCounter>().value++;
    });

    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    const auto saved = ReadJsonFile(boot.runSave);
    CHECK(saved["run"]["ext"].contains("counter"));
    CHECK(saved["run"]["ext"]["counter"]["value"] == 1);
}

void ContinueStartsFreshLogNoReplay() {
    FBoot boot(UniquePath("run-flow-continue"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    game::AppendRunEvent(registry, game::ERunEventKind::RoomCleared);
    CHECK(registry.ctx().get<game::FRunEventLog>().nextSeq == 2);
    int calls = 0;
    game::RegisterRunEventListener(registry, [&](const game::FRunEvent&) { ++calls; });

    CHECK(game::ContinueDungeonRun(registry));
    const auto& fresh = registry.ctx().get<game::FRunEventLog>();
    CHECK(fresh.tail.empty() && fresh.nextSeq == 1);
    CHECK(calls == 0);
    CHECK(registry.ctx().contains<game::FRunEventListeners>());  // listeners survive
}

void RunEventTailBounded() {
    FBoot boot(UniquePath("run-flow-tail"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    for (int index = 0; index < 100; ++index) {
        game::AppendRunEvent(registry, game::ERunEventKind::RewardPicked, "rune", index);
    }
    const auto& log = registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.size() == game::kRunEventTail);
    CHECK(log.tail.front().seq == 37);
    CHECK(log.tail.back().seq == 100);
    CHECK(log.nextSeq == 101);
}

void ListenersSurviveRunRecreation() {
    FBoot boot(UniquePath("run-flow-listeners"));
    auto& registry = boot.registry;
    int calls = 0;
    game::RegisterRunEventListener(registry, [&](const game::FRunEvent&) { ++calls; });

    game::StartRun(registry, kSeed, true);
    CHECK(registry.ctx().contains<game::FRunEventListeners>());
    game::AppendRunEvent(registry, game::ERunEventKind::RoomCleared);
    CHECK(calls == 1);

    game::StartRun(registry, kSeed + 1, true);
    CHECK(registry.ctx().contains<game::FRunEventListeners>());
    game::AppendRunEvent(registry, game::ERunEventKind::RoomCleared);
    CHECK(calls == 2);

    game::ShutdownRunEventLog(registry);
    CHECK(!registry.ctx().contains<game::FRunEventLog>());
    CHECK(!registry.ctx().contains<game::FRunEventListeners>());
}

void RewardPickedAppendsWithoutPhaseChange() {
    FBoot boot(UniquePath("run-flow-reward"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    CHECK(game::ResolveRoomVictory(registry, true).has_value());
    auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.phase == game::EDungeonPhase::Reward);
    const std::string saveBefore = ReadBytes(boot.runSave);

    game::AppendRunEvent(registry, game::ERunEventKind::RewardPicked, "rune.fire", 1);
    CHECK(run.phase == game::EDungeonPhase::Reward);
    CHECK(ReadBytes(boot.runSave) == saveBefore);  // the log is not saved
    const auto& log = registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.size() == 2u);  // RoomCleared + RewardPicked
    CHECK(log.tail.back().kind == game::ERunEventKind::RewardPicked && log.tail.back().id == "rune.fire");
}

void BossDefeatedExternalAppend() {
    FBoot boot(UniquePath("run-flow-boss"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    game::AppendRunEvent(registry, game::ERunEventKind::BossDefeated, "boss.dungeon", 0);
    const auto& log = registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.size() == 1u);
    CHECK(log.tail.front().kind == game::ERunEventKind::BossDefeated);
    CHECK(log.tail.front().seq == 1);
    CHECK(log.tail.front().depth == 1 && log.tail.front().room == 0);
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Battle);
}

void RunIntentRequestLastWins() {
    FBoot boot(UniquePath("run-flow-intent"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    CHECK(!registry.ctx().contains<game::FRunIntentRequest>());

    game::PostRunIntent(registry, game::ERunIntent::ContinueReward);
    CHECK(registry.ctx().get<game::FRunIntentRequest>().intent == game::ERunIntent::ContinueReward);
    game::PostRunIntent(registry, game::ERunIntent::LeaveCamp);
    CHECK(registry.ctx().get<game::FRunIntentRequest>().intent == game::ERunIntent::LeaveCamp);

    registry.ctx().erase<game::FRunIntentRequest>();  // the host consumes it
    CHECK(!registry.ctx().contains<game::FRunIntentRequest>());
}

// --- save wiring -------------------------------------------------------------------------------

void MenuRefreshDoesNotUnpackSections() {
    const std::string checkpoint = UniquePath("run-flow-menu");
    {
        entt::registry registry;
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        game::RegisterSaveSection(registry, CountingSection());
        CHECK(game::InitializeDungeon(registry, checkpoint));
        registry.ctx().emplace<FSectionCounter>(FSectionCounter{7});
        game::StartRun(registry, kSeed, true);
        CHECK(ReadJsonFile(checkpoint + ".run.json")["run"]["ext"].contains("counter"));
    }
    {
        entt::registry registry;
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        int unpackCalls = 0;
        game::FSaveSectionSerializer counter;
        counter.id = "counter";
        counter.version = 1;
        counter.pack = [](entt::registry&) { return json::object(); };
        counter.unpack = [&unpackCalls](entt::registry&, const json&) { ++unpackCalls; };
        game::RegisterSaveSection(registry, counter);

        // The boot refresh only validates: it must not unpack the section into the run.
        CHECK(game::InitializeDungeon(registry, checkpoint));
        CHECK(unpackCalls == 0);
        CHECK(registry.ctx().get<game::FDungeonSave>().exists);
        CHECK(!registry.ctx().contains<FSectionCounter>());
    }
    RemoveFiles(checkpoint);
}

void SavePacksSectionsAndLoadUnpacks() {
    const std::string checkpoint = UniquePath("run-flow-sections");
    {
        entt::registry registry;
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        game::RegisterSaveSection(registry, CountingSection());
        CHECK(game::InitializeDungeon(registry, checkpoint));
        registry.ctx().emplace<FSectionCounter>(FSectionCounter{5});
        game::StartRun(registry, kSeed, true);
        CHECK(game::SaveDungeonRun(registry));
    }
    {
        entt::registry registry;
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        game::RegisterSaveSection(registry, CountingSection());
        CHECK(game::InitializeDungeon(registry, checkpoint));
        CHECK(!registry.ctx().contains<FSectionCounter>());
        CHECK(game::ContinueDungeonRun(registry));
        CHECK(registry.ctx().get<FSectionCounter>().value == 5);
    }
    RemoveFiles(checkpoint);
}

void ClearPreservedOnNewRun() {
    FBoot boot(UniquePath("run-flow-preserved"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);

    auto document = game::DungeonRunToJson(registry.ctx().get<game::FDungeonRun>());
    document["run"]["ext"]["future_module"] = json{{"x", 1}};
    CHECK(game::UnpackSections(registry, document).has_value());
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.size() == 1u);

    game::StartRun(registry, kSeed, true);
    CHECK(registry.ctx().get<game::FSaveSectionRegistry>().preserved.empty());
    CHECK(!ReadJsonFile(boot.runSave)["run"]["ext"].contains("future_module"));
}

void PackFailureKeepsOldSave() {
    FBoot boot(UniquePath("run-flow-pack-fail"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);
    const std::string previous = ReadBytes(boot.runSave);

    game::FSaveSectionSerializer broken;
    broken.id = "broken";
    broken.version = 1;
    broken.pack = [](entt::registry&) -> json { throw std::runtime_error("cannot pack"); };
    broken.unpack = [](entt::registry&, const json&) {};
    game::RegisterSaveSection(registry, broken);

    const auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto seed = run.seed;
    const auto depth = run.depth;
    const auto room = run.room;
    const auto materials = run.materials;
    const auto phase = run.phase;
    const auto runes = run.runes;

    CHECK(!game::SaveDungeonRun(registry));
    CHECK(ReadBytes(boot.runSave) == previous);
    const auto& save = registry.ctx().get<game::FDungeonSave>();
    CHECK(!save.error.empty());
    CHECK(run.seed == seed && run.depth == depth && run.room == room && run.materials == materials &&
          run.phase == phase && run.runes == runes);
}

void UnpackFailureKeepsCurrentRun() {
    FBoot boot(UniquePath("run-flow-unpack-fail"));
    auto& registry = boot.registry;
    game::StartRun(registry, kSeed, true);

    game::FSaveSectionSerializer versioned;
    versioned.id = "failing";
    versioned.version = 1;
    versioned.pack = [](entt::registry&) { return json::object(); };
    versioned.unpack = [](entt::registry&, const json&) {};
    game::RegisterSaveSection(registry, versioned);

    const auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto seed = run.seed;
    const auto depth = run.depth;
    const auto room = run.room;
    const auto materials = run.materials;
    const auto phase = run.phase;
    const auto runes = run.runes;
    auto document = game::DungeonRunToJson(run);
    document["run"]["ext"]["failing"] = json{{"section_version", 5}};
    std::string error;
    CHECK(game::WriteSaveFile(boot.runSave, document, error));

    CHECK(!game::ContinueDungeonRun(registry));
    const auto& save = registry.ctx().get<game::FDungeonSave>();
    CHECK(!save.exists);
    CHECK(!save.error.empty());
    CHECK(run.seed == seed && run.depth == depth && run.room == room && run.materials == materials &&
          run.phase == phase && run.runes == runes);
}

void LegacyWrappersMatchRunFlow() {
    FBoot legacy(UniquePath("run-flow-legacy"));
    FBoot flow(UniquePath("run-flow-flow"));
    game::StartDungeonRun(legacy.registry, kSeed, true);
    game::StartRun(flow.registry, kSeed, true);

    game::CompleteDungeonBattle(legacy.registry, true);
    CHECK(game::ResolveRuneOffer(legacy.registry, 0));
    game::ContinueDungeonReward(legacy.registry);
    game::ChooseDungeonEvent(legacy.registry, false);
    game::CompleteDungeonBattle(legacy.registry, true);
    CHECK(game::ResolveRuneOffer(legacy.registry, 0));
    game::ContinueDungeonReward(legacy.registry);
    game::CompleteDungeonBattle(legacy.registry, true);
    CHECK(game::ResolveRuneOffer(legacy.registry, 0));
    game::ContinueDungeonReward(legacy.registry);
    game::LeaveDungeonCamp(legacy.registry);

    CHECK(game::ResolveRoomVictory(flow.registry, true).has_value());
    CHECK(game::ResolveRuneOffer(flow.registry, 0));
    CHECK(game::ApplyRunIntent(flow.registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ApplyRunIntent(flow.registry, game::ERunIntent::ChooseEventLoot).has_value());
    CHECK(game::ResolveRoomVictory(flow.registry, true).has_value());
    CHECK(game::ResolveRuneOffer(flow.registry, 0));
    CHECK(game::ApplyRunIntent(flow.registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ResolveRoomVictory(flow.registry, true).has_value());
    CHECK(game::ResolveRuneOffer(flow.registry, 0));
    CHECK(game::ApplyRunIntent(flow.registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(game::ApplyRunIntent(flow.registry, game::ERunIntent::LeaveCamp).has_value());

    const auto& a = legacy.registry.ctx().get<game::FDungeonRun>();
    const auto& b = flow.registry.ctx().get<game::FDungeonRun>();
    CHECK(a.phase == b.phase);
    CHECK(a.room == b.room);
    CHECK(a.depth == b.depth);
    CHECK(a.cleared == b.cleared);
    CHECK(a.materials == b.materials);
    CHECK(a.runes == b.runes);
    CHECK(a.advanceRequested == b.advanceRequested);
    CHECK(a.party.size() == b.party.size());
    for (std::size_t index = 0; index < a.party.size(); ++index) {
        CHECK(a.party[index].health == b.party[index].health);
        CHECK(a.party[index].experience == b.party[index].experience);
        CHECK(a.party[index].level == b.party[index].level);
        CHECK(a.party[index].passivePoints == b.party[index].passivePoints);
        CHECK(a.party[index].allocatedNodes == b.party[index].allocatedNodes);
        CHECK(a.party[index].skills.size() == b.party[index].skills.size());
    }

    // The saved run is the same run; only the wall-clock timestamp differs. The `log` section is the run's
    // per-run file identity (WP-19a: log_id + run_started_at), not run state — two independent runs must
    // not compare by it.
    const auto savedLegacy = ReadJsonFile(legacy.runSave);
    const auto savedFlow = ReadJsonFile(flow.runSave);
    auto legacyRun = savedLegacy["run"];
    auto flowRun = savedFlow["run"];
    legacyRun["ext"].erase("log");
    flowRun["ext"].erase("log");
    CHECK(legacyRun == flowRun);

    // The 2-argument form (fresh defaults to false) must keep compiling; it starts at checkpoint+1.
    FBoot defaulted(UniquePath("run-flow-legacy-default"));
    game::StartDungeonRun(defaulted.registry, kSeed);
    const auto& defaultedRun = defaulted.registry.ctx().get<game::FDungeonRun>();
    CHECK(defaultedRun.seed == kSeed);
    CHECK(defaultedRun.depth == 1);
    CHECK(defaultedRun.phase == game::EDungeonPhase::Battle);
    CHECK(std::filesystem::exists(defaulted.runSave));

    // A defeat: the wrapper and the flow agree on the lost run, its message and the deleted save.
    FBoot legacyDefeat(UniquePath("run-flow-legacy-defeat"));
    FBoot flowDefeat(UniquePath("run-flow-flow-defeat"));
    game::StartDungeonRun(legacyDefeat.registry, kSeed);
    game::StartRun(flowDefeat.registry, kSeed, false);
    CHECK(std::filesystem::exists(legacyDefeat.runSave));
    CHECK(std::filesystem::exists(flowDefeat.runSave));
    game::CompleteDungeonBattle(legacyDefeat.registry, false);
    CHECK(game::ResolveRoomVictory(flowDefeat.registry, false).has_value());
    const auto& lostLegacy = legacyDefeat.registry.ctx().get<game::FDungeonRun>();
    const auto& lostFlow = flowDefeat.registry.ctx().get<game::FDungeonRun>();
    CHECK(lostLegacy.phase == game::EDungeonPhase::Defeat);
    CHECK(lostFlow.phase == lostLegacy.phase);
    CHECK(!lostLegacy.message.empty());
    CHECK(lostLegacy.message == lostFlow.message);
    CHECK(!std::filesystem::exists(legacyDefeat.runSave));
    CHECK(!std::filesystem::exists(flowDefeat.runSave));
    CHECK(!legacyDefeat.registry.ctx().get<game::FDungeonSave>().exists);
    CHECK(!flowDefeat.registry.ctx().get<game::FDungeonSave>().exists);
}

} // namespace

int main() {
    return game_test::RunTests("run flow", {
        {"TransitionTableWalks", TransitionTableWalks},
        {"TransitionTableSingleWriter", TransitionTableSingleWriter},
        {"WrongPhaseIsRejected", WrongPhaseIsRejected},
        {"ExitTransitionsDropTheRun", ExitTransitionsDropTheRun},
        {"RunResultIsNeutral", RunResultIsNeutral},
        {"PhaseIdRoundTrip", PhaseIdRoundTrip},
        {"IsKnownPhaseIdCore", IsKnownPhaseIdCore},
        {"SavedPhaseNamesUnchanged", SavedPhaseNamesUnchanged},
        {"PhaseTypeStaysEnumUntilWP08", PhaseTypeStaysEnumUntilWP08},
        {"RunEventLogOrdered", RunEventLogOrdered},
        {"RunEventLogDeliversOnce", RunEventLogDeliversOnce},
        {"ListenerAppendIsRejected", ListenerAppendIsRejected},
        {"ThrowingListenerKeepsDelivering", ThrowingListenerKeepsDelivering},
        {"ListenerChangeIsInSameSave", ListenerChangeIsInSameSave},
        {"ContinueStartsFreshLogNoReplay", ContinueStartsFreshLogNoReplay},
        {"RunEventTailBounded", RunEventTailBounded},
        {"ListenersSurviveRunRecreation", ListenersSurviveRunRecreation},
        {"RewardPickedAppendsWithoutPhaseChange", RewardPickedAppendsWithoutPhaseChange},
        {"BossDefeatedExternalAppend", BossDefeatedExternalAppend},
        {"RunIntentRequestLastWins", RunIntentRequestLastWins},
        {"MenuRefreshDoesNotUnpackSections", MenuRefreshDoesNotUnpackSections},
        {"SavePacksSectionsAndLoadUnpacks", SavePacksSectionsAndLoadUnpacks},
        {"ClearPreservedOnNewRun", ClearPreservedOnNewRun},
        {"PackFailureKeepsOldSave", PackFailureKeepsOldSave},
        {"UnpackFailureKeepsCurrentRun", UnpackFailureKeepsCurrentRun},
        {"LegacyWrappersMatchRunFlow", LegacyWrappersMatchRunFlow},
    });
}
