// WP-21a: promote the FDungeonRun.cpp anonymous-namespace `RunMemberUnitTemplate` to the public
// `ResolveRunMemberUnit` — the single memberId → unit-template resolver every call site shares (WP-21 §1).
// This test pins the behaviour-identical export: the authored dungeon party still resolves through the
// same `data/dungeon party[].id → unit` lookup, the fresh-run max-HP call site resolves through the
// export, and the nullptr contract (unknown id, un-booted registry) is unchanged. Roster awareness
// arrives with 21b — nothing here reads roster.json.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "gameplay/data/FContentRegistry.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 2101;

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string UniquePath(const char* stem) {
    return "out/" + UniqueStem(stem) + ".json";
}

/// A booted registry with its own checkpoint and run-save slot (the camp_tests FBoot pattern): every file
/// the test writes lives under out/ and is removed again by the destructor.
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

    FBoot(const FBoot&) = delete;
    FBoot& operator=(const FBoot&) = delete;
};

void ResolveExportIsBehaviorIdentical() {
    FBoot boot(UniquePath("preparation-resolve"));

    // The authored dungeon party is the resolver's source of truth: every row maps to its content row, and
    // the pointer is the very row `content.units.find` returns — one lookup path, no copy.
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const json& party = game::DungeonDefinition(boot.registry).at("party");
    CHECK(!party.empty());
    for (const json& row : party) {
        const std::string id = row.at("id").get<std::string>();
        const std::string unitId = row.at("unit").get<std::string>();
        const game::gameplay::FUnitContent* unit = game::ResolveRunMemberUnit(boot.registry, id);
        CHECK(unit != nullptr);
        CHECK(unit == content.units.find(unitId));
        CHECK(unit->id == unitId);
    }

    // A fresh run's members resolve to those same pointers (run.party is authored from the same rows), and
    // the existing max-HP call site goes through the export: a fresh member has no bonuses or nodes, so
    // its maximum is the unit template's base.
    game::StartDungeonRun(boot.registry, kSeed, true);
    const auto& run = boot.registry.ctx().get<game::FDungeonRun>();
    CHECK(run.party.size() == party.size());
    for (const game::FRunMember& member : run.party) {
        const json* row = nullptr;
        for (const json& candidate : party) {
            if (candidate.at("id").get<std::string>() == member.id) {
                row = &candidate;
                break;
            }
        }
        CHECK(row != nullptr);
        const game::gameplay::FUnitContent* unit = game::ResolveRunMemberUnit(boot.registry, member.id);
        CHECK(unit != nullptr);
        CHECK(unit == content.units.find(row->at("unit").get<std::string>()));
        CHECK(game::DungeonMemberMaxHealth(boot.registry, member) == static_cast<int>(unit->maxHealth));
    }

    // Unknown member id: nullptr, never a throw (callers keep their existing warning/fallback).
    CHECK(game::ResolveRunMemberUnit(boot.registry, "party.ghost") == nullptr);

    // A registry that never booted the content: nullptr — the resolver must not assume its context.
    entt::registry bare;
    CHECK(game::ResolveRunMemberUnit(bare, "party.warrior") == nullptr);
}

} // namespace

int main() {
    // CTest sets GAME01P_SAVE_DIR per feature test; a direct run does not. Fall back to out/test-saves so
    // a run can never touch %APPDATA%\Game01P\EmberDungeon.
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path =
            (std::filesystem::current_path() / "out" / "test-saves" / "preparation_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("preparation", {
        {"ResolveExportIsBehaviorIdentical", ResolveExportIsBehaviorIdentical},
    });
}
