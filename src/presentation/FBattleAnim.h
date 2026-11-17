#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>

#include "core/AnimationLibrary.h"
#include "core/AssetHandles.h"
#include "ecs/components/FAnimator.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/grid/FBattlefieldData.h"
#include "presentation/FIsoView.h"

namespace game {

namespace gameplay { struct FContentRegistry; }

/// What a unit can be seen doing. Every skill reuses Attack; the skill itself is a separate effect image.
enum class EUnitAnim : std::size_t { Idle, Move, Attack, Hit, Death, Count };
inline constexpr std::size_t kUnitAnimCount = static_cast<std::size_t>(EUnitAnim::Count);

/// The clips of one unit kind plus the frame of its Attack clip where the blow lands.
struct FUnitAnimSet {
    std::array<FAnimClipId, kUnitAnimCount> clips{};
    int impactFrame{0};
};

enum class EEffectKind {
    Impact,      // plays on every unit the skill reaches
    Projectile,  // flies from the caster to each target, then plays `impactClip` there
    Zone,        // plays on every cell the skill covers
};

/// How one skill shows itself on the field, apart from the caster's shared attack motion.
struct FSkillEffect {
    EEffectKind kind{EEffectKind::Impact};
    FAnimClipId clip{};
    FAnimClipId impactClip{};  // Projectile: what plays where it lands
    float scale{1.f};
    float impactScale{0.f};    // Projectile: scale of the landing picture; 0 = same as `scale`
    float lift{0.f};           // pixels above the cell centre (chest height for hits, 0 for ground effects)
    float speed{500.f};        // Projectile: pixels per second at scale 1
    float stagger{0.f};        // seconds between successive targets/cells
    float lunge{0.f};          // pixels the caster steps toward the target during the attack
    bool rotate{false};        // Projectile: turn the picture toward its heading
    bool ground{false};        // drawn under the units
    bool fromSky{false};       // Projectile: falls onto the target instead of leaving the caster
};

/// data/battle_anim.json, validated: which clips each unit kind plays and how each skill is shown.
struct FBattleAnimData {
    std::vector<FUnitAnimSet> sets;
    std::unordered_map<std::string, std::size_t> setByUnit;  // unit template id -> index into `sets`
    std::unordered_map<std::string, FSkillEffect> skills;    // skill id -> its effect
    FSkillEffect fallback;                                    // for skills without an entry
    float moveSeconds{.22f};                                  // time to walk one cell
};

/// Playback state of one unit's picture. Gameplay never reads this; it follows the battle events.
struct FUnitAnim {
    std::size_t set{0};
    EUnitAnim state{EUnitAnim::Idle};
    FAnimator player;
    glm::vec2 visual{};            // where the unit is drawn, in cells (fractional while it walks)
    std::vector<glm::vec2> route;  // cells still to walk
    glm::vec2 aim{0.f, 1.f};       // screen direction toward the last target
    float lunge{0.f};              // pixels to step toward the target during the current attack
    glm::vec2 push{};              // that step as it is right now, in unscaled screen pixels
    float attackTime{0.f};
    float hitTime{99.f};           // seconds since a blow landed; drives the flash and the shake
    int shownHealth{-1};           // what the health bar shows while a blow is in flight; -1 = the real value
    int pendingDeaths{0};          // knock-outs waiting for their blow to land
};

/// One effect picture on the field.
struct FActiveEffect {
    FAnimClipId clip{};
    FAnimator player;
    glm::vec2 from{};      // cells; for a projectile, where it starts
    glm::vec2 to{};        // cells
    float duration{0.f};   // > 0: a projectile in flight; 0: plays its clip once and ends
    float age{0.f};
    float scale{1.f};
    float lift{0.f};
    bool rotate{false};
    bool ground{false};
};

/// One floating damage number on the field, spawned where a blow really landed. Scope is the target's own
/// healthBefore→healthAfter delta only: HP changes from hooks (leech heals the caster) are not in that
/// delta and get no pop (WP-12, W6 ★13 — recorded follow-up).
struct FDamagePop {
    glm::vec2 at{};          // cell coordinates (fractional, like FActiveEffect)
    int damage{0};
    float age{0.f};
    float lifetime{.8f};
    bool crit{false};
};

struct FBattleEffects {
    std::vector<FActiveEffect> active;
    std::vector<FDamagePop> pops;
};

/// Things that happen a little later: the blow landing, a projectile arriving, a unit falling.
struct FBattleTimeline {
    struct FStep {
        float delay{0.f};
        std::function<void(entt::registry&)> run;
    };
    std::vector<FStep> steps;
};

/// Parses and checks the battle animation document against the loaded clips and gameplay content.
[[nodiscard]] std::expected<FBattleAnimData, std::string> ParseBattleAnim(
    const nlohmann::json& document, const FAnimationLibrary& library, const gameplay::FContentRegistry& content);

/// Loads data/battle_anim.json into the registry. Unusable data stops the boot instead of drawing half an animation.
[[nodiscard]] bool InitializeBattleAnim(entt::registry& registry);

/// Registers the per-battle state (event queue, effects, timeline). Safe without FBattleAnimData: nothing animates.
void BeginBattleAnim(entt::registry& registry);
void EndBattleAnim(entt::registry& registry);

/// Gives every placed unit with animation data a FUnitAnim standing at its cell.
void AttachUnitAnimations(entt::registry& registry, std::span<const FUnitPlacement> units);
/// Puts units that are already down straight into their final pose (a battle resumed after the fight).
void SettleUnitAnimations(entt::registry& registry);

/// Turns the queued battle events into animations and effects, then advances everything by `dt` seconds.
void UpdateBattleAnimations(entt::registry& registry, float dt);

/// Draws one frame of a clip with its anchor on `anchor` (screen pixels). False when the clip or texture is missing.
bool DrawAnimFrame(entt::registry& registry, FAnimClipId clip, std::uint32_t frame, glm::vec2 anchor, float scale,
                   Uint8 alpha = 255, SDL_Color tint = {255, 255, 255, 255}, double angleDegrees = 0.0);

/// Effects under the units (`ground`) or over them.
void DrawBattleEffects(entt::registry& registry, const FIsoView& view, bool ground);

/// Screen-space nudge of a unit this frame (attack lunge and hit shake), in unscaled pixels.
[[nodiscard]] glm::vec2 UnitAnimOffset(const FUnitAnim& anim);
/// Red-tinted flash right after a blow lands.
[[nodiscard]] SDL_Color UnitAnimTint(const FUnitAnim& anim);

} // namespace game
