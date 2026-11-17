#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <entt/entt.hpp>

namespace game {

enum class EAudioOutput : std::uint8_t { Silent, Device, Offline };
enum class EAudioBus : std::uint8_t { Effects, Music };

inline constexpr std::uint32_t kAudioSampleRate = 48000;
inline constexpr std::uint32_t kAudioChannels = 2;
inline constexpr std::size_t kMaxOneShots = 32;

/// F7. `audioEnv` = GAME01P_AUDIO ("" when unset), `videoDriver` = SDL_GetCurrentVideoDriver() ("" when null).
[[nodiscard]] EAudioOutput ResolveAudioOutput(std::string_view audioEnv, std::string_view videoDriver, bool recording);

/// Plays effects and music through miniaudio; lives in registry.ctx(). Never fails the boot: a device or
/// offline engine that cannot open degrades to Silent with one warning.
struct FAudioManager {
    static void Initialize(entt::registry& registry, EAudioOutput requested);
    static void Shutdown(entt::registry& registry);

    [[nodiscard]] EAudioOutput output() const;
    /// Device/Offline: decodes `path` (MA_SOUND_FLAG_DECODE, synchronous) once and keeps it as the template for
    /// playOneShot. Silent: returns true without reading. False = the file cannot be opened or decoded.
    [[nodiscard]] bool preload(const std::string& path);
    /// One effect on the Effects bus (a copy of the preloaded template; an unknown path is preloaded first).
    /// Silent, or kMaxOneShots already playing: nothing.
    void playOneShot(const std::string& path, float volume, float pitch);
    /// Crossfades the Music bus to `path`, looping. "" fades the current track out. The same path as
    /// currentMusic(): nothing. Silent: only currentMusic() changes.
    void playMusic(const std::string& path, float volume, float fadeSeconds);
    [[nodiscard]] const std::string& currentMusic() const;
    /// 0..1, clamped. Silent keeps the value for busVolume().
    void setBusVolume(EAudioBus bus, float volume);
    [[nodiscard]] float busVolume(EAudioBus bus) const;
    /// Releases finished one-shots and faded-out tracks. Called once per frame by Engine::update.
    void update();
    /// Offline only: opens `wavPath` as a 32-bit float, kAudioChannels, kAudioSampleRate WAV. False otherwise.
    [[nodiscard]] bool beginCapture(const std::string& wavPath);
    /// Offline with an open capture: mixes `frames` frames (ma_engine_read_pcm_frames) and appends them;
    /// returns the frames written (0 otherwise).
    std::uint64_t renderCapture(std::uint64_t frames);
    /// Finalizes the WAV header. No-op without an open capture.
    void endCapture();

private:
    struct FState;  // defined in AudioManager.cpp; holds every miniaudio object
    std::shared_ptr<FState> state_;
};

} // namespace game
