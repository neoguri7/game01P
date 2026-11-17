#pragma once
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <entt/entt.hpp>
#include "presentation/FAudioCatalog.h"

namespace game {
inline constexpr std::uint32_t kAudioRngSeed = 0x51A0D10u;

enum class EMusicPlace : std::uint8_t { Keep, Hub, Theme, Boss, Camp, Victory, Silence };

struct FPlayedSound {
    std::string cue;
    std::string file;
    float pitch{1.f};
    std::uint64_t frame{0};
};
/// Per-run director memory (registry.ctx()). `played` is filled only while `logPlays` (tests).
struct FAudioDirectorState {
    std::uint64_t frame{0};
    std::string lastTop;
    entt::entity lastActor{entt::null};
    std::mt19937 rng{kAudioRngSeed};
    std::vector<std::uint64_t> lastPlayedFrame;  // per cue; UINT64_MAX = never
    std::vector<std::uint32_t> lastVariant;      // per cue; UINT32_MAX = none
    bool logPlays{false};
    std::vector<FPlayedSound> played;
};

/// The state ids a state_cues key may name: title, hub, prepare, battle, reward, event, camp.
[[nodiscard]] std::span<const std::string_view> AudioStateCueIds();
/// Boot: needs FAssetManager, FContentRegistry and FRunSequence in ctx. Parses data/audio, preloads every cue
/// file into FAudioManager when present, stores FAudioCatalog + FAudioDirectorState. False = invalid content.
[[nodiscard]] bool InitializeAudio(entt::registry& registry);
void PlayAudioCue(entt::registry& registry, FAudioCueId cue);
void PlayAudioEvent(entt::registry& registry, EAudioEvent event);
[[nodiscard]] FSkillAudio SkillAudio(const entt::registry& registry, std::string_view skillId);
[[nodiscard]] EMusicPlace MusicPlaceFor(std::string_view topStateId, bool bossBattle, bool runWon);
/// Once per frame (the Engine frame observer), after the frame driver and before render().
void UpdateAudioDirector(entt::registry& registry, std::string_view topStateId);
} // namespace game
