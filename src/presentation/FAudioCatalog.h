#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace game {
struct FAssetManager;
namespace gameplay { struct FContentRegistry; }

using FAudioCueId = std::uint32_t;
inline constexpr FAudioCueId kNoAudioCue = 0xFFFFFFFFu;
using FMusicId = std::uint32_t;
inline constexpr FMusicId kNoMusic = 0xFFFFFFFFu;

/// The fixed moments the code plays (audio.json "events"); every slot is required.
enum class EAudioEvent : std::uint8_t { UiClick, Footstep, UnitHit, UnitDeath, Crit, TurnParty, TurnEnemy, Victory, Defeat, Count };
inline constexpr std::size_t kAudioEventCount = static_cast<std::size_t>(EAudioEvent::Count);
inline constexpr std::array<std::string_view, kAudioEventCount> kAudioEventKeys{
    "ui_click", "footstep", "unit_hit", "unit_death", "crit", "turn_party", "turn_enemy", "victory", "defeat"};

struct FAudioCue {
    std::string id;
    std::vector<std::string> files;  // resolved on-disk paths (FAssetManager::resolvePath), one per variant
    float volume{1.f};
    float pitchJitter{0.f};
};
struct FSkillAudio {
    FAudioCueId release{kNoAudioCue};
    FAudioCueId land{kNoAudioCue};
};
struct FMusicTrack {
    std::string id;    // "hub", "camp", "boss", "victory" or a theme id
    std::string file;  // resolved on-disk path
    float volume{1.f};
};
struct FAudioCatalog {
    std::vector<FAudioCue> cues;
    std::unordered_map<std::string, FSkillAudio> skills;  // every content skill: its entry or its fallback
    FSkillAudio unknownSkill;                              // skill_fallback.physical, for ids content does not know
    std::array<FAudioCueId, kAudioEventCount> events{};
    std::unordered_map<std::string, FAudioCueId> stateCues;  // state catalog id -> cue
    std::vector<FMusicTrack> music;
    FMusicId hub{kNoMusic}, camp{kNoMusic}, boss{kNoMusic}, victory{kNoMusic};
    std::unordered_map<std::string, FMusicId> themes;  // theme id -> track
    float musicFadeSeconds{1.5f};

    [[nodiscard]] FAudioCueId findCue(std::string_view id) const;  // kNoAudioCue when absent
};

/// Validates and resolves audio.json (§5.3). `themeIds` = the run sequence's theme ids; `stateIds` = the
/// state ids a state_cues key may name. The error names the offending key, id or file.
[[nodiscard]] std::expected<FAudioCatalog, std::string> ParseAudioCatalog(
    const nlohmann::json& document, const FAssetManager& assets, const gameplay::FContentRegistry& content,
    std::span<const std::string> themeIds, std::span<const std::string_view> stateIds);
} // namespace game
