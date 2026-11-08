#include "presentation/FTitleMenuView.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>

#include "core/EngineState.h"
#include "core/Logger.h"
#include "gameplay/meta/FMetaProgress.h"
#include "presentation/FCompendiumView.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FSettings.h"
#include "presentation/FSettingsView.h"
#include "presentation/FUiTheme.h"
#include "states/FEmberModalHostState.h"

namespace game {
namespace {

constexpr std::string_view kContinueId = "title.continue";
constexpr std::string_view kNewId = "title.new";
constexpr std::string_view kFreshId = "title.fresh";
constexpr std::string_view kQuitId = "title.quit";

std::string DescribeSave(const FDungeonSave& save) {
    const char* where = save.phase == EDungeonPhase::Battle ? "battle"
        : save.phase == EDungeonPhase::Reward ? "room cleared"
        : save.phase == EDungeonPhase::Event ? "shrine" : "camp";
    std::string text = "Dungeon " + std::to_string(save.depth) + ", room " + std::to_string(save.room + 1) +
        " (" + where + ")";
    if (save.savedAt.size() >= 10) text += "  |  saved " + save.savedAt.substr(0, 10);
    return text;
}

/// Starting over while a save exists asks first: that save is the only copy of the run.
void NewExpeditionConfirm(entt::registry& registry) {
    auto& state = registry.ctx().get<FDungeonUiState>();
    if (!state.confirmNewExpedition) return;
    const auto& save = registry.ctx().get<FDungeonSave>();
    auto& request = registry.ctx().get<FMenuRequest>();
    ImGui::OpenPopup("Start a new expedition?");
    if (ImGui::BeginPopupModal("Start a new expedition?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        ui::HeadingText("NEW EXPEDITION?", 22.f);
        ImGui::Text("Your saved expedition will be overwritten:");
        ImGui::TextDisabled("%s", DescribeSave(save).c_str());
        if (ui::Button("Keep my save", {200, 36})) {
            state.confirmNewExpedition = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::Button("Start new", {160, 36})) {
            request.value = EMenuRequest::Start;
            request.fresh = state.newExpeditionFresh;
            state.confirmNewExpedition = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// A title-menu button: the HUD button picture under an ImGui button with a clear face, so the label still
// reaches ImGui's text log.
bool MenuButton(entt::registry& registry, const char* label, ImVec2 size, float textSize) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max{min.x + size.x, min.y + size.y};
    const bool hot = ImGui::IsMouseHoveringRect(min, max);
    ui::DrawNineSlice(ImGui::GetWindowDrawList(), DungeonSkinPiece(registry, EDungeonSprite::HudButton), min, max, 16.f,
                      hot ? IM_COL32(255, 236, 190, 255) : IM_COL32_WHITE);
    constexpr ImVec4 clear{0.f, 0.f, 0.f, 0.f};
    ImGui::PushStyleColor(ImGuiCol_Button, clear);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, clear);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, clear);
    ImGui::PushStyleColor(ImGuiCol_Text, hot ? ui::palette::kGoldBrightV : ui::palette::kGoldV);
    ImFont* heading = ui::HeadingFont();
    if (heading) ImGui::PushFont(heading, textSize);
    const bool pressed = ImGui::Button(label, size);
    if (heading) ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return pressed;
}

/// A request row: its request is the menu request; a transition row leaves its transition in the pending slot.
void ApplyMenuItem(entt::registry& registry, const FTitleMenuItem& item, bool saveExists) {
    auto& request = registry.ctx().get<FMenuRequest>();
    auto& state = registry.ctx().get<FDungeonUiState>();
    if (item.request.value != EMenuRequest::None) {
        if (item.request.value == EMenuRequest::Start && saveExists) {
            state.confirmNewExpedition = true;
            state.newExpeditionFresh = item.request.fresh;
            return;
        }
        request.value = item.request.value;
        request.fresh = item.request.fresh;
        return;
    }
    if (item.transition.kind != EStateChangeKind::None) {
        registry.ctx().insert_or_assign<FTitleMenuPendingTransition>(FTitleMenuPendingTransition{item.transition});
    }
}

} // namespace

void RegisterTitleMenuItems(entt::registry& registry) {
    if (registry.ctx().contains<FTitleMenuRegistry>()) return;
    // The title screen exists only with a dungeon loaded. InitializeDungeonUi is also called by UI-only
    // tests (and a bare registry), so the rows are registered only when their save/checkpoint exist.
    const auto* save = registry.ctx().find<FDungeonSave>();
    const auto* checkpoint = registry.ctx().find<FDungeonCheckpoint>();
    if (save == nullptr || checkpoint == nullptr) {
        LOG_WARN("Title menu rows were not registered: no dungeon save or checkpoint in the context.");
        return;
    }
    registry.ctx().emplace<FTitleMenuRegistry>();
    auto& items = registry.ctx().get<FTitleMenuRegistry>().items;

    // Order is the on-screen order. Continue is only visible while a save exists.
    items.push_back({std::string(kContinueId), "CONTINUE", {EMenuRequest::Continue, false}, {}, save->exists});
    items.push_back({std::string(kNewId), "BEGIN EXPEDITION", {EMenuRequest::Start, false}, {}, true});
    items.push_back({std::string(kFreshId), "FRESH EXPEDITION", {EMenuRequest::Start, true}, {}, checkpoint->cleared > 0});
    items.push_back({std::string(kQuitId), "QUIT", {EMenuRequest::Quit, false}, {}, true});
    // [WP-24a] settings: the row (kept before QUIT), its view and the one boot-time load.
    items.insert(items.end() - 1,
                 {std::string("title.settings"), "SETTINGS", {},
                  {EStateChangeKind::Push, std::string(kEmberSettingsId)}, true});
    RegisterModalView(registry, kEmberSettingsId, RenderSettingsView);
    LoadSettings(registry);
    // [WP-24b] compendium: append the menu item and RegisterModalView here.
    items.insert(items.end() - 1, {std::string(kCompendiumModalId), "COMPENDIUM", {},
                                    {EStateChangeKind::Push, std::string(kCompendiumModalId)}, true});
    RegisterModalView(registry, kCompendiumModalId, RenderCompendiumView);
}

void DrawTitleMenu(entt::registry& registry) {
    // The title screen is a dungeon screen; without a loaded dungeon there is no menu to draw.
    if (registry.ctx().find<FDungeonSave>() == nullptr || registry.ctx().find<FDungeonCheckpoint>() == nullptr ||
        registry.ctx().find<FDungeonUiState>() == nullptr || registry.ctx().find<FTitleMenuRegistry>() == nullptr ||
        registry.ctx().find<FEngineWindowState>() == nullptr) {
        return;
    }
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const float w = static_cast<float>(window.width);
    const float h = static_cast<float>(window.height);
    const float u = std::min(h / 800.f, w / 1160.f);

    auto* back = ImGui::GetBackgroundDrawList();
    const auto painting = DungeonSpriteSize(registry, EDungeonSprite::TitleBg);
    if (painting.x > 0.f && painting.y > 0.f) {
        // Cover the window and crop the overflow evenly, whatever its shape.
        const float cover = std::max(w / painting.x, h / painting.y);
        const ImVec2 size{painting.x * cover, painting.y * cover};
        const ImVec2 min{(w - size.x) * .5f, (h - size.y) * .5f};
        DrawDungeonImage(registry, EDungeonSprite::TitleBg, back, min, {min.x + size.x, min.y + size.y});
    }
    // Sink the lower half so the menu reads over any painting.
    back->AddRectFilledMultiColor({0.f, h * .3f}, {w, h}, IM_COL32(3, 4, 8, 0), IM_COL32(3, 4, 8, 0),
                                  IM_COL32(3, 4, 8, 205), IM_COL32(3, 4, 8, 205));

    const auto logo = DungeonSpriteSize(registry, EDungeonSprite::TitleLogo) * u;
    const ImVec2 logoMin{std::floor((w - logo.x) * .5f), std::floor(h * .1f)};
    DrawDungeonImage(registry, EDungeonSprite::TitleLogo, back, logoMin, {logoMin.x + logo.x, logoMin.y + logo.y});

    const float column = 340.f * u;
    const float menuTop = logoMin.y + logo.y + 44.f * u;
    ui::BeginZone("Ember dungeon", {0.f, 0.f}, {w, h});
    const auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
    const auto& save = registry.ctx().get<FDungeonSave>();
    auto& menu = registry.ctx().get<FTitleMenuRegistry>();
    // The save's existence and the checkpoint's depth change while the app runs; refresh the row flags at
    // draw time so a row registered once still follows the save (the row list itself stays static).
    for (auto& item : menu.items) {
        if (item.id == kContinueId) item.visible = save.exists;
        else if (item.id == kFreshId) item.visible = checkpoint.cleared > 0;
    }

    {
        // The font scope must close before the window does; ImGui asserts on an unbalanced font stack in End().
        ui::FBodyFont body(17.f * u);
        const float x = (w - column) * .5f;
        const ImVec2 tall{column, 52.f * u};
        const ImVec2 short_{column, 44.f * u};
        const float gap = 10.f * u;
        const auto centered = [&](float y, const std::string& text, ImVec4 color) {
            ImGui::SetCursorScreenPos({(w - ImGui::CalcTextSize(text.c_str()).x) * .5f, y});
            ImGui::TextColored(color, "%s", text.c_str());
        };
        centered(menuTop - 30.f * u, "Four adventurers. One more descent.", ui::palette::kTextV);

        float y = menuTop;
        for (const auto& item : menu.items) {
            if (!item.visible) continue;
            if (item.id == kContinueId) {
                if (!save.exists) continue;
                ImGui::SetCursorScreenPos({x, y});
                if (MenuButton(registry, "CONTINUE", tall, 20.f * u)) ApplyMenuItem(registry, item, save.exists);
                y += tall.y + 2.f * u;
                centered(y, DescribeSave(save), ui::palette::kTextV);
                y += 24.f * u;
            } else if (item.id == kNewId) {
                const std::string begin = checkpoint.cleared > 0
                    ? "BEGIN AT DUNGEON " + std::to_string(checkpoint.cleared + 1) : "BEGIN EXPEDITION";
                ImGui::SetCursorScreenPos({x, y});
                if (MenuButton(registry, begin.c_str(), save.exists ? short_ : tall, 20.f * u)) {
                    ApplyMenuItem(registry, item, save.exists);
                }
                y += (save.exists ? short_.y : tall.y) + gap;
            } else if (item.id == kFreshId) {
                if (checkpoint.cleared <= 0) continue;
                ImGui::SetCursorScreenPos({x, y});
                if (MenuButton(registry, "FRESH EXPEDITION", short_, 17.f * u)) ApplyMenuItem(registry, item, save.exists);
                y += short_.y + gap;
            } else if (item.id == kQuitId) {
                ImGui::SetCursorScreenPos({x, y});
                if (MenuButton(registry, "QUIT", short_, 17.f * u)) ApplyMenuItem(registry, item, save.exists);
                y += short_.y + gap;
            } else {
                // A later WP's row (settings/compendium): same button, request or transition.
                ImGui::SetCursorScreenPos({x, y});
                if (MenuButton(registry, item.label.c_str(), short_, 17.f * u)) ApplyMenuItem(registry, item, save.exists);
                y += short_.y + gap;
            }
        }

        if (!checkpoint.error.empty()) {
            centered(y, checkpoint.error, {.9f, .31f, .31f, 1});
            y += 22.f * u;
        }
        if (!save.error.empty()) {
            centered(y, save.error, {.9f, .31f, .31f, 1});
            y += 22.f * u;
        }
        // [WP-20] meta save: one read-only information line ("Seals N · Best dungeon M · Clears K";
        // "meta disabled" for a read-only session). Nothing is drawn while meta is not initialized. The
        // records/unlocks panel is WP-21's preparation screen — this is a drawing-path block only.
        if (const std::string metaLine = MetaSummaryLine(registry); !metaLine.empty()) {
            centered(y, metaLine, ui::palette::kTextV);
        }

        ui::FBodyFont hints(14.f * u);
        centered(h - 26.f * u, "Mouse: move / target    WASD: one-cell move    1-4: skill    Space: end turn    Esc: abandon",
                 ui::palette::kMutedV);
    }
    ui::EndZone();
    NewExpeditionConfirm(registry);
}

std::optional<FStateTransition> TakeTitleMenuTransition(entt::registry& registry) {
    auto* pending = registry.ctx().find<FTitleMenuPendingTransition>();
    if (pending == nullptr) return std::nullopt;
    const FStateTransition transition = pending->transition;
    registry.ctx().erase<FTitleMenuPendingTransition>();
    return transition;
}

} // namespace game
