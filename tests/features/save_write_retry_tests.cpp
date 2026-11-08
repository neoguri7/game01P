// WP-00b flake fix: a save survives another process briefly holding the save file.
//
// Windows refuses MoveFileExW(REPLACE_EXISTING) while any handle is open on the target (ERROR_ACCESS_DENIED) and
// DeleteFileW while a handle without FILE_SHARE_DELETE is open (ERROR_SHARING_VIOLATION). An on-access antivirus
// scan or a second reader holds a freshly written file for a few milliseconds, so a single rename made
// ResolveRoomVictory / SaveDungeonRun fail intermittently (reward_offer_tests line 166, run_flow_tests,
// run_modal_tests; docs/expansion/reviews/flake-diag.md). WriteSaveFile and RemoveSaveFile retry those codes
// briefly. Here the "other process" is a std::ifstream held for a short time on a helper thread: MSVC opens it
// with share read|write and no share delete, the same as the scanner that reproduced the flake.
//
// Off Windows a rename or unlink over an open file always succeeds, so these tests pass trivially there.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <system_error>
#include <thread>

#include <nlohmann/json.hpp>

#include "gameplay/dungeon/FDungeonSave.h"

namespace {

using namespace std::chrono_literals;

/// A unique scratch directory under the system temp directory, removed when it goes out of scope.
struct FTempDir {
    std::filesystem::path path;

    FTempDir() {
        static std::uint32_t serial = 0;
        path = std::filesystem::temp_directory_path() /
               ("game01p-save-retry-tests-" + std::to_string(++serial) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }

    ~FTempDir() {
        std::error_code code;
        std::filesystem::remove_all(path, code);
    }

    FTempDir(const FTempDir&) = delete;
    FTempDir& operator=(const FTempDir&) = delete;
};

/// Opens `file` for reading on another thread, signals once it is open, holds it for `hold`, then closes it.
std::future<void> HoldOpen(const std::filesystem::path& file, std::chrono::milliseconds hold, std::promise<void>& opened) {
    return std::async(std::launch::async, [file, hold, &opened] {
        std::ifstream reader(file, std::ios::binary);
        opened.set_value();
        std::this_thread::sleep_for(hold);
    });
}

void ReplaceSucceedsWhileTargetIsBrieflyOpen() {
    FTempDir dir;
    const auto target = dir.path / "run_save.json";
    std::string error;
    CHECK(game::WriteSaveFile(target, nlohmann::json{{"value", 1}}, error));

    std::promise<void> opened;
    auto holder = HoldOpen(target, 40ms, opened);
    opened.get_future().wait();
    const bool written = game::WriteSaveFile(target, nlohmann::json{{"value", 2}}, error);
    holder.get();
    CHECK(written);
    CHECK(error.empty());

    const auto saved = game::ReadSaveFile(target);
    CHECK(saved.has_value());
    CHECK(saved->at("value") == 2);
    CHECK(!std::filesystem::exists(dir.path / "run_save.json.tmp"));
}

void RemoveSucceedsWhileTargetIsBrieflyOpen() {
    FTempDir dir;
    const auto target = dir.path / "run_save.json";
    std::string error;
    CHECK(game::WriteSaveFile(target, nlohmann::json{{"value", 1}}, error));

    std::promise<void> opened;
    auto holder = HoldOpen(target, 40ms, opened);
    opened.get_future().wait();
    std::error_code code;
    game::RemoveSaveFile(target, code);
    holder.get();
    CHECK(!code);
    CHECK(!std::filesystem::exists(target));
}

void RemovingAMissingFileIsNotAnError() {
    FTempDir dir;
    std::error_code code;
    game::RemoveSaveFile(dir.path / "never-written.json", code);
    CHECK(!code);
}

void PermanentFailureStillFails() {
    FTempDir dir;
    // A directory sits where the save file should go. Windows reports that as access denied too, so the retry
    // runs out (~0.6 s) and the write must still report the failure and leave no temp file behind.
    const auto target = dir.path / "occupied";
    std::filesystem::create_directories(target / "child");
    std::string error;
    CHECK(!game::WriteSaveFile(target, nlohmann::json{{"value", 1}}, error));
    CHECK(!error.empty());
    CHECK(!std::filesystem::exists(dir.path / "occupied.tmp"));
}

} // namespace

int main() {
    return game_test::RunTests("save write retry", {
        {"ReplaceSucceedsWhileTargetIsBrieflyOpen", ReplaceSucceedsWhileTargetIsBrieflyOpen},
        {"RemoveSucceedsWhileTargetIsBrieflyOpen", RemoveSucceedsWhileTargetIsBrieflyOpen},
        {"RemovingAMissingFileIsNotAnError", RemovingAMissingFileIsNotAnError},
        {"PermanentFailureStillFails", PermanentFailureStillFails},
    });
}
