#include "core/AudioManager.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <miniaudio.h>

#include "core/Logger.h"

namespace game {

namespace {

const char* OutputName(EAudioOutput output) {
    switch (output) {
    case EAudioOutput::Silent: return "Silent";
    case EAudioOutput::Device: return "Device";
    case EAudioOutput::Offline: return "Offline";
    }
    return "Silent";
}

void DestroySound(std::unique_ptr<ma_sound>& sound) {
    if (sound) {
        ma_sound_uninit(sound.get());
        sound.reset();
    }
}

}  // namespace

struct FAudioManager::FState {
    EAudioOutput output{EAudioOutput::Silent};
    ma_engine engine{};
    bool engineReady{false};
    ma_sound_group effects{};
    ma_sound_group music{};
    bool groupsReady{false};
    std::unordered_map<std::string, std::unique_ptr<ma_sound>> templates;
    std::vector<std::unique_ptr<ma_sound>> oneShots;
    std::vector<std::unique_ptr<ma_sound>> fading;
    std::unique_ptr<ma_sound> track;
    std::string currentMusic;
    float volumes[2]{1.f, 1.f};
    ma_encoder encoder{};
    bool capturing{false};
    std::vector<float> scratch;

    ~FState() {
        if (capturing) {
            ma_encoder_uninit(&encoder);
            capturing = false;
        }
        for (auto& sound : oneShots) DestroySound(sound);
        for (auto& sound : fading) DestroySound(sound);
        DestroySound(track);
        for (auto& [path, sound] : templates) DestroySound(sound);
        if (groupsReady) {
            ma_sound_group_uninit(&effects);
            ma_sound_group_uninit(&music);
            groupsReady = false;
        }
        if (engineReady) {
            ma_engine_uninit(&engine);
            engineReady = false;
        }
    }
};

EAudioOutput ResolveAudioOutput(std::string_view audioEnv, std::string_view videoDriver, bool recording) {
    if (recording) return EAudioOutput::Offline;
    if (audioEnv == "0") return EAudioOutput::Silent;
    if (audioEnv == "1") return EAudioOutput::Device;
    if (videoDriver == "dummy") return EAudioOutput::Silent;
    return EAudioOutput::Device;
}

void FAudioManager::Initialize(entt::registry& registry, EAudioOutput requested) {
    registry.ctx().erase<FAudioManager>();
    auto& manager = registry.ctx().emplace<FAudioManager>();
    manager.state_ = std::make_shared<FState>();
    FState& state = *manager.state_;
    state.output = requested;
    if (requested != EAudioOutput::Silent) {
        ma_engine_config config = ma_engine_config_init();
        config.channels = kAudioChannels;
        config.sampleRate = kAudioSampleRate;
        if (requested == EAudioOutput::Offline) config.noDevice = MA_TRUE;
        if (ma_engine_init(&config, &state.engine) != MA_SUCCESS) {
            LOG_WARN("[Audio] {} engine could not open; falling back to silence", OutputName(requested));
            state.output = EAudioOutput::Silent;
        } else {
            state.engineReady = true;
            const bool effectsOk = ma_sound_group_init(&state.engine, 0, nullptr, &state.effects) == MA_SUCCESS;
            const bool musicOk = effectsOk && ma_sound_group_init(&state.engine, 0, nullptr, &state.music) == MA_SUCCESS;
            if (!musicOk) {
                if (effectsOk) ma_sound_group_uninit(&state.effects);
                ma_engine_uninit(&state.engine);
                state.engineReady = false;
                LOG_WARN("[Audio] sound groups could not be created; falling back to silence");
                state.output = EAudioOutput::Silent;
            } else {
                state.groupsReady = true;
            }
        }
    }
    LOG_INFO("[Audio] output: {}", OutputName(state.output));
}

void FAudioManager::Shutdown(entt::registry& registry) { registry.ctx().erase<FAudioManager>(); }

EAudioOutput FAudioManager::output() const { return state_->output; }

bool FAudioManager::preload(const std::string& path) {
    FState& s = *state_;
    if (s.output == EAudioOutput::Silent) return true;
    if (s.templates.contains(path)) return true;
    auto sound = std::make_unique<ma_sound>();
    if (ma_sound_init_from_file(&s.engine, path.c_str(), MA_SOUND_FLAG_DECODE, &s.effects, nullptr, sound.get()) !=
        MA_SUCCESS) {
        return false;
    }
    s.templates.emplace(path, std::move(sound));
    return true;
}

void FAudioManager::playOneShot(const std::string& path, float volume, float pitch) {
    FState& s = *state_;
    if (s.output == EAudioOutput::Silent || s.oneShots.size() >= kMaxOneShots) return;
    if (!s.templates.contains(path) && !preload(path)) {
        LOG_WARN("[Audio] cannot play {}", path);
        return;
    }
    auto copy = std::make_unique<ma_sound>();
    if (ma_sound_init_copy(&s.engine, s.templates.at(path).get(), 0, &s.effects, copy.get()) != MA_SUCCESS) return;
    ma_sound_set_volume(copy.get(), volume);
    ma_sound_set_pitch(copy.get(), pitch);
    ma_sound_start(copy.get());
    s.oneShots.push_back(std::move(copy));
}

void FAudioManager::playMusic(const std::string& path, float volume, float fadeSeconds) {
    FState& s = *state_;
    if (path == s.currentMusic) return;
    s.currentMusic = path;
    if (s.output == EAudioOutput::Silent) return;
    const auto ms = static_cast<ma_uint64>(std::max(0.f, fadeSeconds) * 1000.f);
    if (s.track) {
        ma_sound_stop_with_fade_in_milliseconds(s.track.get(), ms);
        s.fading.push_back(std::move(s.track));
    }
    if (path.empty()) return;
    auto next = std::make_unique<ma_sound>();
    // Flags 0: the encoded file stays in memory and is decoded while mixing; never STREAM or ASYNC (offline
    // capture must be deterministic).
    if (ma_sound_init_from_file(&s.engine, path.c_str(), 0, &s.music, nullptr, next.get()) != MA_SUCCESS) {
        LOG_WARN("[Audio] cannot load music {}", path);
        return;
    }
    ma_sound_set_looping(next.get(), MA_TRUE);
    ma_sound_set_volume(next.get(), volume);
    ma_sound_set_fade_in_milliseconds(next.get(), 0.f, 1.f, ms);
    ma_sound_start(next.get());
    s.track = std::move(next);
}

const std::string& FAudioManager::currentMusic() const { return state_->currentMusic; }

void FAudioManager::setBusVolume(EAudioBus bus, float volume) {
    FState& s = *state_;
    const float clamped = std::clamp(volume, 0.f, 1.f);
    s.volumes[static_cast<std::size_t>(bus)] = clamped;
    if (s.groupsReady) {
        ma_sound_group_set_volume(bus == EAudioBus::Effects ? &s.effects : &s.music, clamped);
    }
}

float FAudioManager::busVolume(EAudioBus bus) const { return state_->volumes[static_cast<std::size_t>(bus)]; }

void FAudioManager::update() {
    FState& s = *state_;
    auto release = [](std::vector<std::unique_ptr<ma_sound>>& sounds) {
        std::erase_if(sounds, [](std::unique_ptr<ma_sound>& sound) {
            if (ma_sound_is_playing(sound.get()) == MA_TRUE) return false;
            DestroySound(sound);
            return true;
        });
    };
    release(s.oneShots);
    release(s.fading);
}

bool FAudioManager::beginCapture(const std::string& wavPath) {
    FState& s = *state_;
    if (s.output != EAudioOutput::Offline || s.capturing) return false;
    const ma_encoder_config config =
        ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, kAudioChannels, kAudioSampleRate);
    if (ma_encoder_init_file(wavPath.c_str(), &config, &s.encoder) != MA_SUCCESS) return false;
    s.capturing = true;
    return true;
}

std::uint64_t FAudioManager::renderCapture(std::uint64_t frames) {
    FState& s = *state_;
    if (s.output != EAudioOutput::Offline || !s.capturing) return 0;
    s.scratch.assign(static_cast<std::size_t>(frames) * kAudioChannels, 0.f);
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&s.engine, s.scratch.data(), frames, &read);  // an unread tail stays zero
    ma_uint64 written = 0;
    ma_encoder_write_pcm_frames(&s.encoder, s.scratch.data(), frames, &written);
    return written;
}

void FAudioManager::endCapture() {
    FState& s = *state_;
    if (!s.capturing) return;
    ma_encoder_uninit(&s.encoder);
    s.capturing = false;
}

} // namespace game
