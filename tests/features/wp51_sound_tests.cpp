// WP-51 sound tests: the audio manager (silent and offline), the catalog parse rules, the audio director
// (cues, music places, turn and button cues), audio settings, the Settings rows, the light-mesh fix for SDL's
// software renderer and the simulated scene clock. The two recorder behaviours (F24/F25) are ctest entries
// driven by tests/check_record_output.cmake.
#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <SDL3/SDL.h>

#include "core/AnimationLibrary.h"
#include "core/AssetManager.h"
#include "core/AudioManager.h"
#include "core/EngineState.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"
#include "ecs/AnimationFunctions.h"
#include "ecs/components/FBossGoals.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/systems/EmberGridRenderSystem.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "presentation/FAudioCatalog.h"
#include "presentation/FAudioDirector.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FSettings.h"
#include "presentation/FSettingsView.h"
#include "presentation/FUiTheme.h"

namespace {

using game::EAudioEvent;
using game::EAudioOutput;
using game::EMusicPlace;
using game::FAudioCatalog;
using game::FAudioDirectorState;
using game::FAudioManager;
using game::FBattleAnimData;
using game::FBattleEvent;

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

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

class FLogCapture {
public:
    FLogCapture() {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream);
        sink->set_pattern("%v");
        logger_ = std::make_shared<spdlog::logger>("wp51-test", sink);
        logger_->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger_);
    }
    ~FLogCapture() { SetCurrentGameLogger(nullptr); }
    FLogCapture(const FLogCapture&) = delete;
    FLogCapture& operator=(const FLogCapture&) = delete;
    [[nodiscard]] std::string text() const { return stream.str(); }

private:
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger_;
};

[[nodiscard]] bool Contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

[[nodiscard]] std::string ReadText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

constexpr std::array<const char*, game::kUnitAnimCount> kActionSuffix{"idle", "walk", "attack", "hit", "death"};

/// Assets + content + run sequence + audio catalog (Silent manager) + the battle animation contexts.
struct FAudioFixture {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, &SDL_DestroySurface};
    FRendererPtr renderer{nullptr};
    entt::registry registry;

    FAudioFixture() {
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        surface.reset(SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA8888));
        CHECK(surface != nullptr);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        CHECK(renderer != nullptr);
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        registry.ctx().emplace<SDL_Renderer*>(renderer.get());
        CHECK(game::FAnimationLibrary::Initialize(registry));
        std::string error;
        const auto* assets = registry.ctx().find<game::FAssetManager>();
        CHECK(assets != nullptr);
        const auto dungeon = assets->digest("data/dungeon");
        CHECK(dungeon != nullptr);
        auto sequence = game::FRunSequence::Load(dungeon->json(), error);
        CHECK(sequence.has_value());
        registry.ctx().emplace<game::FRunSequence>(std::move(*sequence));
        FAudioManager::Initialize(registry, EAudioOutput::Silent);
        CHECK(game::InitializeAudio(registry));
        CHECK(registry.ctx().find<FAudioDirectorState>() != nullptr && registry.ctx().find<FAudioCatalog>() != nullptr);
        registry.ctx().get<FAudioDirectorState>().logPlays = true;
        registry.ctx().emplace<FBattleAnimData>();
        game::BeginBattleAnim(registry);
        BuildWarriorSet();
    }

    void BuildWarriorSet() {
        const auto* library = game::FAnimationLibrary::Find(registry);
        CHECK(library != nullptr);
        FBattleAnimData data;
        game::FUnitAnimSet set;
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
        game::FUnitAnim anim;
        anim.set = 0;
        const game::FAnimClipId idle = data.sets[0].clips[static_cast<std::size_t>(game::EUnitAnim::Idle)];
        if (const game::FAnimClip* clip = library->get(idle)) game::ecs::StartClip(anim.player, idle, *clip);
        const entt::entity unit = registry.create();
        registry.emplace<game::FUnitAnim>(unit, std::move(anim));
        return unit;
    }

    [[nodiscard]] FAudioCatalog& catalog() { return registry.ctx().get<FAudioCatalog>(); }
    [[nodiscard]] FAudioDirectorState& director() { return registry.ctx().get<FAudioDirectorState>(); }
    [[nodiscard]] game::FAudioCueId cue(std::string_view id) {
        const game::FAudioCueId found = catalog().findCue(id);
        CHECK(found != game::kNoAudioCue);
        return found;
    }

    void Step(int frames) {
        for (int frame = 0; frame < frames; ++frame) {
            game::UpdateBattleAnimations(registry, 1.f / 60.f);
            game::UpdateAudioDirector(registry, "ember.battle");
        }
    }
    [[nodiscard]] std::vector<std::string> Cues(bool skipBattleStart = true) {
        std::vector<std::string> result;
        for (const auto& played : director().played) {
            if (skipBattleStart && played.cue == "sfx.battle_start") continue;
            result.push_back(played.cue);
        }
        return result;
    }
    [[nodiscard]] std::size_t Count(std::string_view id) {
        return static_cast<std::size_t>(std::count_if(director().played.begin(), director().played.end(),
                                                      [&](const game::FPlayedSound& s) { return s.cue == id; }));
    }
    [[nodiscard]] std::uint64_t FrameOf(std::string_view id) {
        for (const auto& played : director().played) {
            if (played.cue == id) return played.frame;
        }
        throw std::runtime_error(std::string("cue never played: ") + std::string(id));
    }
    [[nodiscard]] std::string MusicFile(game::FMusicId id) { return catalog().music.at(id).file; }
    [[nodiscard]] const std::string& Current() { return registry.ctx().get<FAudioManager>().currentMusic(); }
};

/// The shipped audio.json parsed against the fixture's content (for the rejection tests).
struct FParseContext {
    entt::registry registry;
    std::vector<std::string> themes;
    FParseContext() {
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        themes = {"theme.ember_crypt", "theme.ashen_reach"};
    }
    [[nodiscard]] nlohmann::json Shipped() {
        const auto* assets = registry.ctx().find<game::FAssetManager>();
        const auto digest = assets->digest("data/audio");
        CHECK(digest != nullptr);
        return digest->json();
    }
    [[nodiscard]] std::expected<FAudioCatalog, std::string> Parse(const nlohmann::json& document) {
        return game::ParseAudioCatalog(document, *registry.ctx().find<game::FAssetManager>(),
                                       registry.ctx().get<game::gameplay::FContentRegistry>(), themes,
                                       game::AudioStateCueIds());
    }
};

void ExpectRejected(FParseContext& context, const nlohmann::json& document, std::string_view mention) {
    const auto parsed = context.Parse(document);
    CHECK(!parsed.has_value());
    CHECK(Contains(parsed.error(), mention));
}

// ---------------------------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------------------------

void ShippedCatalogLoads() {
    FAudioFixture fixture;
    const auto& content = fixture.registry.ctx().get<game::gameplay::FContentRegistry>();
    FAudioCatalog& catalog = fixture.catalog();
    CHECK(!content.skills.rows.empty());
    for (const auto& skill : content.skills.rows) CHECK(catalog.skills.contains(skill.id));
    for (const game::FAudioCueId event : catalog.events) CHECK(event != game::kNoAudioCue);
    CHECK(catalog.hub != game::kNoMusic && catalog.camp != game::kNoMusic);
    CHECK(catalog.boss != game::kNoMusic && catalog.victory != game::kNoMusic);
    CHECK(catalog.themes.contains("theme.ember_crypt"));
    CHECK(catalog.themes.contains("theme.ashen_reach"));
    const game::FSkillAudio arrow = game::SkillAudio(fixture.registry, "skill.arrow");
    CHECK(arrow.release == catalog.findCue("sfx.bow_release"));
    CHECK(arrow.land == catalog.findCue("sfx.arrow_hit"));
    CHECK(arrow.release != game::kNoAudioCue && arrow.land != game::kNoAudioCue);
}

void UnknownCueRejected() {
    FParseContext context;
    auto skill = context.Shipped();
    skill["skills"]["skill.strike"]["land"] = "sfx.nope";
    ExpectRejected(context, skill, "sfx.nope");
    auto event = context.Shipped();
    event["events"]["crit"] = "sfx.nope";
    ExpectRejected(context, event, "sfx.nope");
    auto state = context.Shipped();
    state["state_cues"]["ember.camp"] = "sfx.nope";
    ExpectRejected(context, state, "sfx.nope");
}

void UnknownSkillKeyRejected() {
    FParseContext context;
    auto document = context.Shipped();
    document["skills"]["skill.nope"] = {{"land", "sfx.crit"}};
    ExpectRejected(context, document, "skill.nope");
}

void MissingOrUnsupportedFileRejected() {
    FParseContext context;
    auto missing = context.Shipped();
    missing["cues"]["ui.click"]["files"] = nlohmann::json::array({"audio/sfx/missing.wav"});
    ExpectRejected(context, missing, "missing.wav");
    auto ogg = context.Shipped();
    ogg["cues"]["ui.click"]["files"] = nlohmann::json::array({"audio/sfx/ui_click.ogg"});
    ExpectRejected(context, ogg, ".ogg");
    auto music = context.Shipped();
    music["music"]["hub"]["file"] = "audio/music/none.wav";
    ExpectRejected(context, music, "none.wav");
}

void ThemeMusicMustMatchSequence() {
    FParseContext context;
    auto missing = context.Shipped();
    missing["music"]["themes"].erase("theme.ashen_reach");
    ExpectRejected(context, missing, "theme.ashen_reach");
    auto extra = context.Shipped();
    extra["music"]["themes"]["theme.nowhere"] = {{"file", "audio/music/hub.wav"}, {"volume", 0.5}};
    ExpectRejected(context, extra, "theme.nowhere");
}

void SkillFallbackByTargetAndDamageType() {
    FParseContext context;
    auto document = context.Shipped();
    for (const char* id : {"skill.heal", "skill.bolt", "skill.strike"}) document["skills"].erase(id);
    const auto parsed = context.Parse(document);
    CHECK(parsed.has_value());
    const FAudioCatalog& catalog = *parsed;
    CHECK(catalog.skills.at("skill.heal").land == catalog.findCue("sfx.buff"));
    CHECK(catalog.skills.at("skill.bolt").release == catalog.findCue("sfx.arcane_cast"));
    CHECK(catalog.skills.at("skill.bolt").land == catalog.findCue("sfx.arcane_burst"));
    CHECK(catalog.skills.at("skill.strike").land == catalog.findCue("sfx.melee_slash"));
    CHECK(catalog.skills.at("skill.strike").release == game::kNoAudioCue);
}

// ---------------------------------------------------------------------------------------------
// Audio manager
// ---------------------------------------------------------------------------------------------

void AudioOutputRules() {
    using game::ResolveAudioOutput;
    CHECK(ResolveAudioOutput("", "dummy", false) == EAudioOutput::Silent);
    CHECK(ResolveAudioOutput("1", "dummy", false) == EAudioOutput::Device);
    CHECK(ResolveAudioOutput("0", "windows", false) == EAudioOutput::Silent);
    CHECK(ResolveAudioOutput("", "windows", false) == EAudioOutput::Device);
    CHECK(ResolveAudioOutput("yes", "dummy", false) == EAudioOutput::Silent);
    CHECK(ResolveAudioOutput("0", "dummy", true) == EAudioOutput::Offline);
}

void SilentManagerIsANoOp() {
    entt::registry registry;
    FAudioManager::Initialize(registry, EAudioOutput::Silent);
    auto& manager = registry.ctx().get<FAudioManager>();
    CHECK(manager.output() == EAudioOutput::Silent);
    CHECK(manager.preload("no/such.wav"));
    manager.playOneShot("no/such.wav", 1.f, 1.f);
    manager.update();
    manager.playMusic("a.wav", 1.f, 1.f);
    CHECK(manager.currentMusic() == "a.wav");
    manager.setBusVolume(game::EAudioBus::Music, 1.5f);
    CHECK(manager.busVolume(game::EAudioBus::Music) == 1.f);
    CHECK(!manager.beginCapture((FreshDir("f8") / "x.wav").string()));
    FAudioManager::Shutdown(registry);
}

struct FWavInfo {
    std::uint16_t tag{0}, channels{0}, bits{0};
    std::uint32_t rate{0}, dataBytes{0};
    float peak{0.f};
};

[[nodiscard]] FWavInfo ParseWav(const std::string& path) {
    const std::string bytes = ReadText(path);
    CHECK(bytes.size() > 44);
    CHECK(std::memcmp(bytes.data(), "RIFF", 4) == 0);
    CHECK(std::memcmp(bytes.data() + 8, "WAVE", 4) == 0);
    FWavInfo info;
    std::size_t at = 12;
    while (at + 8 <= bytes.size()) {
        std::uint32_t size = 0;
        std::memcpy(&size, bytes.data() + at + 4, 4);
        const char* body = bytes.data() + at + 8;
        if (std::memcmp(bytes.data() + at, "fmt ", 4) == 0) {
            std::memcpy(&info.tag, body, 2);
            std::memcpy(&info.channels, body + 2, 2);
            std::memcpy(&info.rate, body + 4, 4);
            std::memcpy(&info.bits, body + 14, 2);
        } else if (std::memcmp(bytes.data() + at, "data", 4) == 0) {
            info.dataBytes = size;
            const std::size_t available = std::min<std::size_t>(size, bytes.size() - (at + 8));
            for (std::size_t offset = 0; offset + 4 <= available; offset += 4) {
                float sample = 0.f;
                std::memcpy(&sample, body + offset, 4);
                info.peak = std::max(info.peak, std::fabs(sample));
            }
        }
        at += 8 + size + (size & 1u);
    }
    return info;
}

void OfflineCaptureWritesMixedAudio() {
    entt::registry registry;
    FAudioManager::Initialize(registry, EAudioOutput::Offline);
    auto& manager = registry.ctx().get<FAudioManager>();
    CHECK(manager.output() == EAudioOutput::Offline);
    const std::string sound = "assets/audio/sfx/heavy_impact_1.wav";
    CHECK(manager.preload(sound));
    const std::string wav = (FreshDir("f9") / "f9.wav").string();
    CHECK(manager.beginCapture(wav));
    manager.playOneShot(sound, 1.f, 1.f);
    CHECK(manager.renderCapture(4800) == 4800);
    manager.endCapture();
    FAudioManager::Shutdown(registry);
    const FWavInfo info = ParseWav(wav);
    CHECK(info.tag == 3 || info.tag == 0xFFFE);
    CHECK(info.channels == 2 && info.rate == 48000 && info.bits == 32);
    CHECK(info.dataBytes == 4800u * 8u);
    CHECK(info.peak > 0.01f);
}

void EveryShippedFileDecodes() {
    FAudioFixture fixture;
    FAudioManager::Initialize(fixture.registry, EAudioOutput::Offline);
    auto& manager = fixture.registry.ctx().get<FAudioManager>();
    CHECK(manager.output() == EAudioOutput::Offline);
    for (const auto& cue : fixture.catalog().cues) {
        for (const std::string& file : cue.files) CHECK(manager.preload(file));
    }
    for (const auto& track : fixture.catalog().music) CHECK(manager.preload(track.file));
}

// ---------------------------------------------------------------------------------------------
// Director: cues from the battle presentation
// ---------------------------------------------------------------------------------------------

[[nodiscard]] FBattleEvent SkillEvent(entt::entity actor, entt::entity target, const char* skill, int before,
                                      int after, bool died = false, bool crit = false) {
    FBattleEvent event;
    event.kind = FBattleEvent::EKind::Skill;
    event.actor = actor;
    event.skillId = skill;
    event.targets.push_back({target, before, after, true, died, crit});
    return event;
}

void SkillCuesFollowTheBlow() {
    FAudioFixture fixture;
    const entt::entity actor = fixture.MakeWarrior();
    const entt::entity target = fixture.registry.create();
    fixture.registry.ctx().get<game::FBattleEvents>().pending.push_back(SkillEvent(actor, target, "skill.arrow", 10, 6));
    fixture.Step(60);
    const auto cues = fixture.Cues();
    CHECK((cues == std::vector<std::string>{"sfx.bow_release", "sfx.arrow_hit", "sfx.unit_hit"}));
    const std::uint64_t frame = fixture.FrameOf("sfx.bow_release");
    CHECK(frame >= 1);
    CHECK(fixture.FrameOf("sfx.arrow_hit") == frame);
    CHECK(fixture.FrameOf("sfx.unit_hit") == frame);
}

void ProjectileLandsAfterRelease() {
    FAudioFixture fixture;
    const auto* library = game::FAnimationLibrary::Find(fixture.registry);
    game::FSkillEffect projectile;
    projectile.kind = game::EEffectKind::Projectile;
    projectile.clip = *library->findClip("fx.slash");
    projectile.speed = 100.f;
    fixture.registry.ctx().get<FBattleAnimData>().skills["skill.arrow"] = projectile;
    const entt::entity actor = fixture.MakeWarrior();
    const entt::entity target = fixture.registry.create();
    fixture.registry.emplace<game::FGridPosition>(actor, game::FGridPosition{0, 0});
    fixture.registry.emplace<game::FGridPosition>(target, game::FGridPosition{4, 0});
    FBattleEvent event = SkillEvent(actor, target, "skill.arrow", 10, 6);
    event.aim = {4, 0};
    fixture.registry.ctx().get<game::FBattleEvents>().pending.push_back(event);
    fixture.Step(180);
    CHECK(fixture.FrameOf("sfx.arrow_hit") > fixture.FrameOf("sfx.bow_release"));
    CHECK(fixture.FrameOf("sfx.unit_hit") == fixture.FrameOf("sfx.arrow_hit"));
}

void DeathAndCritCues() {
    FAudioFixture fixture;
    const entt::entity actor = fixture.MakeWarrior();
    const entt::entity target = fixture.registry.create();
    fixture.registry.ctx().get<game::FBattleEvents>().pending.push_back(
        SkillEvent(actor, target, "skill.strike", 10, 0, true, true));
    fixture.Step(60);
    CHECK(fixture.Count("sfx.unit_death") == 1);
    CHECK(fixture.Count("sfx.crit") == 1);
    CHECK(fixture.Count("sfx.unit_hit") == 0);
}

void FootstepPerCell() {
    FAudioFixture fixture;
    const entt::entity actor = fixture.MakeWarrior();
    FBattleEvent move;
    move.kind = FBattleEvent::EKind::Move;
    move.actor = actor;
    move.path = {{0, 0}, {1, 0}, {2, 0}};
    fixture.registry.ctx().get<game::FBattleEvents>().pending.push_back(move);
    fixture.Step(60);
    std::vector<std::uint64_t> frames;
    for (const auto& played : fixture.director().played) {
        if (played.cue == "sfx.footstep") frames.push_back(played.frame);
    }
    CHECK(frames.size() == 2);
    const auto gap = static_cast<long long>(frames[1]) - static_cast<long long>(frames[0]);
    const long long expected = std::llround(fixture.registry.ctx().get<FBattleAnimData>().moveSeconds * 60.f);
    CHECK(std::llabs(gap - expected) <= 1);
}

void SameCueOncePerFrame() {
    FAudioFixture fixture;
    const game::FAudioCueId crit = fixture.cue("sfx.crit");
    game::PlayAudioCue(fixture.registry, crit);
    game::PlayAudioCue(fixture.registry, crit);
    CHECK(fixture.Count("sfx.crit") == 1);
    game::UpdateAudioDirector(fixture.registry, "ember.title");
    game::PlayAudioCue(fixture.registry, crit);
    CHECK(fixture.Count("sfx.crit") == 2);
}

void VariantsAndPitchJitter() {
    FAudioFixture fixture;
    const game::FAudioCueId slash = fixture.cue("sfx.melee_slash");
    for (int play = 0; play < 30; ++play) {
        game::PlayAudioCue(fixture.registry, slash);
        game::UpdateAudioDirector(fixture.registry, "ember.title");
    }
    const auto& played = fixture.director().played;
    CHECK(played.size() == 30);
    for (std::size_t index = 0; index < played.size(); ++index) {
        CHECK(played[index].cue == "sfx.melee_slash");
        CHECK(played[index].pitch >= 0.94f && played[index].pitch <= 1.06f);
        if (index > 0) CHECK(played[index].file != played[index - 1].file);
    }
    fixture.director().played.clear();
    game::PlayAudioCue(fixture.registry, fixture.cue("sfx.turn_party"));
    CHECK(fixture.director().played.size() == 1);
    CHECK(fixture.director().played[0].pitch == 1.f);
}

// ---------------------------------------------------------------------------------------------
// Director: music, state and turn cues
// ---------------------------------------------------------------------------------------------

void MusicPlaceRules() {
    using game::MusicPlaceFor;
    CHECK(MusicPlaceFor("ember.title", false, false) == EMusicPlace::Hub);
    CHECK(MusicPlaceFor("ember.hub", false, false) == EMusicPlace::Hub);
    CHECK(MusicPlaceFor("ember.prepare", false, false) == EMusicPlace::Hub);
    CHECK(MusicPlaceFor("ember.compendium", false, false) == EMusicPlace::Hub);
    CHECK(MusicPlaceFor("ember.battle", false, false) == EMusicPlace::Theme);
    CHECK(MusicPlaceFor("ember.battle", true, false) == EMusicPlace::Boss);
    CHECK(MusicPlaceFor("ember.reward", false, false) == EMusicPlace::Theme);
    CHECK(MusicPlaceFor("ember.event", false, false) == EMusicPlace::Theme);
    CHECK(MusicPlaceFor("ember.briefing", false, false) == EMusicPlace::Theme);
    CHECK(MusicPlaceFor("ember.camp", false, false) == EMusicPlace::Camp);
    CHECK(MusicPlaceFor("ember.runEnd", false, true) == EMusicPlace::Victory);
    CHECK(MusicPlaceFor("ember.runEnd", false, false) == EMusicPlace::Silence);
    CHECK(MusicPlaceFor("ember.settings", false, false) == EMusicPlace::Keep);
    CHECK(MusicPlaceFor("nowhere", false, false) == EMusicPlace::Keep);

    FAudioFixture fixture;
    game::UpdateAudioDirector(fixture.registry, "ember.title");
    CHECK(fixture.Current() == fixture.MusicFile(fixture.catalog().hub));
    game::UpdateAudioDirector(fixture.registry, "nowhere");
    CHECK(fixture.Current() == fixture.MusicFile(fixture.catalog().hub));
    game::FDungeonRun run;
    run.depth = 1;
    fixture.registry.ctx().emplace<game::FDungeonRun>(run);
    game::UpdateAudioDirector(fixture.registry, "ember.briefing");
    CHECK(fixture.Current() == fixture.MusicFile(fixture.catalog().themes.at("theme.ember_crypt")));
}

void MusicFollowsTheRun() {
    FAudioFixture fixture;
    game::UpdateAudioDirector(fixture.registry, "ember.title");
    CHECK(fixture.Current() == fixture.MusicFile(fixture.catalog().hub));
    game::FDungeonRun run;
    run.depth = 1;
    auto& stored = fixture.registry.ctx().emplace<game::FDungeonRun>(run);
    game::UpdateAudioDirector(fixture.registry, "ember.battle");
    CHECK(fixture.Current() == fixture.MusicFile(fixture.catalog().themes.at("theme.ember_crypt")));
    fixture.registry.ctx().emplace<game::FBossBattle>();
    game::UpdateAudioDirector(fixture.registry, "ember.battle");
    CHECK(fixture.Current() == fixture.MusicFile(fixture.catalog().boss));
    fixture.registry.ctx().erase<game::FBossBattle>();
    game::UpdateAudioDirector(fixture.registry, "ember.camp");
    CHECK(fixture.Current() == fixture.MusicFile(fixture.catalog().camp));
    stored.phase = game::EDungeonPhase::Victory;
    game::UpdateAudioDirector(fixture.registry, "ember.runEnd");
    CHECK(fixture.Current() == fixture.MusicFile(fixture.catalog().victory));
    CHECK(fixture.Count("stinger.victory") == 1);
    game::UpdateAudioDirector(fixture.registry, "ember.title");
    stored.phase = game::EDungeonPhase::Defeat;
    game::UpdateAudioDirector(fixture.registry, "ember.runEnd");
    CHECK(fixture.Current().empty());
    CHECK(fixture.Count("stinger.defeat") == 1);
}

void StateEntryCues() {
    FAudioFixture fixture;
    game::UpdateAudioDirector(fixture.registry, "ember.battle");
    CHECK(fixture.Count("sfx.battle_start") == 1);
    game::UpdateAudioDirector(fixture.registry, "ember.reward");
    CHECK(fixture.Count("sfx.reward_open") == 1);
    game::UpdateAudioDirector(fixture.registry, "ember.battle");
    CHECK(fixture.Count("sfx.battle_start") == 2);
    game::UpdateAudioDirector(fixture.registry, "ember.settings");
    game::UpdateAudioDirector(fixture.registry, "ember.battle");
    CHECK(fixture.Count("sfx.battle_start") == 2);
}

void UiButtonPressClicks() {
    FAudioFixture fixture;
    game::ui::NoteButtonPress();
    game::ui::NoteButtonPress();
    game::UpdateAudioDirector(fixture.registry, "ember.title");
    CHECK(fixture.Count("ui.click") == 1);
    CHECK(game::ui::TakeButtonPresses() == 0);
}

void TurnStartCues() {
    FAudioFixture fixture;
    auto& combat = fixture.registry.ctx().emplace<game::FCombatRuntime>();
    const entt::entity party = fixture.registry.create();
    fixture.registry.emplace<game::FPartyMember>(party);
    const entt::entity enemy = fixture.registry.create();
    fixture.registry.emplace<game::FEnemy>(enemy);
    combat.currentActor = party;
    game::UpdateAudioDirector(fixture.registry, "ember.battle");
    CHECK(fixture.Count("sfx.turn_party") == 1);
    game::UpdateAudioDirector(fixture.registry, "ember.battle");
    CHECK(fixture.Count("sfx.turn_party") == 1 && fixture.Count("sfx.turn_enemy") == 0);
    combat.currentActor = enemy;
    game::UpdateAudioDirector(fixture.registry, "ember.battle");
    CHECK(fixture.Count("sfx.turn_enemy") == 1);
}

// ---------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------

void AudioSettingsPersistAndApply() {
    const auto dir = FreshDir("f22");
    FSavedEnv override(dir);
    {
        entt::registry registry;
        FAudioManager::Initialize(registry, EAudioOutput::Silent);
        game::LoadSettings(registry);
        CHECK(game::CurrentSettings(registry).musicVolume == 75);
        CHECK(game::CurrentSettings(registry).effectsVolume == 100);
        CHECK(!game::CurrentSettings(registry).muted);
        game::CycleMusicVolume(registry);
        CHECK(game::CurrentSettings(registry).musicVolume == 100);
        game::CycleMusicVolume(registry);
        CHECK(game::CurrentSettings(registry).musicVolume == 0);
        game::SetMuted(registry, true);
        auto& manager = registry.ctx().get<FAudioManager>();
        CHECK(manager.busVolume(game::EAudioBus::Effects) == 0.f);
        CHECK(manager.busVolume(game::EAudioBus::Music) == 0.f);
        game::SetMuted(registry, false);
        CHECK(manager.busVolume(game::EAudioBus::Effects) == 1.f);
        game::SetMuted(registry, true);
    }
    {
        entt::registry registry;
        game::LoadSettings(registry);
        CHECK(game::CurrentSettings(registry).musicVolume == 0);
        CHECK(game::CurrentSettings(registry).muted);
    }
    const auto file = nlohmann::json::parse(ReadText((dir / game::kSettingsFileName).string()));
    CHECK(file.contains("music_volume") && file.contains("effects_volume") && file.contains("muted"));

    {
        std::ofstream out(dir / game::kSettingsFileName, std::ios::binary);
        out << R"({"schema_version": 1, "music_volume": 33})";
    }
    {
        FLogCapture capture;
        entt::registry registry;
        game::LoadSettings(registry);
        CHECK(game::CurrentSettings(registry).musicVolume == 25);
        CHECK(Contains(capture.text(), "must be 0, 25, 50, 75 or 100"));
    }
    {
        std::ofstream out(dir / game::kSettingsFileName, std::ios::binary);
        out << R"({"schema_version": 1, "music_volume": "x"})";
    }
    {
        entt::registry registry;
        game::LoadSettings(registry);
        CHECK(game::CurrentSettings(registry).musicVolume == 75);
    }
}

class FImGuiFixture {
public:
    FImGuiFixture() {
        context = ImGui::CreateContext();
        CHECK(context != nullptr);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = {1280.f, 800.f};
        io.DeltaTime = 1.f / 60.f;
        io.IniFilename = nullptr;
        io.Fonts->Build();
    }
    ~FImGuiFixture() { ImGui::DestroyContext(context); }
    FImGuiFixture(const FImGuiFixture&) = delete;
    FImGuiFixture& operator=(const FImGuiFixture&) = delete;

private:
    ImGuiContext* context{nullptr};
};

void SettingsViewHasSoundRows() {
    const auto dir = FreshDir("f23");
    FSavedEnv override(dir);
    FImGuiFixture imgui;
    entt::registry registry;
    registry.ctx().emplace<game::FEngineWindowState>(game::FEngineWindowState{1280, 800});
    game::LoadSettings(registry);
    const std::string logPath = (dir / "settings-ui.log").string();
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        if (frame == 2) ImGui::LogToFile(0, logPath.c_str());
        game::RenderSettingsView(registry);
        if (frame == 2) ImGui::LogFinish();
        ImGui::Render();
    }
    const std::string log = ReadText(logPath);
    CHECK(Contains(log, "Music"));
    CHECK(Contains(log, "Effects"));
    CHECK(Contains(log, "75%"));
    CHECK(Contains(log, "Mute all sound"));
}

// ---------------------------------------------------------------------------------------------
// Software-renderer light mesh and the scene clock
// ---------------------------------------------------------------------------------------------

void LightMeshModulatesOnSoftwareRenderer() {
    // (a) no two consecutive triangles share exactly two vertices.
    const std::vector<int> indices = game::ecs::LightMeshIndices(48, 30);
    CHECK(indices.size() == static_cast<std::size_t>(48 * 30 * 6));
    for (const int index : indices) CHECK(index >= 0 && index < 49 * 31);
    for (std::size_t triangle = 0; triangle + 1 < indices.size() / 3; ++triangle) {
        int shared = 0;
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t b = 0; b < 3; ++b) {
                if (indices[triangle * 3 + a] == indices[(triangle + 1) * 3 + b]) ++shared;
            }
        }
        CHECK(shared != 2);
    }

    // (b) ground x ambient on the software renderer, never the flat ambient colour.
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{
        SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA8888), &SDL_DestroySurface};
    CHECK(surface != nullptr);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const Uint8 value = ((x + y) % 2 == 0) ? 100 : 160;
            CHECK(SDL_WriteSurfacePixel(surface.get(), x, y, value, value, value, 255));
        }
    }
    FRendererPtr renderer{SDL_CreateSoftwareRenderer(surface.get())};
    CHECK(renderer != nullptr);
    const std::vector<SDL_FColor> colors(25, SDL_FColor{64.f / 255.f, 76.f / 255.f, 92.f / 255.f, 1.f});
    game::ecs::DrawModulateMesh(renderer.get(), colors, 4, 4, 64.f, 64.f);
    SDL_FlushRenderer(renderer.get());
    Uint8 r1 = 0, g = 0, b = 0, a = 0, r2 = 0;
    CHECK(SDL_ReadSurfacePixel(surface.get(), 32, 32, &r1, &g, &b, &a));
    CHECK(SDL_ReadSurfacePixel(surface.get(), 33, 32, &r2, &g, &b, &a));
    std::cout << "  light mesh pixels: (32,32) r=" << static_cast<int>(r1) << ", (33,32) r=" << static_cast<int>(r2) << '\n';
    CHECK(r1 <= 48 && r2 <= 48);
    CHECK(std::abs(static_cast<int>(r1) - static_cast<int>(r2)) >= 8);
}

void SceneClockIsSimulatedTime() {
    entt::registry registry;
    CHECK(game::SceneSeconds(registry) == 0.f);
    game::AccumulateSimSeconds(registry, 1.f);
    registry.ctx().emplace<game::FEngineFrameState>();
    game::AccumulateSimSeconds(registry, 0.25f);
    game::AccumulateSimSeconds(registry, 0.25f);
    CHECK(game::SceneSeconds(registry) == 0.5f);
}

} // namespace

int main() {
    return game_test::RunTests("wp51_sound_tests", {
        {"ShippedCatalogLoads", ShippedCatalogLoads},
        {"UnknownCueRejected", UnknownCueRejected},
        {"UnknownSkillKeyRejected", UnknownSkillKeyRejected},
        {"MissingOrUnsupportedFileRejected", MissingOrUnsupportedFileRejected},
        {"ThemeMusicMustMatchSequence", ThemeMusicMustMatchSequence},
        {"SkillFallbackByTargetAndDamageType", SkillFallbackByTargetAndDamageType},
        {"AudioOutputRules", AudioOutputRules},
        {"SilentManagerIsANoOp", SilentManagerIsANoOp},
        {"OfflineCaptureWritesMixedAudio", OfflineCaptureWritesMixedAudio},
        {"EveryShippedFileDecodes", EveryShippedFileDecodes},
        {"SkillCuesFollowTheBlow", SkillCuesFollowTheBlow},
        {"ProjectileLandsAfterRelease", ProjectileLandsAfterRelease},
        {"DeathAndCritCues", DeathAndCritCues},
        {"FootstepPerCell", FootstepPerCell},
        {"SameCueOncePerFrame", SameCueOncePerFrame},
        {"VariantsAndPitchJitter", VariantsAndPitchJitter},
        {"MusicPlaceRules", MusicPlaceRules},
        {"MusicFollowsTheRun", MusicFollowsTheRun},
        {"StateEntryCues", StateEntryCues},
        {"UiButtonPressClicks", UiButtonPressClicks},
        {"TurnStartCues", TurnStartCues},
        {"AudioSettingsPersistAndApply", AudioSettingsPersistAndApply},
        {"SettingsViewHasSoundRows", SettingsViewHasSoundRows},
        {"LightMeshModulatesOnSoftwareRenderer", LightMeshModulatesOnSoftwareRenderer},
        {"SceneClockIsSimulatedTime", SceneClockIsSimulatedTime},
    });
}
