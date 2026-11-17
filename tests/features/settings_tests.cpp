// WP-24a settings tests: FSettings (settings.json load/save/isolation), the BattleAnimationSystem speed
// and skip path, and the ParseBattleAnim loop-clip warning. The battle-level test drives three full
// auto-played fights (1x / 2x / skip) on one pinned seed and asserts the rules outcome is identical.
#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <array>
#include <chrono>
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
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <SDL3/SDL.h>

#include "core/AnimationLibrary.h"
#include "core/AssetManager.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"
#include "core/events/FEventBus.h"
#include "ecs/AnimationFunctions.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/BattleAnimationSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/FBattleFlowSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FSettings.h"
#include "states/FEmberBattleState.h"

namespace {

using game::FAnimClip;
using game::FAnimClipId;
using game::FBattleAnimData;
using game::FBattleEvent;
using game::FBattleOutcome;
using game::FCombatRuntime;
using game::FHealth;
using game::FPresentationGate;
using game::FSettings;
using game::FSettingsLocation;
using game::FUnitAnim;
using game::FUnitAnimSet;

/// The clip suffix per EUnitAnim index (idle, move, attack, hit, death) — the same order as FBattleAnim.cpp.
constexpr std::array<const char*, game::kUnitAnimCount> kActionSuffix{"idle", "walk", "attack", "hit", "death"};

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

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

/// The isolated save directory main() committed; every file this suite writes lives under it.
[[nodiscard]] std::filesystem::path IsolatedDir() {
    const char* dir = std::getenv(game::kSettingsSaveDirEnv);
    CHECK(dir != nullptr && *dir != '\0');
    return std::filesystem::path(dir);
}

/// A private, freshly emptied subdirectory of the isolated dir.
[[nodiscard]] std::filesystem::path FreshDir(const char* name) {
    const std::filesystem::path dir = IsolatedDir() / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    CHECK(!error);
    return dir;
}

/// Routes LOG_WARN/LOG_ERROR into a string stream so a test can assert a warning without files or stderr.
class FLogCapture {
public:
    FLogCapture() {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream);
        sink->set_pattern("%v");
        logger_ = std::make_shared<spdlog::logger>("settings-test", sink);
        logger_->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger_);
    }
    ~FLogCapture() { SetCurrentGameLogger(nullptr); }
    FLogCapture(const FLogCapture&) = delete;
    FLogCapture& operator=(const FLogCapture&) = delete;

    [[nodiscard]] std::string text() const { return stream.str(); }
    void clear() { stream.str(""); stream.clear(); }

private:
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger_;
};

/// ImGui-free animation fixture: assets + content + a software renderer (the animation library loads its
/// textures through it) + the battle animation contexts. `roots` is customizable so a test can shadow
/// anim/clips with a private document.
struct FAnimFixture {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, &SDL_DestroySurface};
    FRendererPtr renderer{nullptr};
    entt::registry registry;

    explicit FAnimFixture(std::vector<std::string> roots = TestAssetRoots()) {
        game::FAssetManager::Initialize(registry, std::move(roots));
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        surface.reset(SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA8888));
        CHECK(surface != nullptr);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        CHECK(renderer != nullptr);
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        registry.ctx().emplace<SDL_Renderer*>(renderer.get());
        CHECK(game::FAnimationLibrary::Initialize(registry));
        // An empty animation document keeps UpdateBattleAnimations running (it bails out without one);
        // BuildWarriorSet() replaces it with the hand-built set for the tests that animate a unit.
        registry.ctx().emplace<FBattleAnimData>();
        game::BeginBattleAnim(registry);
    }

    /// A hand-built one-set animation document on the shipped warrior clips: the skip tests drive the
    /// animation system directly, so they need neither units/skills data nor battle_anim.json validation.
    void BuildWarriorSet() {
        const auto* library = game::FAnimationLibrary::Find(registry);
        CHECK(library != nullptr);
        FBattleAnimData data;
        FUnitAnimSet set;
        for (std::size_t action = 0; action < game::kUnitAnimCount; ++action) {
            const auto clip = library->findClip(std::string("unit.warrior.") + kActionSuffix[action]);
            CHECK(clip.has_value());
            set.clips[action] = *clip;
        }
        set.impactFrame = 1;
        data.sets.push_back(set);
        data.setByUnit.emplace("char.warrior", 0);
        const auto fallback = library->findClip("fx.slash");
        CHECK(fallback.has_value());
        data.fallback.clip = *fallback;
        registry.ctx().insert_or_assign<FBattleAnimData>(std::move(data));
    }

    [[nodiscard]] entt::entity MakeWarrior() {
        const auto* library = game::FAnimationLibrary::Find(registry);
        const auto& data = registry.ctx().get<FBattleAnimData>();
        FUnitAnim anim;
        anim.set = 0;
        const FAnimClipId idle = data.sets[0].clips[static_cast<std::size_t>(game::EUnitAnim::Idle)];
        if (const FAnimClip* clip = library->get(idle)) game::ecs::StartClip(anim.player, idle, *clip);
        const entt::entity unit = registry.create();
        registry.emplace<FUnitAnim>(unit, std::move(anim));
        return unit;
    }
};

/// The presentation `animations` must not hold the gate after one update when skip is on.
void ExpectSettled(entt::registry& registry) {
    CHECK(game::ecs::PresentationBusyCount(registry) == 0);
    CHECK(registry.ctx().get<FPresentationGate>().busy == 0);
}

// ---------------------------------------------------------------------------------------------
// Settings file (load / save / path isolation)
// ---------------------------------------------------------------------------------------------

void DefaultIsOneX() {
    const auto dir = FreshDir("default-1x");
    FSavedEnv override(dir);
    entt::registry registry;
    game::LoadSettings(registry);
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedNormal);
    CHECK(!game::CurrentSettings(registry).skipAnimations);
    CHECK(registry.ctx().get<FSettingsLocation>().path == std::optional(dir / game::kSettingsFileName));
}

void SettingsUseIsolatedDir() {
    const auto dir = FreshDir("isolated-dir");
    FSavedEnv override(dir);
    entt::registry registry;
    game::LoadSettings(registry);
    game::SetAnimSpeed(registry, game::kAnimSpeedFast);
    const auto expected = dir / game::kSettingsFileName;
    CHECK(registry.ctx().get<FSettingsLocation>().path == std::optional(expected));
    CHECK(std::filesystem::exists(expected));
    int files = 0;  // nothing else was written into the directory
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        (void)entry;
        ++files;
    }
    CHECK(files == 1);
}

void SettingsRoundTrip() {
    const auto dir = FreshDir("round-trip");
    {
        FSavedEnv override(dir);
        entt::registry registry;
        game::LoadSettings(registry);
        game::SetAnimSpeed(registry, game::kAnimSpeedFast);
        game::SetSkipAnimations(registry, true);
        CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedFast);
        CHECK(game::CurrentSettings(registry).skipAnimations);
    }
    {  // a fresh boot reads the same file back
        FSavedEnv override(dir);
        entt::registry reloaded;
        game::LoadSettings(reloaded);
        CHECK(game::CurrentSettings(reloaded).animSpeed == game::kAnimSpeedFast);
        CHECK(game::CurrentSettings(reloaded).skipAnimations);
    }
    std::ifstream file(dir / game::kSettingsFileName);
    CHECK(file.good());
    const auto document = nlohmann::json::parse(file);
    CHECK(document.at("schema_version").get<int>() == game::kSettingsSchemaVersion);
    CHECK(document.at("anim_speed").get<float>() == game::kAnimSpeedFast);
    CHECK(document.at("skip_animations").get<bool>());
}

void CorruptSettingsFallsBack() {
    const auto dir = FreshDir("corrupt");
    {
        std::ofstream file(dir / game::kSettingsFileName);
        file << "{ this is not json";
    }
    FSavedEnv override(dir);
    entt::registry registry;
    game::LoadSettings(registry);  // warns, keeps the defaults, fails nothing
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedNormal);
    CHECK(!game::CurrentSettings(registry).skipAnimations);
    CHECK(registry.ctx().get<FSettingsLocation>().path.has_value());  // the path itself was fine
}

void InvalidSpeedClampsToOne() {
    const auto dir = FreshDir("invalid-speed");
    const auto write = [&](double speed) {
        std::string error;
        CHECK(game::WriteSaveFile(dir / game::kSettingsFileName,
                                  nlohmann::json{{"schema_version", game::kSettingsSchemaVersion},
                                                 {"anim_speed", speed}, {"skip_animations", true}},
                                  error));
    };
    FSavedEnv override(dir);
    write(1.5);
    {
        entt::registry registry;
        game::LoadSettings(registry);
        CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedNormal);
        CHECK(game::CurrentSettings(registry).skipAnimations);  // only the speed was refused
    }
    write(0.25);
    {
        entt::registry registry;
        game::LoadSettings(registry);
        CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedNormal);
    }
}

void SettingsToggleAPI() {
    const auto dir = FreshDir("toggle-api");
    FSavedEnv override(dir);
    entt::registry registry;
    game::LoadSettings(registry);
    game::SetAnimSpeed(registry, game::kAnimSpeedFast);
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedFast);
    game::ToggleAnimSpeed(registry);
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedNormal);
    game::ToggleAnimSpeed(registry);
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedFast);
    game::SetAnimSpeed(registry, 1.5f);  // outside the allowed set: clamps to 1x and warns
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedNormal);

    entt::registry reloaded;  // every setter saved immediately; the last value is on disk
    game::LoadSettings(reloaded);
    CHECK(game::CurrentSettings(reloaded).animSpeed == game::kAnimSpeedNormal);
}

void SettingsNeverFallBackWhenOverrideSet() {
    const auto root = FreshDir("never-fallback");
    // A FILE where the override wants its directory: create_directories must fail...
    const auto blocker = root / "blocker";
    {
        std::ofstream file(blocker);
        file << "not a directory";
    }
    const auto overrideDir = blocker / "sub";
    // ...and the preference-directory provider must not be probed for a fallback.
    int probes = 0;
    const auto path = game::ResolveSettingsPath(overrideDir, {}, [&probes]() -> std::optional<std::filesystem::path> {
        ++probes;
        return std::nullopt;
    });
    CHECK(!path.has_value());
    CHECK(probes == 0);

    // LoadSettings through the same failing override: in-memory defaults, nothing written anywhere.
    FSavedEnv env(overrideDir);
    entt::registry registry;
    game::LoadSettings(registry);
    CHECK(!registry.ctx().get<FSettingsLocation>().path.has_value());
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedNormal);
    game::SetAnimSpeed(registry, game::kAnimSpeedFast);  // setters still work in memory
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedFast);
    CHECK(!std::filesystem::exists(overrideDir));  // the directory never came to be
}

void SavedSpeedAppliesWithoutOpeningSettings() {
    // The saved 2x reaches the system through the boot load only; the settings view is never drawn here.
    const auto dir = FreshDir("saved-speed");
    std::string writeError;
    CHECK(game::WriteSaveFile(dir / game::kSettingsFileName,
                              nlohmann::json{{"schema_version", 1}, {"anim_speed", 2.0}, {"skip_animations", false}},
                              writeError));

    FAnimFixture fixture;
    auto& registry = fixture.registry;
    {
        FSavedEnv override(dir);
        game::LoadSettings(registry);
    }
    CHECK(game::CurrentSettings(registry).animSpeed == game::kAnimSpeedFast);
    CHECK(!game::CurrentSettings(registry).skipAnimations);

    // Delete the file before the system runs: the 2x below can then only come from the loaded ctx
    // value — the system itself does no IO at all (the acceptance criterion).
    std::error_code removeError;
    std::filesystem::remove(dir / game::kSettingsFileName, removeError);
    CHECK(!removeError);
    CHECK(!std::filesystem::exists(dir / game::kSettingsFileName));

    bool ran = false;
    registry.ctx().get<game::FBattleTimeline>().steps.push_back(game::FBattleTimeline::FStep{
        0.03f, [&ran](entt::registry&) { ran = true; }});
    game::ecs::BattleAnimationSystem animations;
    animations.update(registry, 0.02f);
    CHECK(ran);  // 0.02 * 2.0 advances past the 0.03 delay, with no view ever opened

    // The same step at the default 1x does not fire on a 0.02 frame: the 2x really came from the file.
    FAnimFixture slow;
    auto& plain = slow.registry;
    {
        FSavedEnv override(dir / "no-file");  // an empty override directory: no settings.json there
        game::LoadSettings(plain);
    }
    CHECK(game::CurrentSettings(plain).animSpeed == game::kAnimSpeedNormal);
    bool early = false;
    plain.ctx().get<game::FBattleTimeline>().steps.push_back(game::FBattleTimeline::FStep{
        0.03f, [&early](entt::registry&) { early = true; }});
    game::ecs::BattleAnimationSystem plainAnimations;
    plainAnimations.update(plain, 0.02f);
    CHECK(!early);
    CHECK(plain.ctx().get<game::FBattleTimeline>().steps.size() == 1);
}

// ---------------------------------------------------------------------------------------------
// Skip through BattleAnimationSystem
// ---------------------------------------------------------------------------------------------

void SkipSettlesImmediately() {
    FAnimFixture fixture;
    fixture.BuildWarriorSet();
    auto& registry = fixture.registry;
    const entt::entity unit = fixture.MakeWarrior();
    auto& anim = registry.get<FUnitAnim>(unit);
    anim.route.push_back({1.f, 1.f});
    anim.route.push_back({2.f, 2.f});
    registry.ctx().get<game::FBattleTimeline>().steps.push_back(
        game::FBattleTimeline::FStep{0.25f, [](entt::registry&) {}});
    game::FActiveEffect effect;
    effect.duration = 0.4f;  // a projectile in flight: pure decoration, still part of the gate count
    registry.ctx().get<game::FBattleEffects>().active.push_back(effect);
    registry.ctx().emplace<FSettings>(FSettings{game::kAnimSpeedFast, true});

    game::ecs::BattleAnimationSystem animations;
    animations.update(registry, 1.f / 60.f);

    CHECK(!animations.skipBoundWarned);
    ExpectSettled(registry);
    CHECK(registry.ctx().get<game::FBattleTimeline>().steps.empty());
    CHECK(registry.ctx().get<game::FBattleEffects>().active.empty());
    CHECK(anim.route.empty() && anim.visual.x == 2.f && anim.visual.y == 2.f);
}

void SkipUsesGateCount() {
    // The exit predicate is the W3 gate count — including the effect pictures. A local timeline-only
    // predicate would leave the picture running.
    FAnimFixture fixture;
    auto& registry = fixture.registry;
    game::FActiveEffect effect;
    effect.duration = 0.4f;
    registry.ctx().get<game::FBattleEffects>().active.push_back(effect);
    registry.ctx().emplace<FSettings>(FSettings{game::kAnimSpeedNormal, true});
    game::ecs::BattleAnimationSystem animations;
    animations.update(registry, 1.f / 60.f);
    CHECK(registry.ctx().get<game::FBattleEffects>().active.empty());
    ExpectSettled(registry);

    // Without skip the very same setup advances one 60 Hz frame and keeps the picture: the drain above
    // is the skip, not the frame.
    FAnimFixture plain;
    auto& other = plain.registry;
    game::FActiveEffect midFlight;
    midFlight.duration = 0.4f;
    other.ctx().get<game::FBattleEffects>().active.push_back(midFlight);
    game::ecs::BattleAnimationSystem steady;  // no FSettings: 1x, no skip
    steady.update(other, 1.f / 60.f);
    CHECK(other.ctx().get<game::FBattleEffects>().active.size() == 1);
    CHECK(other.ctx().get<FPresentationGate>().busy == 1);
}

void SkipIsBounded() {
    FAnimFixture fixture;
    fixture.BuildWarriorSet();
    auto& registry = fixture.registry;
    const entt::entity unit = fixture.MakeWarrior();
    registry.get<FUnitAnim>(unit).pendingDeaths = 1;  // no announcing blow: the gate can never open
    registry.ctx().get<game::FBattleTimeline>().steps.push_back(
        game::FBattleTimeline::FStep{4.f, [](entt::registry&) {}});
    registry.ctx().emplace<FSettings>(FSettings{game::kAnimSpeedNormal, true});

    game::ecs::BattleAnimationSystem animations;
    animations.update(registry, 1.f / 60.f);  // must return even though the count can never reach 0

    CHECK(animations.skipBoundWarned);
    CHECK(game::ecs::PresentationBusyCount(registry) > 0);
    // The bound only stops the loop: the step (4s > the 3s budget) is still queued with its progress.
    const auto& steps = registry.ctx().get<game::FBattleTimeline>().steps;
    CHECK(steps.size() == 1 && steps.front().delay > 0.f && steps.front().delay < 4.f);

    // Once the gate can open, the next frame settles and the warning resets.
    registry.get<FUnitAnim>(unit).pendingDeaths = 0;
    registry.ctx().get<game::FBattleTimeline>().steps.clear();
    animations.update(registry, 1.f / 60.f);
    CHECK(!animations.skipBoundWarned);
    ExpectSettled(registry);
}

void SkipBoundLeavesStepsForNextFrame() {
    FAnimFixture fixture;
    fixture.BuildWarriorSet();
    auto& registry = fixture.registry;
    const entt::entity unit = fixture.MakeWarrior();
    registry.get<FUnitAnim>(unit).pendingDeaths = 1;  // keeps the first frame busy to the bound
    bool landed = false;
    registry.ctx().get<game::FBattleTimeline>().steps.push_back(game::FBattleTimeline::FStep{4.f,
        [&landed, unit](entt::registry& r) {
            landed = true;
            r.get<FUnitAnim>(unit).pendingDeaths = 0;
        }});
    registry.ctx().emplace<FSettings>(FSettings{game::kAnimSpeedNormal, true});

    game::ecs::BattleAnimationSystem animations;
    animations.update(registry, 1.f / 60.f);
    CHECK(!landed);  // the bound hit before the 4s step was due...
    CHECK(registry.ctx().get<game::FBattleTimeline>().steps.size() == 1);  // ...but nothing was discarded
    const float remaining = registry.ctx().get<game::FBattleTimeline>().steps.front().delay;
    CHECK(remaining > 0.f && remaining < 4.f);

    animations.update(registry, 1.f / 60.f);  // the next frame consumes it and settles
    CHECK(landed);
    CHECK(registry.ctx().get<game::FBattleTimeline>().steps.empty());
    ExpectSettled(registry);
}

void SkipStillSettlesDeaths() {
    FAnimFixture fixture;
    fixture.BuildWarriorSet();
    auto& registry = fixture.registry;
    const entt::entity attacker = fixture.MakeWarrior();
    const entt::entity victim = fixture.MakeWarrior();
    FBattleEvent event;
    event.kind = FBattleEvent::EKind::Skill;
    event.actor = attacker;
    event.aim = game::FGridPosition{1, 1};
    event.skillId = "skill.strike";  // not in the hand-built data: the fallback effect plays
    FBattleEvent::FTarget target;
    target.unit = victim;
    target.healthBefore = 3;
    target.healthAfter = 0;
    target.hostile = true;
    target.died = true;
    event.targets.push_back(target);
    registry.ctx().get<game::FBattleEvents>().pending.push_back(std::move(event));
    registry.ctx().emplace<FSettings>(FSettings{game::kAnimSpeedNormal, true});

    game::ecs::BattleAnimationSystem animations;
    animations.update(registry, 1.f / 60.f);

    const auto& anim = registry.get<FUnitAnim>(victim);
    CHECK(anim.pendingDeaths == 0);                // the death reaction ran through the normal path
    CHECK(anim.state == game::EUnitAnim::Death);   // and left the unit in the death pose
    CHECK(!animations.skipBoundWarned);
    ExpectSettled(registry);                       // the death clip finished inside the skip
}

void SkipStillFinishesWalk() {
    FAnimFixture fixture;
    fixture.BuildWarriorSet();
    auto& registry = fixture.registry;
    const entt::entity unit = fixture.MakeWarrior();
    auto& anim = registry.get<FUnitAnim>(unit);
    anim.route.push_back({1.f, 2.f});
    anim.route.push_back({3.f, 4.f});
    registry.ctx().emplace<FSettings>(FSettings{game::kAnimSpeedNormal, true});

    game::ecs::BattleAnimationSystem animations;
    animations.update(registry, 1.f / 60.f);

    CHECK(anim.route.empty());
    CHECK(anim.visual.x == 3.f && anim.visual.y == 4.f);
    CHECK(anim.state == game::EUnitAnim::Idle);
    ExpectSettled(registry);
}

void SkipKeepsAmbientClockAtFrameDt() {
    // Skip must not hijack the ambient clock: with nothing busy and skip on, the first pass is the real
    // frame time, so idle clips advance exactly as in a session with no settings at all. On a 144 Hz
    // display the old fixed-slice first pass would have run idles ~2.4x fast (1/60 vs 1/144 per frame).
    FAnimFixture skipping;
    skipping.BuildWarriorSet();
    const entt::entity skippingUnit = skipping.MakeWarrior();
    skipping.registry.ctx().emplace<FSettings>(FSettings{game::kAnimSpeedNormal, true});

    FAnimFixture plain;  // no FSettings: 1x, no skip — the reference clock
    plain.BuildWarriorSet();
    const entt::entity plainUnit = plain.MakeWarrior();

    game::ecs::BattleAnimationSystem skipAnimations;
    game::ecs::BattleAnimationSystem plainAnimations;
    constexpr float kFrame = 1.f / 144.f;
    for (int frame = 0; frame < 60; ++frame) {
        skipAnimations.update(skipping.registry, kFrame);
        plainAnimations.update(plain.registry, kFrame);
    }

    const auto& fast = skipping.registry.get<FUnitAnim>(skippingUnit).player;
    const auto& reference = plain.registry.get<FUnitAnim>(plainUnit).player;
    CHECK(fast.clip.value == reference.clip.value);
    CHECK(fast.frame == reference.frame);
    CHECK(fast.frameTime == reference.frameTime);  // exact: both sides accumulated the same dt sequence
    CHECK(fast.frameTime > 0.0);                   // and the clock is live, not frozen
    CHECK(fast.frame > 0);                         // 60/144 s has passed the idle clip's first frame
}

// ---------------------------------------------------------------------------------------------
// ParseBattleAnim loop warning
// ---------------------------------------------------------------------------------------------

void DeathClipLoopIsWarned() {
    // A private first root shadows anim/clips: "loopy" loops its death clip, "spinny" its attack,
    // "clean" neither. The images resolve from the shipped assets and the unit id from the fixture
    // content, so the parse gets all the way to the loop check.
    const auto root = FreshDir("clips-root");
    std::filesystem::create_directories(root / "anim");
    const auto row = [](const std::string& id, const std::string& image, bool loop) {
        return nlohmann::json{{"id", id}, {"image", image}, {"frame_w", 64}, {"frame_h", 64}, {"row", 0},
                              {"start", 0}, {"count", 2}, {"fps", 8}, {"loop", loop}};
    };
    nlohmann::json clips{{"schema_version", 1}, {"clips", nlohmann::json::array()}};
    for (const std::string prefix : {"clean", "loopy", "spinny"}) {
        for (std::size_t action = 0; action < game::kUnitAnimCount; ++action) {
            const std::string name = kActionSuffix[action];
            const bool loop = name == "idle" || name == "walk" || (name == "death" && prefix == "loopy") ||
                              (name == "attack" && prefix == "spinny");
            clips["clips"].push_back(row(prefix + "." + name, "art/anim/warrior_" + name, loop));
        }
    }
    {
        std::ofstream file(root / "anim" / "clips.json");
        file << clips.dump(2);
        CHECK(file.good());
    }

    FLogCapture capture;
    auto roots = TestAssetRoots();
    roots.insert(roots.begin(), root.string());
    FAnimFixture fixture(std::move(roots));
    const auto* library = game::FAnimationLibrary::Find(fixture.registry);
    CHECK(library != nullptr);
    const auto& content = fixture.registry.ctx().get<game::gameplay::FContentRegistry>();

    const auto document = [](const char* prefix) {
        return nlohmann::json{
            {"schema_version", 1},
            {"units", {{"char.warrior", {{"clips", prefix}, {"impact_frame", 0}}}}},
            {"skills", nlohmann::json::object()},
            {"default_effect", {{"kind", "impact"}, {"clip", "clean.attack"}}},
        };
    };

    capture.clear();
    CHECK(game::ParseBattleAnim(document("loopy"), *library, content).has_value());  // a warning, not a failure
    CHECK(capture.text().find("loop clip on a one-shot action") != std::string::npos);
    CHECK(capture.text().find("char.warrior") != std::string::npos);

    capture.clear();
    CHECK(game::ParseBattleAnim(document("spinny"), *library, content).has_value());
    CHECK(capture.text().find("loop clip on a one-shot action") != std::string::npos);

    capture.clear();
    CHECK(game::ParseBattleAnim(document("clean"), *library, content).has_value());
    CHECK(capture.text().find("loop clip on a one-shot action") == std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// Speed scales the presentation only (full auto-played battle)
// ---------------------------------------------------------------------------------------------

struct FSimResult {
    bool resolved{false};
    bool victory{false};
    std::uint64_t rounds{0};
    int frames{0};
    std::vector<int> partyHealth;
    std::vector<int> partyHealthMax;
    std::vector<int> enemyHealth;
    std::vector<int> enemyHealthMax;
    std::vector<std::string> logLines;  // the full battle feed: identical text = identical rules and RNG
};

/// One full auto-played battle, driven in the Engine's system order (Turn -> TurnFlow -> Resolver ->
/// Animation -> Outcome -> BattleFlow last, main.cpp) with the presentation gate waiting as in a real
/// frame; the battle feed is captured for a line-for-line comparison across speeds.
FSimResult RunAutoplayBattle(const FSettings& settings) {
    constexpr std::uint32_t kSeed = 20261108;  // pinned: same room, same units, same RNG stream
    constexpr float kFrame = 1.f / 60.f;
    constexpr int kMaxFrames = 60000;

    const std::string checkpoint = UniquePath("settings-battle");
    FSimResult result;
    {
        // The renderer must outlive the registry (textures are destroyed with it).
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
            SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA8888), &SDL_DestroySurface);
        CHECK(surface != nullptr);
        FRendererPtr renderer{SDL_CreateSoftwareRenderer(surface.get())};
        CHECK(renderer != nullptr);

        entt::registry registry;
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        const std::string runSave = registry.ctx().get<game::FDungeonSave>().path;
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        registry.ctx().emplace<SDL_Renderer*>(renderer.get());
        CHECK(game::FAnimationLibrary::Initialize(registry));
        CHECK(game::InitializeBattleAnim(registry));

        game::ecs::TurnSystem turns;
        turns.onRegister(registry);
        FEmberBattleState battle;
        game::StartDungeonRun(registry, kSeed, true);
        battle.OnEnter(registry);
        registry.ctx().get<FCombatRuntime>().autoPlay = true;
        registry.ctx().emplace<FSettings>(settings);
        game::InitializeEventBus(registry);

        game::ecs::TurnFlowSystem flow;
        game::ecs::ActionResolverSystem actions;
        game::ecs::CombatOutcomeSystem outcomes;
        game::ecs::BattleAnimationSystem animations;
        game::ecs::FBattleFlowSystem battleFlow;  // the engine's last system: owns the 4s watch

        while (!registry.ctx().get<FBattleOutcome>().resolved && result.frames < kMaxFrames) {
            registry.ctx().get<game::FEventBus>().beginFrame();
            turns.update(registry, kFrame);
            flow.update(registry, kFrame);
            actions.update(registry, kFrame);
            animations.update(registry, kFrame);
            outcomes.update(registry, kFrame);
            battleFlow.update(registry, kFrame);
            ++result.frames;
        }
        CHECK(result.frames < kMaxFrames);

        const auto& outcome = registry.ctx().get<FBattleOutcome>();
        result.resolved = outcome.resolved;
        result.victory = outcome.victory;
        result.rounds = registry.ctx().get<game::FTurnRuntime>().controller.snapshot().roundNumber;
        for (const entt::entity member : registry.view<game::FPartyMember, FHealth>()) {
            const auto& health = registry.get<FHealth>(member);
            result.partyHealth.push_back(health.current);
            result.partyHealthMax.push_back(health.max);
        }
        for (const entt::entity enemy : registry.view<game::FEnemy, FHealth>()) {
            const auto& health = registry.get<FHealth>(enemy);
            result.enemyHealth.push_back(health.current);
            result.enemyHealthMax.push_back(health.max);
        }
        if (const auto* log = registry.ctx().find<game::FCombatLog>()) result.logLines = log->lines;
        battle.OnExit(registry);
        RemoveFiles(checkpoint);
        RemoveFiles(runSave);
    }
    return result;
}

void AnimSpeedScalesPresentationOnly() {
    const FSimResult atOneX = RunAutoplayBattle(FSettings{game::kAnimSpeedNormal, false});
    const FSimResult atTwoX = RunAutoplayBattle(FSettings{game::kAnimSpeedFast, false});
    const FSimResult skipped = RunAutoplayBattle(FSettings{game::kAnimSpeedFast, true});

    // A real fight happened: somebody ended below full health (the checks below are not vacuously true).
    const auto anyHurt = [](const FSimResult& run) {
        for (std::size_t index = 0; index < run.partyHealth.size(); ++index) {
            if (run.partyHealth[index] < run.partyHealthMax[index]) return true;
        }
        for (std::size_t index = 0; index < run.enemyHealth.size(); ++index) {
            if (run.enemyHealth[index] < run.enemyHealthMax[index]) return true;
        }
        return false;
    };
    for (const FSimResult* run : {&atOneX, &atTwoX, &skipped}) {
        CHECK(run->resolved);
        CHECK(anyHurt(*run));
    }
    // Positive control: the settings were consumed — 2x needs fewer frames than 1x, skip fewer still.
    CHECK(atTwoX.frames < atOneX.frames);
    CHECK(skipped.frames < atTwoX.frames);
    // The rules outcome is identical: winner, rounds and per-unit final HP.
    CHECK(atOneX.victory == atTwoX.victory);
    CHECK(atTwoX.victory == skipped.victory);
    CHECK(atOneX.rounds == atTwoX.rounds);
    CHECK(atTwoX.rounds == skipped.rounds);
    CHECK(atOneX.partyHealth == atTwoX.partyHealth);
    CHECK(atTwoX.partyHealth == skipped.partyHealth);
    CHECK(atOneX.enemyHealth == atTwoX.enemyHealth);
    CHECK(atTwoX.enemyHealth == skipped.enemyHealth);
    // And so is the full battle feed, line for line: identical text proves identical rules + RNG,
    // not just an equal end state (damage numbers, hit/miss, KO order all have to match).
    CHECK(!atOneX.logLines.empty());
    CHECK(atOneX.logLines == atTwoX.logLines);
    CHECK(atTwoX.logLines == skipped.logLines);
}

} // namespace

int main() {
    // Save safety: ctest exports no GAME01P_SAVE_DIR for feature tests, so a plain run would let
    // settings.json be read from — and written to — the user's real preference directory. Commit an
    // override before any dungeon/settings service can touch disk.
    if (const char* existing = std::getenv(game::kSettingsSaveDirEnv); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "settings_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s(game::kSettingsSaveDirEnv, value.c_str());
#else
        setenv(game::kSettingsSaveDirEnv, value.c_str(), 1);
#endif
    }
    return game_test::RunTests("settings", {
        {"DefaultIsOneX", DefaultIsOneX},
        {"SettingsUseIsolatedDir", SettingsUseIsolatedDir},
        {"SettingsRoundTrip", SettingsRoundTrip},
        {"CorruptSettingsFallsBack", CorruptSettingsFallsBack},
        {"InvalidSpeedClampsToOne", InvalidSpeedClampsToOne},
        {"SettingsToggleAPI", SettingsToggleAPI},
        {"SettingsNeverFallBackWhenOverrideSet", SettingsNeverFallBackWhenOverrideSet},
        {"SavedSpeedAppliesWithoutOpeningSettings", SavedSpeedAppliesWithoutOpeningSettings},
        {"SkipSettlesImmediately", SkipSettlesImmediately},
        {"SkipUsesGateCount", SkipUsesGateCount},
        {"SkipIsBounded", SkipIsBounded},
        {"SkipBoundLeavesStepsForNextFrame", SkipBoundLeavesStepsForNextFrame},
        {"SkipStillSettlesDeaths", SkipStillSettlesDeaths},
        {"SkipStillFinishesWalk", SkipStillFinishesWalk},
        {"SkipKeepsAmbientClockAtFrameDt", SkipKeepsAmbientClockAtFrameDt},
        {"DeathClipLoopIsWarned", DeathClipLoopIsWarned},
        {"AnimSpeedScalesPresentationOnly", AnimSpeedScalesPresentationOnly},
    });
}
