#include "presentation/FAudioDirector.h"

#include <array>
#include <limits>

#include "core/AssetManager.h"
#include "core/AudioManager.h"
#include "core/Logger.h"
#include "ecs/components/FBossGoals.h"
#include "ecs/components/FFaction.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "presentation/FUiTheme.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberHubState.h"
#include "states/FEmberModalHostState.h"
#include "states/FEmberPreparationState.h"
#include "states/FEmberTitleState.h"

namespace game {
namespace {

constexpr std::string_view kBriefingId = "ember.briefing";  // WP-50's state, compared as a literal

const std::array<std::string_view, 7> kStateCueIds{kEmberTitleId,    kEmberHubId,      kEmberPrepareId, kEmberBattleId,
                                                   kRunModalReward, kRunModalEvent,   kRunModalCamp};

void PlayMusicTrack(FAudioManager& audio, const FAudioCatalog& catalog, FMusicId id) {
    if (id >= catalog.music.size()) return;
    const FMusicTrack& track = catalog.music[id];
    audio.playMusic(track.file, track.volume, catalog.musicFadeSeconds);
}

}  // namespace

std::span<const std::string_view> AudioStateCueIds() { return kStateCueIds; }

bool InitializeAudio(entt::registry& registry) {
    const auto* assets = registry.ctx().find<FAssetManager>();
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    const FRunSequence* sequence = FindRunSequence(registry);
    if (assets == nullptr || content == nullptr || sequence == nullptr) {
        LOG_ERROR("Audio needs assets, gameplay content and the run sequence.");
        return false;
    }
    const auto digest = assets->digest("data/audio");
    if (digest == nullptr) {
        LOG_ERROR("Audio data (data/audio) is missing.");
        return false;
    }
    std::vector<std::string> themeIds;
    for (const auto& theme : sequence->themes) themeIds.push_back(theme.id);
    auto parsed = ParseAudioCatalog(digest->json(), *assets, *content, themeIds, AudioStateCueIds());
    if (!parsed) {
        LOG_ERROR("Audio data is invalid: {}", parsed.error());
        return false;
    }
    if (auto* audio = registry.ctx().find<FAudioManager>()) {
        for (const FAudioCue& cue : parsed->cues) {
            for (const std::string& file : cue.files) {
                if (!audio->preload(file)) {
                    LOG_ERROR("Audio file {} cannot be decoded.", file);
                    return false;
                }
            }
        }
    }
    FAudioDirectorState state;
    state.lastPlayedFrame.assign(parsed->cues.size(), std::numeric_limits<std::uint64_t>::max());
    state.lastVariant.assign(parsed->cues.size(), std::numeric_limits<std::uint32_t>::max());
    registry.ctx().insert_or_assign<FAudioCatalog>(std::move(*parsed));
    registry.ctx().insert_or_assign<FAudioDirectorState>(std::move(state));
    return true;
}

void PlayAudioCue(entt::registry& registry, FAudioCueId cue) {
    auto* catalog = registry.ctx().find<FAudioCatalog>();
    auto* state = registry.ctx().find<FAudioDirectorState>();
    if (catalog == nullptr || state == nullptr || cue >= catalog->cues.size()) return;
    if (state->lastPlayedFrame[cue] == state->frame) return;
    const FAudioCue& definition = catalog->cues[cue];
    const auto count = static_cast<std::uint32_t>(definition.files.size());
    std::uint32_t variant = 0;
    if (count > 1) {
        variant = std::uniform_int_distribution<std::uint32_t>(0, count - 1)(state->rng);
        if (variant == state->lastVariant[cue]) variant = (variant + 1) % count;
    }
    float pitch = 1.f;
    if (definition.pitchJitter > 0.f) {
        pitch += std::uniform_real_distribution<float>(-definition.pitchJitter, definition.pitchJitter)(state->rng);
    }
    state->lastPlayedFrame[cue] = state->frame;
    state->lastVariant[cue] = variant;
    const std::string& file = definition.files[variant];
    if (state->logPlays) state->played.push_back(FPlayedSound{definition.id, file, pitch, state->frame});
    if (auto* audio = registry.ctx().find<FAudioManager>()) audio->playOneShot(file, definition.volume, pitch);
}

void PlayAudioEvent(entt::registry& registry, EAudioEvent event) {
    const auto* catalog = registry.ctx().find<FAudioCatalog>();
    if (catalog == nullptr) return;
    PlayAudioCue(registry, catalog->events[static_cast<std::size_t>(event)]);
}

FSkillAudio SkillAudio(const entt::registry& registry, std::string_view skillId) {
    const auto* catalog = registry.ctx().find<FAudioCatalog>();
    if (catalog == nullptr) return {};
    const auto found = catalog->skills.find(std::string(skillId));
    return found != catalog->skills.end() ? found->second : catalog->unknownSkill;
}

EMusicPlace MusicPlaceFor(std::string_view topStateId, bool bossBattle, bool runWon) {
    if (topStateId == kEmberTitleId || topStateId == kEmberHubId || topStateId == kEmberPrepareId ||
        topStateId == kEmberCompendiumId) {
        return EMusicPlace::Hub;
    }
    if (topStateId == kEmberBattleId) return bossBattle ? EMusicPlace::Boss : EMusicPlace::Theme;
    if (topStateId == kRunModalReward || topStateId == kRunModalEvent || topStateId == kBriefingId) return EMusicPlace::Theme;
    if (topStateId == kRunModalCamp) return EMusicPlace::Camp;
    if (topStateId == kRunModalRunEnd) return runWon ? EMusicPlace::Victory : EMusicPlace::Silence;
    return EMusicPlace::Keep;
}

void UpdateAudioDirector(entt::registry& registry, std::string_view topStateId) {
    auto* catalog = registry.ctx().find<FAudioCatalog>();
    auto* state = registry.ctx().find<FAudioDirectorState>();
    if (catalog == nullptr || state == nullptr) return;
    ++state->frame;
    if (ui::TakeButtonPresses() > 0) PlayAudioEvent(registry, EAudioEvent::UiClick);

    const auto* run = registry.ctx().find<FDungeonRun>();
    const bool runWon = run != nullptr && run->phase == EDungeonPhase::Victory;

    if (topStateId != state->lastTop) {
        if (state->lastTop != kEmberSettingsId && state->lastTop != kEmberCompendiumId) {
            if (const auto cue = catalog->stateCues.find(std::string(topStateId)); cue != catalog->stateCues.end()) {
                PlayAudioCue(registry, cue->second);
            }
            if (topStateId == kRunModalRunEnd) {
                PlayAudioEvent(registry, runWon ? EAudioEvent::Victory : EAudioEvent::Defeat);
            }
        }
        state->lastTop = std::string(topStateId);
    }

    if (auto* audio = registry.ctx().find<FAudioManager>()) {
        const EMusicPlace place = MusicPlaceFor(topStateId, registry.ctx().contains<FBossBattle>(), runWon);
        switch (place) {
        case EMusicPlace::Keep: break;
        case EMusicPlace::Silence: audio->playMusic("", 0.f, catalog->musicFadeSeconds); break;
        case EMusicPlace::Hub: PlayMusicTrack(*audio, *catalog, catalog->hub); break;
        case EMusicPlace::Camp: PlayMusicTrack(*audio, *catalog, catalog->camp); break;
        case EMusicPlace::Boss: PlayMusicTrack(*audio, *catalog, catalog->boss); break;
        case EMusicPlace::Victory: PlayMusicTrack(*audio, *catalog, catalog->victory); break;
        case EMusicPlace::Theme:
            if (const auto position = RunPosition(registry)) {
                if (const FRunSequence* sequence = FindRunSequence(registry);
                    sequence != nullptr && position->theme < sequence->themes.size()) {
                    const auto theme = catalog->themes.find(sequence->themes[position->theme].id);
                    if (theme != catalog->themes.end()) PlayMusicTrack(*audio, *catalog, theme->second);
                }
            }
            break;
        }
    }

    if (topStateId == kEmberBattleId) {
        const auto* combat = registry.ctx().find<FCombatRuntime>();
        const entt::entity actor = combat != nullptr ? combat->currentActor : entt::null;
        if (actor != state->lastActor && registry.valid(actor)) {
            if (registry.all_of<FEnemy>(actor)) PlayAudioEvent(registry, EAudioEvent::TurnEnemy);
            else if (registry.all_of<FPartyMember>(actor)) PlayAudioEvent(registry, EAudioEvent::TurnParty);
        }
        state->lastActor = actor;
    } else {
        state->lastActor = entt::null;
    }
}

} // namespace game
