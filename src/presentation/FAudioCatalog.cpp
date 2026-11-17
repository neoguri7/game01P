#include "presentation/FAudioCatalog.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

#include "core/AssetManager.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {
namespace {

/// Thrown by the helpers below and turned into the std::unexpected of ParseAudioCatalog.
struct FAudioParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void Fail(const std::string& message) { throw FAudioParseError(message); }

const nlohmann::json& ObjectAt(const nlohmann::json& parent, const char* key) {
    const auto found = parent.find(key);
    if (found == parent.end() || !found->is_object()) Fail(std::string("'") + key + "' must be an object");
    return *found;
}

float NumberIn(const nlohmann::json& object, const char* key, float fallback, float low, float high,
               const std::string& where) {
    const auto found = object.find(key);
    if (found == object.end()) return fallback;
    if (!found->is_number()) Fail(where + "." + key + " must be a number");
    const float value = found->get<float>();
    if (value < low || value > high) Fail(where + "." + key + " must be between " + std::to_string(low) + " and " + std::to_string(high));
    return value;
}

/// Extension check, then resolution under the asset roots.
std::string ResolveFile(const nlohmann::json& name, const FAssetManager& assets, const std::string& where) {
    if (!name.is_string()) Fail(where + " must name a file");
    const std::string file = name.get<std::string>();
    std::string extension;
    if (const std::size_t dot = file.rfind('.'); dot != std::string::npos) extension = file.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".wav" && extension != ".mp3" && extension != ".flac") {
        Fail("audio file '" + file + "' has an unsupported extension (use .wav, .mp3 or .flac)");
    }
    const auto resolved = assets.resolvePath(file, "");
    if (!resolved) Fail("audio file '" + file + "' was not found under any asset root");
    return *resolved;
}

FAudioCueId CueRef(const FAudioCatalog& catalog, const nlohmann::json& value, const std::string& where) {
    if (!value.is_string()) Fail(where + " must be a cue id");
    const FAudioCueId cue = catalog.findCue(value.get<std::string>());
    if (cue == kNoAudioCue) Fail(where + " names the undefined cue '" + value.get<std::string>() + "'");
    return cue;
}

FSkillAudio SkillAudioOf(const FAudioCatalog& catalog, const nlohmann::json& value, const std::string& where) {
    if (!value.is_object()) Fail(where + " must be an object");
    FSkillAudio audio;
    for (const auto& [key, entry] : value.items()) {
        if (key == "release") audio.release = CueRef(catalog, entry, where + ".release");
        else if (key == "land") audio.land = CueRef(catalog, entry, where + ".land");
        else Fail(where + " has the unknown key '" + key + "'");
    }
    if (audio.release == kNoAudioCue && audio.land == kNoAudioCue) Fail(where + " needs release and/or land");
    return audio;
}

FMusicId AddTrack(FAudioCatalog& catalog, const nlohmann::json& value, const FAssetManager& assets,
                  const std::string& id, const std::string& where) {
    if (!value.is_object()) Fail(where + " must be an object");
    const auto file = value.find("file");
    if (file == value.end()) Fail(where + " needs a file");
    FMusicTrack track;
    track.id = id;
    track.file = ResolveFile(*file, assets, where + ".file");
    track.volume = NumberIn(value, "volume", 1.f, 0.f, 1.f, where);
    catalog.music.push_back(std::move(track));
    return static_cast<FMusicId>(catalog.music.size() - 1);
}

FAudioCatalog Build(const nlohmann::json& document, const FAssetManager& assets,
                    const gameplay::FContentRegistry& content, std::span<const std::string> themeIds,
                    std::span<const std::string_view> stateIds) {
    if (!document.is_object()) Fail("the audio document must be an object");
    static const std::set<std::string> kTop{"schema_version", "_comment", "cues", "skills", "skill_fallback",
                                            "events", "state_cues", "music"};
    for (const auto& [key, value] : document.items()) {
        if (!kTop.contains(key)) Fail("unknown top-level key '" + key + "'");
    }
    const auto version = document.find("schema_version");
    if (version == document.end() || !version->is_number_integer() || version->get<int>() != 1) {
        Fail("schema_version must be 1");
    }

    FAudioCatalog catalog;
    const nlohmann::json& cues = ObjectAt(document, "cues");
    if (cues.empty()) Fail("'cues' needs at least one entry");
    for (const auto& [id, value] : cues.items()) {
        if (!value.is_object()) Fail("cue '" + id + "' must be an object");
        const std::string where = "cues." + id;
        for (const auto& [key, ignored] : value.items()) {
            if (key != "files" && key != "volume" && key != "pitch_jitter") Fail(where + " has the unknown key '" + key + "'");
        }
        const auto files = value.find("files");
        if (files == value.end() || !files->is_array() || files->empty()) Fail(where + ".files must be a non-empty array");
        FAudioCue cue;
        cue.id = id;
        for (const auto& file : *files) cue.files.push_back(ResolveFile(file, assets, where + ".files"));
        cue.volume = NumberIn(value, "volume", 1.f, 0.f, 1.f, where);
        cue.pitchJitter = NumberIn(value, "pitch_jitter", 0.f, 0.f, 0.25f, where);
        catalog.cues.push_back(std::move(cue));
    }

    const nlohmann::json& fallbackJson = ObjectAt(document, "skill_fallback");
    for (const auto& [key, value] : fallbackJson.items()) {
        if (key != "physical" && key != "magical" && key != "support") Fail("skill_fallback has the unknown key '" + key + "'");
    }
    FSkillAudio fallback[3];
    constexpr const char* kFallbackKeys[3] = {"physical", "magical", "support"};
    for (int index = 0; index < 3; ++index) {
        const auto found = fallbackJson.find(kFallbackKeys[index]);
        if (found == fallbackJson.end()) Fail(std::string("skill_fallback needs '") + kFallbackKeys[index] + "'");
        fallback[index] = SkillAudioOf(catalog, *found, std::string("skill_fallback.") + kFallbackKeys[index]);
    }
    catalog.unknownSkill = fallback[0];

    const nlohmann::json& skills = ObjectAt(document, "skills");
    std::unordered_map<std::string, FSkillAudio> authored;
    for (const auto& [id, value] : skills.items()) {
        if (content.skills.find(id) == nullptr) Fail("skills names '" + id + "', which is not a content skill");
        authored.emplace(id, SkillAudioOf(catalog, value, "skills." + id));
    }
    for (const auto& row : content.skills.rows) {
        if (const auto found = authored.find(row.id); found != authored.end()) {
            catalog.skills.emplace(row.id, found->second);
        } else if (row.target == "ally" || row.target == "self") {
            catalog.skills.emplace(row.id, fallback[2]);
        } else if (row.damageType == "magical") {
            catalog.skills.emplace(row.id, fallback[1]);
        } else {
            catalog.skills.emplace(row.id, fallback[0]);
        }
    }

    const nlohmann::json& events = ObjectAt(document, "events");
    for (const auto& [key, value] : events.items()) {
        if (std::find(kAudioEventKeys.begin(), kAudioEventKeys.end(), key) == kAudioEventKeys.end()) {
            Fail("events has the unknown key '" + key + "'");
        }
    }
    for (std::size_t index = 0; index < kAudioEventCount; ++index) {
        const std::string key(kAudioEventKeys[index]);
        const auto found = events.find(key);
        if (found == events.end()) Fail("events needs '" + key + "'");
        catalog.events[index] = CueRef(catalog, *found, "events." + key);
    }

    const nlohmann::json& stateCues = ObjectAt(document, "state_cues");
    for (const auto& [state, value] : stateCues.items()) {
        if (std::find(stateIds.begin(), stateIds.end(), std::string_view(state)) == stateIds.end()) {
            Fail("state_cues names '" + state + "', which is not a known state");
        }
        catalog.stateCues.emplace(state, CueRef(catalog, value, "state_cues." + state));
    }

    const nlohmann::json& music = ObjectAt(document, "music");
    catalog.musicFadeSeconds = NumberIn(music, "fade_seconds", 1.5f, 0.f, 5.f, "music");
    for (const auto& [key, value] : music.items()) {
        if (key != "fade_seconds" && key != "hub" && key != "camp" && key != "boss" && key != "victory" && key != "themes") {
            Fail("music has the unknown key '" + key + "'");
        }
    }
    struct { const char* key; FMusicId* target; } const places[] = {
        {"hub", &catalog.hub}, {"camp", &catalog.camp}, {"boss", &catalog.boss}, {"victory", &catalog.victory}};
    for (const auto& place : places) {
        const auto found = music.find(place.key);
        if (found == music.end()) Fail(std::string("music needs '") + place.key + "'");
        *place.target = AddTrack(catalog, *found, assets, place.key, std::string("music.") + place.key);
    }
    const nlohmann::json& themes = ObjectAt(music, "themes");
    for (const auto& [theme, value] : themes.items()) {
        if (std::find(themeIds.begin(), themeIds.end(), theme) == themeIds.end()) {
            Fail("music.themes names the unknown theme '" + theme + "'");
        }
    }
    for (const std::string& theme : themeIds) {
        const auto found = themes.find(theme);
        if (found == themes.end()) Fail("music.themes is missing the theme '" + theme + "'");
        catalog.themes.emplace(theme, AddTrack(catalog, *found, assets, theme, "music.themes." + theme));
    }
    return catalog;
}

}  // namespace

FAudioCueId FAudioCatalog::findCue(std::string_view id) const {
    for (std::size_t index = 0; index < cues.size(); ++index) {
        if (cues[index].id == id) return static_cast<FAudioCueId>(index);
    }
    return kNoAudioCue;
}

std::expected<FAudioCatalog, std::string> ParseAudioCatalog(
    const nlohmann::json& document, const FAssetManager& assets, const gameplay::FContentRegistry& content,
    std::span<const std::string> themeIds, std::span<const std::string_view> stateIds) {
    try {
        return Build(document, assets, content, themeIds, stateIds);
    } catch (const FAudioParseError& error) {
        return std::unexpected(std::string(error.what()));
    } catch (const nlohmann::json::exception& error) {
        return std::unexpected(std::string("audio.json: ") + error.what());
    }
}

} // namespace game
