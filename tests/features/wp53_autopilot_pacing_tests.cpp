// WP-53: autopilot pacing for recordings — `dwell=<N>` waits N frames on every decision screen before
// acting, `triumph` returns in triumph at a theme-cleared camp.
#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include <entt/entt.hpp>

#include "core/AssetManager.h"
#include "dev/FRunAutopilot.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "presentation/FPreparationView.h"

namespace {

using game::dev::EAutopilotStatus;
using game::dev::FRunAutopilot;
using game::dev::ParseAutopilotPolicy;

constexpr std::uint32_t kSeed = 4242;

std::string UniquePath(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return "out/" + std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
}

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

game::dev::FAutopilotPolicy Parse(std::string_view text) {
    const auto policy = ParseAutopilotPolicy(text);
    CHECK(policy.has_value());
    return policy.value_or(game::dev::FAutopilotPolicy{});
}

bool StartRequested(const entt::registry& registry) {
    const auto* request = registry.ctx().find<game::FMenuRequest>();
    return request != nullptr && request->value == game::EMenuRequest::Start;
}

void CampAfterDungeon(entt::registry& registry, int index) {
    registry.ctx().get<game::FDungeonCheckpoint>().cleared = index;
    game::InstallDungeonRunAt(registry, kSeed, index);
    const auto* sequence = game::FindRunSequence(registry);
    CHECK(sequence != nullptr);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Reward;
    run.room = sequence->BattleCountAt(sequence->PositionOf(index)) - 1;
    if (auto* rewards = registry.ctx().find<game::FRewards>()) rewards->offer.active = false;
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Camp);
}

int LastDungeonOfFirstTheme(const entt::registry& registry) {
    return static_cast<int>(game::FindRunSequence(registry)->themes[0].dungeons.size()) - 1;
}

bool PostedIntent(const entt::registry& registry, game::ERunIntent expected) {
    const auto* request = registry.ctx().find<game::FRunIntentRequest>();
    return request != nullptr && request->intent == expected;
}

void PolicyParsesDwellAndTriumph() {
    const auto standard = Parse(game::dev::kDefaultAutopilotPolicy);
    CHECK(standard.dwellFrames == 0);
    CHECK(!standard.returnInTriumph);
    const auto both = Parse("reward=card0,dwell=150,triumph,continue");
    CHECK(both.dwellFrames == 150);
    CHECK(both.returnInTriumph);
    CHECK(both.campContinue);
    CHECK(Parse("dwell=0").dwellFrames == 0);
    CHECK(Parse("dwell=600").dwellFrames == 600);
    CHECK(!ParseAutopilotPolicy("dwell=601").has_value());
    CHECK(!ParseAutopilotPolicy("dwell=-1").has_value());
    CHECK(!ParseAutopilotPolicy("dwell=abc").has_value());
    CHECK(!ParseAutopilotPolicy("dwell=").has_value());
    CHECK(!ParseAutopilotPolicy("triumph=1").has_value());
}

void DwellWaitsBeforeEachDecision() {
    entt::registry registry;
    FRunAutopilot pilot{Parse("dwell=3")};
    for (int call = 0; call < 3; ++call) {
        CHECK(pilot.tick(registry, "ember.title") == EAutopilotStatus::Running);
        CHECK(!StartRequested(registry));
    }
    CHECK(pilot.tick(registry, "ember.title") == EAutopilotStatus::Running);
    CHECK(StartRequested(registry));

    for (int call = 0; call < 3; ++call) {
        CHECK(pilot.tick(registry, "ember.prepare") == EAutopilotStatus::Running);
        CHECK(registry.ctx().find<game::FPreparationCommand>() == nullptr);
    }
    CHECK(pilot.tick(registry, "ember.prepare") == EAutopilotStatus::Running);
    const auto* command = registry.ctx().find<game::FPreparationCommand>();
    CHECK(command != nullptr && command->value == game::EPreparationCommand::Start);
}

void NoDwellActsAtOnce() {
    entt::registry registry;
    FRunAutopilot pilot{Parse(game::dev::kDefaultAutopilotPolicy)};
    CHECK(pilot.tick(registry, "ember.title") == EAutopilotStatus::Running);
    CHECK(StartRequested(registry));
}

void TriumphAtAThemeClearedCamp() {
    const auto atCamp = [](int dungeonIndex, std::string_view policyText, game::ERunIntent expected, bool cleared) {
        FBoot boot(UniquePath("wp53-camp"));
        auto& registry = boot.registry;
        const int index = dungeonIndex >= 0 ? dungeonIndex : LastDungeonOfFirstTheme(registry);
        CampAfterDungeon(registry, index);
        CHECK(game::IsThemeClearedCamp(registry) == cleared);
        FRunAutopilot pilot{Parse(policyText)};
        (void)pilot.tick(registry, game::kRunModalCamp);
        CHECK(PostedIntent(registry, expected));
    };
    atCamp(-1, "camp=none,triumph,continue", game::ERunIntent::ReturnInTriumph, true);
    atCamp(0, "camp=none,triumph,continue", game::ERunIntent::LeaveCamp, false);
    atCamp(-1, "camp=none,continue", game::ERunIntent::LeaveCamp, true);
}

void DwellDoesNotTripTheStuckGuard() {
    entt::registry registry;
    FRunAutopilot pilot{Parse("dwell=600")};
    for (int call = 0; call < 600; ++call) {
        CHECK(pilot.tick(registry, "ember.title") == EAutopilotStatus::Running);
    }
    CHECK(!StartRequested(registry));
    CHECK(pilot.tick(registry, "ember.title") == EAutopilotStatus::Running);
    CHECK(StartRequested(registry));
}

} // namespace

int main() {
    return game_test::RunTests("wp53 autopilot pacing", {
        {"PolicyParsesDwellAndTriumph", PolicyParsesDwellAndTriumph},
        {"DwellWaitsBeforeEachDecision", DwellWaitsBeforeEachDecision},
        {"NoDwellActsAtOnce", NoDwellActsAtOnce},
        {"TriumphAtAThemeClearedCamp", TriumphAtAThemeClearedCamp},
        {"DwellDoesNotTripTheStuckGuard", DwellDoesNotTripTheStuckGuard},
    });
}
