#include "presentation/FUiLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace game::ui {
namespace {

// One table per zone payload type, in the same order, so a spec zone and its resolved rect share an index.
template <class T>
constexpr std::array<std::pair<std::string_view, T FHudZones<T>::*>, 10> ZoneTable() {
    return {{
        {"title", &FHudZones<T>::title},
        {"turn_order", &FHudZones<T>::turnOrder},
        {"status", &FHudZones<T>::status},
        {"party", &FHudZones<T>::party},
        {"unit", &FHudZones<T>::unit},
        {"skills", &FHudZones<T>::skills},
        {"end_turn", &FHudZones<T>::endTurn},
        {"journal", &FHudZones<T>::journal},
        {"hints", &FHudZones<T>::hints},
        {"targets", &FHudZones<T>::targets},
    }};
}

struct FNamedAnchor {
    std::string_view name;
    EHorizontalAnchor horizontal;
    EVerticalAnchor vertical;
};

constexpr std::array<FNamedAnchor, 6> kAnchors{{
    {"top-left", EHorizontalAnchor::Left, EVerticalAnchor::Top},
    {"top-center", EHorizontalAnchor::Center, EVerticalAnchor::Top},
    {"top-right", EHorizontalAnchor::Right, EVerticalAnchor::Top},
    {"bottom-left", EHorizontalAnchor::Left, EVerticalAnchor::Bottom},
    {"bottom-center", EHorizontalAnchor::Center, EVerticalAnchor::Bottom},
    {"bottom-right", EHorizontalAnchor::Right, EVerticalAnchor::Bottom},
}};

std::expected<FInsets, std::string> ParseInsets(const nlohmann::json& object, std::string_view name) {
    FInsets insets{object.at("left").get<float>(), object.at("right").get<float>(), object.at("top").get<float>(),
                   object.at("bottom").get<float>()};
    if (insets.left < 0.f || insets.right < 0.f || insets.top < 0.f || insets.bottom < 0.f) {
        return std::unexpected(std::string(name) + " insets must not be negative");
    }
    return insets;
}

std::expected<FUiLayoutSpec, std::string> Parse(const nlohmann::json& document) {
    FUiLayoutSpec spec;
    const auto& design = document.at("design");
    spec.designWidth = design.at("width").get<float>();
    spec.designHeight = design.at("height").get<float>();
    spec.minWidth = design.at("min_width").get<float>();
    if (spec.designWidth <= 0.f || spec.designHeight <= 0.f) return std::unexpected("design size must be positive");
    if (spec.minWidth <= 0.f || spec.minWidth > spec.designWidth) {
        return std::unexpected("design.min_width must be positive and not exceed design.width");
    }

    const auto bands = ParseInsets(document.at("bands"), "bands");
    if (!bands) return std::unexpected(bands.error());
    spec.bands = *bands;
    if (spec.bands.left + spec.bands.right >= spec.minWidth || spec.bands.top + spec.bands.bottom >= spec.designHeight) {
        return std::unexpected("bands leave no viewport");
    }

    // WP-12: the density threshold is data, not a renderer constant. The whole "combat" object is optional
    // so an older document keeps the default; a present value must be a positive integer.
    if (const auto combat = document.find("combat"); combat != document.end()) {
        if (!combat->is_object()) return std::unexpected("combat must be an object");
        if (const auto threshold = combat->find("intent_compact_threshold"); threshold != combat->end()) {
            if (!threshold->is_number_integer()) {
                return std::unexpected("combat.intent_compact_threshold must be a whole number");
            }
            spec.intentCompactThreshold = threshold->get<int>();
            if (spec.intentCompactThreshold < 1) {
                return std::unexpected("combat.intent_compact_threshold must be positive");
            }
        }
    }

    // WP-24b: the onboarding card placement is optional (older documents keep the defaults); when present
    // every number must be sane and the design canvas must leave a usable side card beside the modal band.
    if (const auto onboarding = document.find("onboarding"); onboarding != document.end()) {
        if (!onboarding->is_object()) return std::unexpected("onboarding must be an object");
        const auto number = [&](const char* key, float& target) -> std::optional<std::string> {
            const auto value = onboarding->find(key);
            if (value == onboarding->end()) return std::nullopt;
            if (!value->is_number()) return std::string("onboarding.") + key + " must be a number";
            target = value->get<float>();
            return std::nullopt;
        };
        if (const auto error = number("strip_width", spec.onboarding.stripWidth)) return std::unexpected(*error);
        if (const auto error = number("side_width", spec.onboarding.sideWidth)) return std::unexpected(*error);
        if (const auto error = number("top_margin", spec.onboarding.topMargin)) return std::unexpected(*error);
        if (const auto error = number("side_margin", spec.onboarding.sideMargin)) return std::unexpected(*error);
        if (const auto error = number("padding", spec.onboarding.padding)) return std::unexpected(*error);
        if (const auto error = number("modal_clearance_width", spec.onboarding.modalClearanceWidth)) {
            return std::unexpected(*error);
        }
        const FOnboardingSpec& card = spec.onboarding;
        if (card.stripWidth <= 0.f || card.stripWidth > spec.designWidth) {
            return std::unexpected("onboarding.strip_width must be positive and not exceed design.width");
        }
        if (card.sideWidth <= 0.f || card.sideWidth > spec.designWidth) {
            return std::unexpected("onboarding.side_width must be positive and not exceed design.width");
        }
        if (card.topMargin < 0.f) return std::unexpected("onboarding.top_margin must not be negative");
        if (card.sideMargin < 0.f) return std::unexpected("onboarding.side_margin must not be negative");
        if (card.padding < 0.f || card.padding * 2.f >= std::min(card.stripWidth, card.sideWidth)) {
            return std::unexpected("onboarding.padding must not be negative and must leave room for text");
        }
        if (card.modalClearanceWidth <= 0.f || card.modalClearanceWidth >= spec.minWidth) {
            return std::unexpected("onboarding.modal_clearance_width must be positive and below design.min_width");
        }
        const float sideRoomAtMin = (spec.minWidth - card.modalClearanceWidth) * .5f - 2.f * card.sideMargin;
        if (sideRoomAtMin < kOnboardingSideMinWidth) {
            return std::unexpected("onboarding leaves no usable side card beside the modal at design.min_width");
        }
    }

    const auto& zones = document.at("zones");
    constexpr auto specZones = ZoneTable<FZoneSpec>();
    for (const auto& [name, member] : specZones) {
        const auto found = zones.find(std::string(name));
        if (found == zones.end()) return std::unexpected("zones." + std::string(name) + " is missing");
        FZoneSpec zone;
        const auto anchorName = found->at("anchor").get<std::string>();
        const auto anchor = std::ranges::find(kAnchors, anchorName, &FNamedAnchor::name);
        if (anchor == kAnchors.end()) {
            return std::unexpected("zones." + std::string(name) + ".anchor '" + anchorName + "' is not a known anchor");
        }
        zone.horizontal = anchor->horizontal;
        zone.vertical = anchor->vertical;
        const auto& rect = found->at("rect");
        if (!rect.is_array() || rect.size() != 4) {
            return std::unexpected("zones." + std::string(name) + ".rect must be [x, y, w, h]");
        }
        zone.rect = {rect[0].get<float>(), rect[1].get<float>(), rect[2].get<float>(), rect[3].get<float>()};
        if (zone.rect.w <= 0.f || zone.rect.h <= 0.f) {
            return std::unexpected("zones." + std::string(name) + " needs a positive size");
        }
        spec.zones.*member = zone;
    }
    return spec;
}

// Whole pixels keep portraits, slots and text crisp; edges are rounded rather than sizes so neighbours stay flush.
FRect Snapped(float x, float y, float right, float bottom) {
    const float left = std::round(x);
    const float top = std::round(y);
    return {left, top, std::round(right) - left, std::round(bottom) - top};
}

FRect Place(const FZoneSpec& zone, float extraX, float extraY, float scale) {
    const float dx = zone.horizontal == EHorizontalAnchor::Left ? 0.f
        : zone.horizontal == EHorizontalAnchor::Center ? extraX * .5f : extraX;
    const float dy = zone.vertical == EVerticalAnchor::Top ? 0.f : extraY;
    return Snapped((zone.rect.x + dx) * scale, (zone.rect.y + dy) * scale, (zone.rect.right() + dx) * scale,
                   (zone.rect.bottom() + dy) * scale);
}

} // namespace

std::expected<FUiLayoutSpec, std::string> ParseUiLayoutSpec(const FAssetDigest& digest) {
    try {
        return Parse(digest.json());
    } catch (const nlohmann::json::exception& error) {
        return std::unexpected(std::string("ui layout is malformed: ") + error.what());
    }
}

FUiLayout ComputeUiLayout(const FUiLayoutSpec& spec, float screenWidth, float screenHeight) {
    screenWidth = std::max(1.f, screenWidth);
    screenHeight = std::max(1.f, screenHeight);
    FUiLayout layout;
    layout.scale = std::min(screenHeight / spec.designHeight, screenWidth / spec.minWidth);
    const float scale = layout.scale;
    const float extraX = screenWidth / scale - spec.designWidth;
    const float extraY = screenHeight / scale - spec.designHeight;

    const auto& bands = spec.bands;
    layout.screen = {0.f, 0.f, screenWidth, screenHeight};
    const float top = std::round(bands.top * scale);
    const float bottom = std::round(screenHeight - bands.bottom * scale);
    const float left = std::round(bands.left * scale);
    const float right = std::round(screenWidth - bands.right * scale);
    layout.topBar = {0.f, 0.f, screenWidth, top};
    layout.dock = {0.f, bottom, screenWidth, screenHeight - bottom};
    layout.leftRail = {0.f, top, left, bottom - top};
    layout.rightRail = {right, top, screenWidth - right, bottom - top};
    layout.viewport = {left, top, right - left, bottom - top};
    layout.intentCompactThreshold = spec.intentCompactThreshold;

    constexpr auto specZones = ZoneTable<FZoneSpec>();
    constexpr auto rectZones = ZoneTable<FRect>();
    for (std::size_t index = 0; index < specZones.size(); ++index) {
        layout.zones.*rectZones[index].second = Place(spec.zones.*specZones[index].second, extraX, extraY, scale);
    }

    const auto& zones = layout.zones;
    float arenaTop = top;
    float arenaBottom = bottom;
    for (const FRect* zone : {&zones.title, &zones.turnOrder, &zones.status}) arenaTop = std::max(arenaTop, zone->bottom());
    for (const FRect* zone : {&zones.party, &zones.unit, &zones.skills, &zones.endTurn, &zones.journal, &zones.hints}) {
        arenaBottom = std::min(arenaBottom, zone->y);
    }
    layout.arena = {left, arenaTop, right - left, std::max(1.f, arenaBottom - arenaTop)};

    // WP-24b: two onboarding anchors inside the arena. `strip` is centered under the top band (battle hint);
    // `side` sits in the right margin beside the centered run-phase modal column (reward/camp hint). The
    // authored numbers scale with the layout; the modal band stays in screen pixels because those views do.
    const FOnboardingSpec& card = spec.onboarding;
    const float cardTop = std::round(layout.arena.y + card.topMargin * scale);
    const float cardHeight = std::max(1.f, layout.arena.bottom() - cardTop);
    const float margin = card.sideMargin * scale;
    const float stripWidth = std::min(std::max(1.f, card.stripWidth * scale),
                                      std::max(1.f, layout.arena.w - 2.f * margin));
    layout.onboarding.strip = {
        std::round(layout.arena.x + (layout.arena.w - stripWidth) * .5f),
        cardTop,
        std::round(stripWidth),
        std::round(cardHeight),
    };
    const float clearance = std::max(1.f, card.modalClearanceWidth);
    layout.onboarding.modalColumn = {
        std::round((screenWidth - clearance) * .5f),
        layout.arena.y,
        clearance,
        layout.arena.h,
    };
    const float sideX = layout.onboarding.modalColumn.right() + margin;
    const float sideRoom = std::max(1.f, screenWidth - margin - sideX);
    const float sideWidth = std::min(std::max(1.f, card.sideWidth * scale), sideRoom);
    layout.onboarding.side = {std::round(sideX), cardTop, std::round(sideWidth), std::round(cardHeight)};
    layout.onboarding.padding = card.padding * scale;
    return layout;
}

} // namespace game::ui
