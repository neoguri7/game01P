#pragma once
#include <miniaudio.h>
#include <spdlog/spdlog.h>
#include <entt/entt.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <tracy/Tracy.hpp>

namespace game {

/// Audio manager — lives in registry.ctx() for lifecycle management.
/// Plays SFX/music. Simple fire-and-forget + persistent sound.
struct FAudioManager {
    static void Initialize(entt::registry& reg) {
        if (reg.ctx().contains<FAudioManager>()) return;

        auto& mgr = reg.ctx().emplace<FAudioManager>();
        ma_engine_config config = ma_engine_config_init();
        ma_result result = ma_engine_init(&config, &mgr.state_->engine);
        if (result != MA_SUCCESS) {
            spdlog::error("[Audio] Failed to initialize miniaudio engine: {}", static_cast<int>(result));
            reg.ctx().erase<FAudioManager>();
            return;
        }
        mgr.state_->initialized = true;
        spdlog::info("[Audio] miniaudio engine initialized.");
    }

    static void Shutdown(entt::registry& reg) {
        if (auto* mgr = reg.ctx().find<FAudioManager>()) {
            mgr->shutdown();
        }
        reg.ctx().erase<FAudioManager>();
    }

    // Fire-and-forget SFX
    void playSoundOneShot(const std::string& path, float volume = 1.f) {
        auto* sound = new ma_sound{};
        ma_result res = ma_sound_init_from_file(&state_->engine, path.c_str(), 0, nullptr, nullptr, sound);
        if (res != MA_SUCCESS) {
            spdlog::warn("[Audio] Failed to load sound: {}", path);
            delete sound;
            return;
        }
        ma_sound_set_volume(sound, volume);
        ma_sound_start(sound);
        state_->trackedSounds.push_back(sound);
    }

    // Persistent (e.g. music loop) — returns a handle for later control
    uint64_t playSoundPersistent(const std::string& path, bool loop = true, float volume = 1.f) {
        auto* sound = new ma_sound{};
        ma_result res = ma_sound_init_from_file(&state_->engine, path.c_str(), 0, nullptr, nullptr, sound);
        if (res != MA_SUCCESS) {
            spdlog::warn("[Audio] Failed to load sound: {}", path);
            delete sound;
            return 0;
        }
        ma_sound_set_volume(sound, volume);
        ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(sound);

        uint64_t handle = state_->nextSoundId++;
        state_->persistentSounds[handle] = sound;
        return handle;
    }

    void stopSound(uint64_t handle) {
        if (auto it = state_->persistentSounds.find(handle); it != state_->persistentSounds.end()) {
            ma_sound_stop(it->second);
            ma_sound_uninit(it->second);
            delete it->second;
            state_->persistentSounds.erase(it);
        }
    }

    // Cleanup tracked one-shot sounds
    void gcOneShots() {
        state_->trackedSounds.erase(
            std::remove_if(state_->trackedSounds.begin(), state_->trackedSounds.end(),
                [](ma_sound* sound) {
                    if (ma_sound_is_playing(sound) == MA_FALSE) {
                        ma_sound_uninit(sound);
                        delete sound;
                        return true;
                    }
                    return false;
                }),
            state_->trackedSounds.end());
    }

private:
    struct FAudioState {
        ~FAudioState() {
            shutdown();
        }

        void shutdown() {
            for (auto* sound : trackedSounds) {
                ma_sound_stop(sound);
                ma_sound_uninit(sound);
                delete sound;
            }
            trackedSounds.clear();

            for (auto& [_, sound] : persistentSounds) {
                ma_sound_stop(sound);
                ma_sound_uninit(sound);
                delete sound;
            }
            persistentSounds.clear();

            if (initialized) {
                ma_engine_uninit(&engine);
                initialized = false;
            }
        }

        ma_engine engine{};
        bool initialized{false};
        uint64_t nextSoundId{1};
        std::vector<ma_sound*> trackedSounds;
        std::unordered_map<uint64_t, ma_sound*> persistentSounds;
    };

    void shutdown() {
        state_->shutdown();
    }

    std::shared_ptr<FAudioState> state_{std::make_shared<FAudioState>()};
};

} // namespace game
