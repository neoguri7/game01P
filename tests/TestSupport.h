#pragma once

#include <string>
#include <vector>

// Test asset roots. Tests run with the repository root as their working directory (see CMakeLists.txt), so
// these relative paths resolve the same way the game's asset boundary does.
//
// 1st root = the frozen test fixture (tests/fixtures/assets/data): the balance/content snapshot the unit
//            tests were written against. A change to the shipped balance data never moves a test's
//            expectations; when the fixture should change, that is a deliberate edit.
// 2nd root = the shipped assets: presentation data (sprites, UI layout, animation clips) and anything else
//            the fixture does not carry. Tests that validate the shipped content itself use {"assets"}.
inline constexpr const char* kTestFixtureRoot = "tests/fixtures/assets";
inline constexpr const char* kShipAssetRoot = "assets";

inline std::vector<std::string> TestAssetRoots() {
    return {kTestFixtureRoot, kShipAssetRoot};
}

// The shipped content only, for the few tests that validate what ships rather than the frozen fixture.
inline std::vector<std::string> ShipAssetRoots() {
    return {kShipAssetRoot};
}

// A path to one file inside the frozen fixture, for tests that derive a modified document from it.
inline std::string FixtureDataPath(const std::string& file) {
    return std::string(kTestFixtureRoot) + "/data/" + file;
}

// ---------------------------------------------------------------------------------------------
// Shared feature-test support (B6) — opt-in.
//
// DungeonTests.cpp and AiTests.cpp keep their own Check/CHECK and runner (they predate this header and rewriting
// them would be churn), so this block only compiles when a feature test asks for it:
//
//     #define GAME01P_TEST_SUPPORT_CHECK
//     #include "../TestSupport.h"
//
// That keeps every tests/features/*_tests.cpp file to one test function plus one RunTests() call: no per-file
// main/Check boilerplate. The CHECK macro is also #ifndef-guarded so a file that defines its own first still wins.
// ---------------------------------------------------------------------------------------------
#ifdef GAME01P_TEST_SUPPORT_CHECK

#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <entt/entt.hpp>

#include "core/AssetManager.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/dungeon/FDungeonRun.h"

namespace game_test {

inline void Check(bool condition, const char* expression, int line) {
    if (!condition) throw std::runtime_error(std::string("Line ") + std::to_string(line) + ": " + expression);
}

#ifndef CHECK
#define CHECK(expression) ::game_test::Check((expression), #expression, __LINE__)
#endif

using FTest = std::pair<const char*, void (*)()>;

// Runs every named test, prints [ ok ] / [FAIL] lines and a summary, returns 0 when all passed.
inline int RunTests(std::string_view suite, std::initializer_list<FTest> tests) {
    int failures = 0;
    for (const auto& [name, run] : tests) {
        try {
            run();
            std::cout << "[ ok ] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    const int total = static_cast<int>(tests.size());
    std::cout << suite << ": " << (total - failures) << "/" << total << " passed\n";
    return failures == 0 ? 0 : 1;
}

/// A registry with the content + AI library and the dungeon loaded. `false` means a loader rejected the roots;
/// the reason is already in the log.
struct FTestRegistry {
    entt::registry registry;
    bool content{false};
    bool dungeon{false};
};

[[nodiscard]] inline FTestRegistry LoadTestRegistry(std::vector<std::string> roots, const std::string& checkpoint) {
    FTestRegistry loaded;
    game::FAssetManager::Initialize(loaded.registry, std::move(roots));
    loaded.content = game::gameplay::FGameplayServices::Initialize(loaded.registry);
    if (loaded.content) {
        loaded.dungeon = game::InitializeDungeon(loaded.registry, checkpoint);
    }
    return loaded;
}

} // namespace game_test

#endif // GAME01P_TEST_SUPPORT_CHECK
