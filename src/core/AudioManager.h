#pragma once
#include <miniaudio.h>
#include <spdlog/spdlog.h>
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <tracy/Tracy.hpp>

namespace game {

/// Audio manager — lives in registry.ctx() for lifecycle management.
/// Plays SFX/music. Simple fire-and-forget + persistent sound.
struct FAudioManager {
    ma_engine engine{};

    static void Initialize(entt::registry& reg) {
        if (reg.ctx().contains<FAudioManager>()) return;

        auto& mgr = reg.ctx().emplace<FAudioManager>();
        ma_engine_config config = ma_engine_config_init();
        ma_result result = ma_engine_init(&config, &mgr.engine);
        if (result != MA_SUCCESS) {
            spdlog::error("[Audio] Failed to initialize miniaudio engine: {}", static_cast<int>(result));
            reg.ctx().erase<FAudioManager>();
            return;
        }
        spdlog::info("[Audio] miniaudio engine initialized.");
    }

    static void Shutdown(entt::registry& reg) {
        if (auto* mgr = reg.ctx().find<FAudioManager>()) {
            ma_engine_uninit(&mgr->engine);
        }
        reg.ctx().erase<FAudioManager>();
    }

    // Fire-and-forget SFX
    void playSoundOneShot(const std::string& path, float volume = 1.f) {
        ma_sound sound{};
        ma_result res = ma_sound_init_from_file(&engine, path.c_str(), 0, nullptr, nullptr, &sound);
        if (res != MA_SUCCESS) {
            spdlog::warn("[Audio] Failed to load sound: {}", path);
            return;
        }
        ma_sound_set_volume(&sound, volume);
        ma_sound_start(&sound);
        trackedSounds_.push_back(sound);
    }

    // Persistent (e.g. music loop) — returns a handle for later control
    uint64_t playSoundPersistent(const std::string& path, bool loop = true, float volume = 1.f) {
        auto* sound = new ma_sound{};
        ma_result res = ma_sound_init_from_file(&engine, path.c_str(), 0, nullptr, nullptr, sound);
        if (res != MA_SUCCESS) {
            spdlog::warn("[Audio] Failed to load sound: {}", path);
            delete sound;
            return 0;
        }
        ma_sound_set_volume(sound, volume);
        ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(sound);

        uint64_t handle = nextSoundId_++;
        persistentSounds_[handle] = sound;
        return handle;
    }

    void stopSound(uint64_t handle) {
        if (auto it = persistentSounds_.find(handle); it != persistentSounds_.end()) {
            ma_sound_stop(it->second);
            ma_sound_uninit(it->second);
            delete it->second;
            persistentSounds_.erase(it);
        }
    }

    // Cleanup tracked one-shot sounds
    void gcOneShots() {
        for (auto& s : trackedSounds_) {
            if (ma_sound_is_playing(&s) == MA_FALSE) {
                ma_sound_uninit(&s);
            }
        }
        trackedSounds_.erase(
            std::remove_if(trackedSounds_.begin(), trackedSounds_.end(),
                [](ma_sound& s) { return ma_sound_is_playing(&s) == MA_FALSE; }),
            trackedSounds_.end());
    }

private:
    uint64_t nextSoundId_{1};
    std::vector<ma_sound> trackedSounds_;
    std::unordered_map<uint64_t, ma_sound*> persistentSounds_;
};

} // namespace game
