#include "core/AnimationLibrary.h"

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"
#include "core/data/FContentLoader.h"

#include <tracy/Tracy.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace game {
namespace {

enum EClipField : std::size_t {
    Id,
    Image,
    FrameWidth,
    FrameHeight,
    Row,
    Start,
    Count,
    Fps,
    Loop,
    Durations,
    AnchorX,
    AnchorY,
    FieldCount
};

constexpr std::array<FContentField, FieldCount> ClipFields{{
    {"id", EContentFieldType::Text, true},
    {"image", EContentFieldType::Id, true},
    {"frame_w", EContentFieldType::Number, true},
    {"frame_h", EContentFieldType::Number, true},
    {"row", EContentFieldType::Number, true},
    {"start", EContentFieldType::Number, true},
    {"count", EContentFieldType::Number, true},
    {"fps", EContentFieldType::Number, false},
    {"loop", EContentFieldType::Boolean, true},
    {"durations", EContentFieldType::NumberArray, false},
    {"anchor_x", EContentFieldType::Number, false},
    {"anchor_y", EContentFieldType::Number, false},
}};

bool ReadInteger(const FContentRow& row, std::size_t field, int& output) {
    const FContentValue* value = row.at(field);
    const double* number = value ? value->asNumber() : nullptr;
    if (number == nullptr
        || !std::isfinite(*number)
        || std::floor(*number) != *number
        || *number < static_cast<double>(std::numeric_limits<int>::min())
        || *number > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    output = static_cast<int>(*number);
    return true;
}

bool ValidatePositiveDurations(const std::vector<double>& durations) {
    for (const double duration : durations) {
        if (!std::isfinite(duration) || duration <= 0.0) {
            return false;
        }
    }
    return true;
}

} // namespace

bool FAnimationLibrary::Initialize(entt::registry& registry, std::string_view assetId) {
    ZoneScopedN("FAnimationLibrary::Initialize");
    if (const FAnimationLibrary* existing = registry.ctx().find<FAnimationLibrary>()) {
        if (existing->assetId_ != assetId) {
            LOG_ERROR("AnimationLibrary is already initialized from '{}'; cannot initialize '{}'.",
                      existing->assetId_, assetId);
            return false;
        }
        return true;
    }

    const FAssetManager* assets = registry.ctx().find<FAssetManager>();
    FResourceManager* resources = registry.ctx().find<FResourceManager>();
    if (assets == nullptr || resources == nullptr) {
        LOG_ERROR("AnimationLibrary needs FAssetManager and FResourceManager in the registry context.");
        return false;
    }

    const FContentSchema schema{assetId, "clips", "id", 1, ClipFields};
    const std::optional<std::vector<FContentRow>> rows = FContentLoader::load(*assets, schema);
    if (!rows) {
        return false;
    }

    FAnimationLibrary library;
    library.clips_.reserve(rows->size());
    library.ids_.reserve(rows->size());

    for (const FContentRow& row : *rows) {
        const std::string clipId = row.text(Id);
        const std::string imageId = row.text(Image);

        int frameWidth = 0;
        int frameHeight = 0;
        int sheetRow = 0;
        int startColumn = 0;
        int frameCount = 0;
        if (!ReadInteger(row, FrameWidth, frameWidth)
            || !ReadInteger(row, FrameHeight, frameHeight)
            || !ReadInteger(row, Row, sheetRow)
            || !ReadInteger(row, Start, startColumn)
            || !ReadInteger(row, Count, frameCount)
            || frameWidth <= 0 || frameHeight <= 0
            || sheetRow < 0 || startColumn < 0 || frameCount <= 0) {
            LOG_ERROR("Animation clip '{}': frame grid values must be positive integers with non-negative row/start.", clipId);
            return false;
        }

        const double fps = row.number(Fps, 0.0);
        const FContentValue* durationsValue = row.at(Durations);
        const std::vector<double>* durationsField = durationsValue ? durationsValue->asNumberArray() : nullptr;
        const std::vector<double> noDurations;
        const std::vector<double>& durations = durationsField ? *durationsField : noDurations;
        const bool hasFps = fps > 0.0;
        const bool hasDurations = !durations.empty();
        const double fpsDuration = hasFps ? 1.0 / fps : 0.0;
        if (hasFps == hasDurations
            || (hasFps && !std::isfinite(fps))
            || (hasFps && (!std::isfinite(fpsDuration) || fpsDuration <= 0.0))
            || (hasDurations && durations.size() != static_cast<std::size_t>(frameCount))
            || (hasDurations && !ValidatePositiveDurations(durations))) {
            LOG_ERROR("Animation clip '{}': provide either positive fps or one positive duration per frame.", clipId);
            return false;
        }

        const std::optional<FTextureId> textureId = resources->loadTexture(*assets, imageId);
        if (!textureId) {
            LOG_ERROR("Animation clip '{}': could not load sprite sheet '{}'.", clipId, imageId);
            return false;
        }
        const std::optional<FTextureSize> textureSize = resources->findSize(*textureId);
        if (!textureSize
            || static_cast<std::int64_t>(sheetRow + std::int64_t{1}) * frameHeight > textureSize->height
            || static_cast<std::int64_t>(startColumn + std::int64_t{frameCount}) * frameWidth > textureSize->width) {
            LOG_ERROR("Animation clip '{}': frame grid exceeds sprite sheet '{}'.", clipId, imageId);
            return false;
        }

        const bool hasAnchorX = row.at(AnchorX) != nullptr;
        if (hasAnchorX != (row.at(AnchorY) != nullptr)) {
            LOG_ERROR("Animation clip '{}': anchor_x and anchor_y come as a pair.", clipId);
            return false;
        }

        FAnimClip clip;
        clip.texture = *textureId;
        clip.loop = row.boolean(Loop, true);
        clip.anchor = hasAnchorX
            ? glm::vec2{static_cast<float>(row.number(AnchorX, 0.0)), static_cast<float>(row.number(AnchorY, 0.0))}
            : glm::vec2{static_cast<float>(frameWidth) * .5f, static_cast<float>(frameHeight) * .5f};
        clip.frames.reserve(static_cast<std::size_t>(frameCount));
        for (int frame = 0; frame < frameCount; ++frame) {
            const double duration = hasFps ? fpsDuration : durations[static_cast<std::size_t>(frame)];
            const double totalDuration = clip.totalDuration + duration;
            if (!std::isfinite(totalDuration)) {
                LOG_ERROR("Animation clip '{}': total frame duration is too large.", clipId);
                return false;
            }
            clip.frames.push_back(FAnimFrame{
                FSourceRect{
                    static_cast<float>((startColumn + frame) * frameWidth),
                    static_cast<float>(sheetRow * frameHeight),
                    static_cast<float>(frameWidth),
                    static_cast<float>(frameHeight)},
                duration});
            clip.totalDuration = totalDuration;
        }

        if (library.clips_.size() >= std::numeric_limits<std::uint32_t>::max()) {
            LOG_ERROR("Animation clip handle capacity exhausted while loading '{}'.", clipId);
            return false;
        }
        const FAnimClipId id{static_cast<std::uint32_t>(library.clips_.size() + 1)};
        library.ids_.emplace(clipId, id);
        library.clips_.push_back(std::move(clip));
    }

    library.assetId_ = assetId;
    registry.ctx().emplace<FAnimationLibrary>(std::move(library));
    LOG_INFO("AnimationLibrary ready ({} clip(s)).", rows->size());
    return true;
}

void FAnimationLibrary::Shutdown(entt::registry& registry) {
    if (registry.ctx().contains<FAnimationLibrary>()) {
        registry.ctx().erase<FAnimationLibrary>();
    }
}

const FAnimationLibrary* FAnimationLibrary::Find(const entt::registry& registry) noexcept {
    return registry.ctx().find<FAnimationLibrary>();
}

std::optional<FAnimClipId> FAnimationLibrary::findClip(std::string_view id) const {
    const auto found = ids_.find(id);
    if (found == ids_.end()) {
        return std::nullopt;
    }
    return found->second;
}

const FAnimClip* FAnimationLibrary::get(FAnimClipId id) const noexcept {
    if (!id || id.value > clips_.size()) {
        return nullptr;
    }
    return &clips_[id.value - 1];
}

} // namespace game
