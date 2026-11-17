#include "presentation/FBossIntroView.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <memory>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/InputState.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "presentation/FFlowArt.h"
#include "presentation/FSettings.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

constexpr float kMaxSeconds = 10.f;

/// Reads `boss_intro` from the flow document; false (with a warning) when anything is off.
[[nodiscard]] bool ParseTiming(const nlohmann::json& document, FBossIntroTiming& out)
{
    if (!document.is_object()) return false;
    const auto schema = document.find("schema_version");
    if (schema == document.end() || !schema->is_number_integer() || schema->get<int>() != 1) {
        LOG_WARN("data/flow: schema_version must be 1; using the default boss intro timing.");
        return false;
    }
    const auto block = document.find("boss_intro");
    if (block == document.end() || !block->is_object()) {
        LOG_WARN("data/flow: the boss_intro block is missing; using the default boss intro timing.");
        return false;
    }
    FBossIntroTiming parsed;
    const auto read = [&](const char* key, float& target) {
        const auto field = block->find(key);
        if (field == block->end() || !field->is_number()) return false;
        const double value = field->get<double>();
        if (!(value >= 0.0 && value <= static_cast<double>(kMaxSeconds))) return false;
        target = static_cast<float>(value);
        return true;
    };
    if (!read("hold_seconds", parsed.holdSeconds) || !read("fade_seconds", parsed.fadeSeconds)) {
        LOG_WARN("data/flow: boss_intro hold_seconds/fade_seconds must be numbers in [0, {}]; using the defaults.",
                 kMaxSeconds);
        return false;
    }
    out = parsed;
    return true;
}

} // namespace

const FBossIntroTiming& BossIntroTiming(entt::registry& registry)
{
    if (const auto* loaded = registry.ctx().find<FBossIntroTiming>()) return *loaded;
    FBossIntroTiming timing;
    if (const auto* assets = registry.ctx().find<FAssetManager>()) {
        if (const auto digest = assets->digest("data/flow")) {
            FBossIntroTiming parsed;
            if (ParseTiming(digest->json(), parsed)) timing = parsed;
        } else {
            LOG_WARN("data/flow is missing; using the default boss intro timing.");
        }
    }
    return registry.ctx().emplace<FBossIntroTiming>(timing);
}

std::optional<FBossIntroState> MakeBossIntro(const entt::registry& registry)
{
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* sequence = FindRunSequence(registry);
    const auto* bosses = FindBossCatalog(registry);
    if (run == nullptr || sequence == nullptr || bosses == nullptr) return std::nullopt;
    const FRunSequence::FPosition position = sequence->PositionOf(run->depth - 1);
    if (position.theme >= sequence->themes.size()) return std::nullopt;
    const FRunRoomSlot* slot = sequence->BattleSlotAt(position, run->room);
    if (slot == nullptr || slot->boss.empty()) return std::nullopt;
    const FBossDef* def = bosses->Resolve(sequence->themes[position.theme].id, slot->boss);
    if (def == nullptr) return std::nullopt;
    return FBossIntroState{def->name, def->phases.empty() ? std::string{} : def->phases.front().notes, 0.f, false};
}

void EndBossIntro(entt::registry& registry)
{
    registry.ctx().erase<FBossIntroState>();
}

void BeginBossIntro(entt::registry& registry)
{
    EndBossIntro(registry);
    if (CurrentSettings(registry).skipAnimations) return;
    if (auto card = MakeBossIntro(registry)) registry.ctx().emplace<FBossIntroState>(std::move(*card));
}

void AdvanceBossIntro(FBossIntroState& intro, const FBossIntroTiming& timing, float deltaSeconds, bool dismiss,
                      bool skipAnimations)
{
    intro.elapsed += std::max(0.f, deltaSeconds);
    if (dismiss || skipAnimations || intro.elapsed >= timing.holdSeconds + timing.fadeSeconds) intro.done = true;
}

float BossIntroAlpha(const FBossIntroState& intro, const FBossIntroTiming& timing)
{
    if (intro.done) return 0.f;
    if (intro.elapsed <= timing.holdSeconds) return 1.f;
    if (timing.fadeSeconds <= 0.f) return 0.f;
    return std::clamp(1.f - (intro.elapsed - timing.holdSeconds) / timing.fadeSeconds, 0.f, 1.f);
}

void TickBossIntro(entt::registry& registry, float deltaSeconds)
{
    auto* card = registry.ctx().find<FBossIntroState>();
    if (card == nullptr) return;
    bool dismiss = false;
    if (const auto* input = registry.ctx().find<FInputState>()) {
        dismiss = input->mouseLeftPressed || input->isActionPressed(EInputAction::Confirm) ||
                  input->isActionPressed(EInputAction::Cancel);
    }
    AdvanceBossIntro(*card, BossIntroTiming(registry), deltaSeconds, dismiss, CurrentSettings(registry).skipAnimations);
    if (card->done) EndBossIntro(registry);
}

void DrawBossIntro(entt::registry& registry)
{
    const auto* card = registry.ctx().find<FBossIntroState>();
    const auto* window = registry.ctx().find<FEngineWindowState>();
    if (card == nullptr || window == nullptr) return;
    const float alpha = BossIntroAlpha(*card, BossIntroTiming(registry));
    if (alpha <= 0.f) return;
    const auto fade = [alpha](ImU32 colour) {
        const auto a = static_cast<ImU32>(static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
        return (colour & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    };

    const float w = static_cast<float>(window->width);
    const float h = static_cast<float>(window->height);
    const float k = static_cast<float>(std::max(1, static_cast<int>(std::floor(std::min(h / 800.f, w / 1160.f)))));
    const ImVec2 min{std::floor((w - 512.f * k) / 2.f), std::floor(h * 0.18f)};
    const ImVec2 max{min.x + 512.f * k, min.y + 96.f * k};
    const float centreX = min.x + 256.f * k;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ui::FSkinPiece plate = FlowPicture(registry, kBossPlateId);
    if (plate.valid()) {
        draw->AddImage(plate.texture, min, max, ImVec2{0.f, 0.f}, ImVec2{1.f, 1.f}, fade(IM_COL32_WHITE));
    } else {
        ui::DrawPanel(draw, min, max);
    }
    ImFont* heading = ui::HeadingFont();
    if (heading == nullptr) heading = ImGui::GetFont();
    const float nameSize = 28.f * k;
    const ImVec2 nameExtent = heading->CalcTextSizeA(nameSize, FLT_MAX, 0.f, card->name.c_str());
    draw->AddText(heading, nameSize,
                  ImVec2{std::floor(centreX - nameExtent.x * .5f), std::floor(min.y + 34.f * k - nameExtent.y * .5f)},
                  fade(ui::palette::kGoldBright), card->name.c_str());
    if (!card->note.empty()) {
        ui::FBodyFont scope(15.f * k);
        ImFont* body = ImGui::GetFont();
        const float noteSize = 15.f * k;
        const ImVec2 noteExtent = body->CalcTextSizeA(noteSize, FLT_MAX, 0.f, card->note.c_str());
        draw->AddText(body, noteSize,
                      ImVec2{std::floor(centreX - noteExtent.x * .5f), std::floor(min.y + 68.f * k - noteExtent.y * .5f)},
                      fade(ui::palette::kText), card->note.c_str());
    }
}

} // namespace game
