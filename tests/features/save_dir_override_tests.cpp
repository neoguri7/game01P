// GAME01P_SAVE_DIR: the storage-directory override that keeps tests and lanes off the user's real save.
//
// InitializeDungeon with an empty checkpoint path normally resolves SDL_GetPrefPath("Game01P","EmberDungeon")
// (%APPDATA%\Game01P\EmberDungeon). When GAME01P_SAVE_DIR is set, that directory is used instead, so a test or
// a lane run can never read or write the player's save. This file pins the contract:
//   * set -> checkpoint.path and save.path live inside the override directory, which is created on demand;
//   * unset or empty -> the override is ignored (the preference directory is used, untouched here);
//   * an override that cannot be created is not silently replaced by the preference directory.
//
// Conventions follow tests/features/fixture_guard_tests.cpp: one file, the shared TestSupport block, cwd is the
// repository root and the scratch files live under the system temp directory.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kSaveDirEnv = "GAME01P_SAVE_DIR";

/// Sets an environment variable. On Windows an empty value removes the variable (the OS has no empty values).
void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

/// Removes an environment variable.
void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

std::optional<std::string> GetEnv(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return std::nullopt;
    std::string result(value);
    std::free(value);
    return result;
#else
    if (const char* value = std::getenv(name)) return std::string(value);
    return std::nullopt;
#endif
}

/// Restores GAME01P_SAVE_DIR when the test ends, so a lane's inherited value survives the test process.
struct FSaveDirEnvGuard {
    std::optional<std::string> previous{GetEnv(kSaveDirEnv)};

    ~FSaveDirEnvGuard() {
        if (previous) SetEnv(kSaveDirEnv, previous->c_str());
        else UnsetEnv(kSaveDirEnv);
    }
};

/// A unique scratch directory under the system temp dir; the repository root stays the working directory.
struct FTempDir {
    std::filesystem::path path;

    FTempDir() {
        static std::uint32_t serial = 0;
        path = std::filesystem::temp_directory_path() /
             ("game01p-save-dir-tests-" + std::to_string(++serial) + "-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~FTempDir() {
        std::error_code code;
        std::filesystem::remove_all(path, code);
    }
};

/// True when `path` resolves inside `root`. Both may be relative and neither needs to exist; the comparison is
/// lexical after weakly_canonical, so mixed separators and drive-letter case do not matter.
bool IsInside(const std::string& path, const std::filesystem::path& root) {
    if (path.empty()) return false;
    std::error_code code;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, code);
    if (code) return false;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, code);
    if (code) return false;
    const auto relative = canonicalPath.lexically_relative(canonicalRoot);
    return !relative.empty() && *relative.begin() != "..";
}

/// Boots the dungeon with default storage (empty checkpoint path) and returns the computed save paths.
struct FStoragePaths {
    std::string checkpoint;
    std::string save;
    std::string checkpointError;
};

FStoragePaths BootDefaultStorage() {
    auto loaded = game_test::LoadTestRegistry(TestAssetRoots(), "");
    CHECK(loaded.content);
    CHECK(loaded.dungeon);
    const auto& checkpoint = loaded.registry.ctx().get<game::FDungeonCheckpoint>();
    const auto& save = loaded.registry.ctx().get<game::FDungeonSave>();
    return {checkpoint.path, save.path, checkpoint.error};
}

/// The preference directory the game uses when no override is set. SDL_GetPrefPath creates it, exactly as
/// InitializeDungeon already does, so this adds no new side effect; the caller owns the returned path.
std::filesystem::path PreferenceDirectory() {
    char* path = SDL_GetPrefPath("Game01P", "EmberDungeon");
    if (path == nullptr) {
        throw std::runtime_error(std::string("SDL_GetPrefPath(\"Game01P\", \"EmberDungeon\") failed: ") + SDL_GetError());
    }
    std::filesystem::path result(path);
    SDL_free(path);
    return result;
}

/// The default boot must resolve the preference directory. Only the computed paths are compared: nothing here
/// exists(), reads or writes them.
void CheckPreferencePaths(const FStoragePaths& paths) {
    const auto prefDir = PreferenceDirectory();
    std::error_code code;
    const auto expectedCheckpoint = std::filesystem::weakly_canonical(prefDir / "checkpoint.json", code);
    CHECK(!code);
    const auto expectedSave = std::filesystem::weakly_canonical(prefDir / "run_save.json", code);
    CHECK(!code);
    const auto actualCheckpoint = std::filesystem::weakly_canonical(paths.checkpoint, code);
    CHECK(!code);
    const auto actualSave = std::filesystem::weakly_canonical(paths.save, code);
    CHECK(!code);
    CHECK(actualCheckpoint == expectedCheckpoint);
    CHECK(actualSave == expectedSave);
}

/// Boots once with the override live, then again after `clear` removes it, and pins both contracts. The second
/// boot runs in the same process on purpose: a cached environment (the SDL_getenv trap) would keep the override
/// and fail the "not inside" checks.
void BootWithOverrideThenWithout(void (*clear)()) {
    FSaveDirEnvGuard guard;
    FTempDir temp;
    const auto override = temp.path / "override";
    SetEnv(kSaveDirEnv, override.string().c_str());
    const auto overridden = BootDefaultStorage();
    CHECK(overridden.checkpointError.empty());
    CHECK(IsInside(overridden.checkpoint, override));  // the variable was live for this boot
    CHECK(IsInside(overridden.save, override));

    clear();
    const auto defaults = BootDefaultStorage();
    CHECK(!IsInside(defaults.checkpoint, override));
    CHECK(!IsInside(defaults.save, override));
    CheckPreferencePaths(defaults);
    // No save is triggered here: the test never calls StartDungeonRun/SaveDungeonRun.
}

void OverrideDirectoryIsUsed() {
    FSaveDirEnvGuard guard;
    FTempDir temp;
    const auto override = temp.path / "nested" / "save-dir";  // does not exist yet
    SetEnv(kSaveDirEnv, override.string().c_str());

    const auto paths = BootDefaultStorage();
    CHECK(!paths.checkpoint.empty());
    CHECK(!paths.save.empty());
    CHECK(paths.checkpointError.empty());
    CHECK(std::filesystem::exists(override));
    CHECK(IsInside(paths.checkpoint, override));
    CHECK(IsInside(paths.save, override));
}

void UnsetUsesPreferenceDirectory() {
    BootWithOverrideThenWithout([] { UnsetEnv(kSaveDirEnv); });
}

void EmptyValueBehavesLikeUnset() {
    // On Windows an empty value removes the variable; on POSIX it stays empty. Both mean "no override".
    BootWithOverrideThenWithout([] { SetEnv(kSaveDirEnv, ""); });
}

void UncreatableOverrideDoesNotFallBack() {
    FSaveDirEnvGuard guard;
    FTempDir temp;
    const auto blocker = temp.path / "blocker";
    {
        std::ofstream file(blocker);
        CHECK(file.good());
    }
    const auto override = blocker / "child";  // a file is in the way, so create_directories fails
    SetEnv(kSaveDirEnv, override.string().c_str());

    const auto paths = BootDefaultStorage();
    CHECK(paths.checkpoint.empty());
    CHECK(paths.save.empty());
    CHECK(!paths.checkpointError.empty());
}

} // namespace

int main() {
    return game_test::RunTests("save dir override", {
        {"override directory is used", OverrideDirectoryIsUsed},
        {"unset uses the preference directory", UnsetUsesPreferenceDirectory},
        {"empty value behaves like unset", EmptyValueBehavesLikeUnset},
        {"uncreatable override does not fall back", UncreatableOverrideDoesNotFallBack},
    });
}
