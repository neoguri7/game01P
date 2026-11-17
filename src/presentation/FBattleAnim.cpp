#include "presentation/FBattleAnim.h"
#include "presentation/FAudioDirector.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"
#include "ecs/AnimationFunctions.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FGridPosition.h"
#include "gameplay/data/FContentRegistry.h"
#include "presentation/FSettings.h"

namespace game {
namespace {

constexpr float kHitFlashSeconds = .2f;
constexpr float kHitShakePixels = 3.f;
constexpr float kMinTravelSeconds = .08f;
constexpr float kSkyDropCells = 5.3f;  // a screen drop of about 170 pixels
constexpr std::array<const char*, kUnitAnimCount> kClipSuffix{"idle", "walk", "attack", "hit", "death"};

// ---------------------------------------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------------------------------------

std::unexpected<std::string> Fail(const std::string& where, const std::string& problem) {
    return std::unexpected(where + ": " + problem);
}

std::expected<FAnimClipId, std::string> FindClip(const FAnimationLibrary& library, const std::string& id) {
    if (const auto found = library.findClip(id)) return *found;
    return std::unexpected("unknown clip '" + id + "'");
}

std::expected<float, std::string> ReadFloat(
    const nlohmann::json& object, const char* key, float fallback, float lowest, float highest) {
    const auto found = object.find(key);
    if (found == object.end()) return fallback;
    if (!found->is_number()) return Fail(key, "must be a number");
    const float value = found->get<float>();
    if (!(value >= lowest && value <= highest)) return Fail(key, "is out of range");
    return value;
}

std::expected<bool, std::string> ReadBool(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end()) return false;
    if (!found->is_boolean()) return Fail(key, "must be true or false");
    return found->get<bool>();
}

std::expected<FSkillEffect, std::string> ParseEffect(const nlohmann::json& object, const FAnimationLibrary& library) {
    if (!object.is_object()) return std::unexpected("an effect must be an object");
    FSkillEffect effect;
    const auto kind = object.find("kind");
    if (kind == object.end() || !kind->is_string()) return Fail("kind", "must be impact, projectile or zone");
    const auto& name = kind->get_ref<const std::string&>();
    if (name == "impact") effect.kind = EEffectKind::Impact;
    else if (name == "projectile") effect.kind = EEffectKind::Projectile;
    else if (name == "zone") effect.kind = EEffectKind::Zone;
    else return Fail("kind", "must be impact, projectile or zone");

    const auto clip = object.find("clip");
    if (clip == object.end() || !clip->is_string()) return Fail("clip", "must name a clip");
    const auto clipId = FindClip(library, clip->get<std::string>());
    if (!clipId) return std::unexpected(clipId.error());
    effect.clip = *clipId;
    if (const auto landing = object.find("impact"); landing != object.end()) {
        if (effect.kind != EEffectKind::Projectile || !landing->is_string()) return Fail("impact", "only a projectile has one");
        const auto landingId = FindClip(library, landing->get<std::string>());
        if (!landingId) return std::unexpected(landingId.error());
        effect.impactClip = *landingId;
    }

    struct FFloatField { const char* key; float* target; float lowest; float highest; };
    for (const FFloatField& field : {FFloatField{"scale", &effect.scale, .1f, 4.f},
                                     FFloatField{"impact_scale", &effect.impactScale, 0.f, 4.f},
                                     FFloatField{"lift", &effect.lift, 0.f, 200.f},
                                     FFloatField{"speed", &effect.speed, 50.f, 4000.f},
                                     FFloatField{"stagger", &effect.stagger, 0.f, 1.f},
                                     FFloatField{"lunge", &effect.lunge, 0.f, 32.f}}) {
        const auto value = ReadFloat(object, field.key, *field.target, field.lowest, field.highest);
        if (!value) return std::unexpected(value.error());
        *field.target = *value;
    }
    struct FFlagField { const char* key; bool* target; };
    for (const FFlagField& field : {FFlagField{"rotate", &effect.rotate}, FFlagField{"ground", &effect.ground},
                                    FFlagField{"from_sky", &effect.fromSky}}) {
        const auto value = ReadBool(object, field.key);
        if (!value) return std::unexpected(value.error());
        *field.target = *value;
    }
    return effect;
}

// ---------------------------------------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------------------------------------

/// Screen offset of a displacement given in cells, at scale 1.
glm::vec2 IsoDelta(glm::vec2 cells) {
    return {(cells.x - cells.y) * kIsoHalfWidth, (cells.x + cells.y) * kIsoHalfHeight};
}

glm::vec2 CellOf(const FGridPosition& cell) { return {static_cast<float>(cell.x), static_cast<float>(cell.y)}; }

/// Seconds from the start of `clip` until `frame` begins.
float SecondsBeforeFrame(const FAnimClip& clip, int frame) {
    double seconds = 0.0;
    for (std::size_t index = 0; index < clip.frames.size() && static_cast<int>(index) < frame; ++index) {
        seconds += clip.frames[index].duration;
    }
    return static_cast<float>(seconds);
}

// ---------------------------------------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------------------------------------

struct FContext {
    const FBattleAnimData& data;
    const FAnimationLibrary& library;
};

void Play(const FContext& context, FUnitAnim& anim, EUnitAnim state) {
    const FAnimClipId id = context.data.sets[anim.set].clips[static_cast<std::size_t>(state)];
    const FAnimClip* clip = context.library.get(id);
    if (clip == nullptr) return;
    ecs::StartClip(anim.player, id, *clip);
    anim.state = state;
    if (state == EUnitAnim::Attack) anim.attackTime = 0.f;
}

void Later(entt::registry& registry, float delay, std::function<void(entt::registry&)> run) {
    registry.ctx().get<FBattleTimeline>().steps.push_back({delay, std::move(run)});
}

void Spawn(entt::registry& registry, FAnimClipId clipId, glm::vec2 from, glm::vec2 to, float duration,
           const FSkillEffect& style, bool rotate) {
    const auto* library = FAnimationLibrary::Find(registry);
    const FAnimClip* clip = library != nullptr ? library->get(clipId) : nullptr;
    auto* effects = registry.ctx().find<FBattleEffects>();
    if (clip == nullptr || effects == nullptr) return;
    FActiveEffect effect;
    effect.clip = clipId;
    ecs::StartClip(effect.player, clipId, *clip);
    effect.from = from;
    effect.to = to;
    effect.duration = duration;
    effect.scale = style.scale;
    effect.lift = style.lift;
    effect.rotate = rotate;
    effect.ground = style.ground;
    effects->active.push_back(std::move(effect));
}

/// A blow lands on `target`: the health bar catches up and the unit flinches or falls.
void React(entt::registry& registry, float delay, const FBattleEvent::FTarget& target) {
    Later(registry, delay, [target](entt::registry& r) {
        // WP-51: the blow is heard where it lands, even for a unit without an animation set.
        if (target.died) PlayAudioEvent(r, EAudioEvent::UnitDeath);
        else if (target.hostile && target.healthBefore > target.healthAfter) PlayAudioEvent(r, EAudioEvent::UnitHit);
        if (target.crit) PlayAudioEvent(r, EAudioEvent::Crit);
        // WP-12 damage pop: the target's own released delta, at the moment the blow lands — every target that
        // lost HP (hostile or not; an area shot can hurt anyone). Skipped with the animations (the
        // presentation preference never changes a rule).
        if (target.healthBefore > target.healthAfter && !CurrentSettings(r).skipAnimations) {
            if (auto* effects = r.ctx().find<FBattleEffects>(); effects != nullptr) {
                if (const auto* cell = r.try_get<FGridPosition>(target.unit); cell != nullptr) {
                    FDamagePop pop{glm::vec2{static_cast<float>(cell->x), static_cast<float>(cell->y)},
                                   target.healthBefore - target.healthAfter, 0.f};
                    pop.crit = target.crit;
                    effects->pops.push_back(pop);
                }
            }
        }
        auto* anim = r.try_get<FUnitAnim>(target.unit);
        const auto* data = r.ctx().find<FBattleAnimData>();
        const auto* library = FAnimationLibrary::Find(r);
        if (anim == nullptr || data == nullptr || library == nullptr) return;
        const FContext context{*data, *library};
        anim->shownHealth = -1;
        if (target.died) {
            anim->pendingDeaths = std::max(0, anim->pendingDeaths - 1);
            anim->route.clear();
            Play(context, *anim, EUnitAnim::Death);
        } else if (target.hostile && anim->state != EUnitAnim::Death) {
            anim->hitTime = 0.f;
            if (anim->state == EUnitAnim::Idle || anim->state == EUnitAnim::Hit) Play(context, *anim, EUnitAnim::Hit);
        }
    });
}

void QueueMove(entt::registry& registry, const FBattleEvent& event, const FContext& context) {
    auto* anim = registry.try_get<FUnitAnim>(event.actor);
    if (anim == nullptr) return;
    // WP-51: one footstep per cell walked (a unit without animation walks silently).
    for (std::size_t step = 1; step < event.path.size(); ++step) {
        Later(registry, static_cast<float>(step - 1) * context.data.moveSeconds,
              [](entt::registry& r) { PlayAudioEvent(r, EAudioEvent::Footstep); });
    }
    for (std::size_t step = 1; step < event.path.size(); ++step) anim->route.push_back(CellOf(event.path[step]));
}

void QueueSkill(entt::registry& registry, const FBattleEvent& event, const FContext& context) {
    const auto styleFound = context.data.skills.find(event.skillId);
    const FSkillEffect& style = styleFound != context.data.skills.end() ? styleFound->second : context.data.fallback;
    const auto* actorCell = registry.try_get<FGridPosition>(event.actor);
    const glm::vec2 origin = actorCell != nullptr ? CellOf(*actorCell) : CellOf(event.aim);

    // Health bars hold their old value and knock-outs wait until the blow that causes them has landed.
    for (const auto& target : event.targets) {
        if (auto* anim = registry.try_get<FUnitAnim>(target.unit)) {
            anim->shownHealth = target.healthBefore;
            if (target.died) ++anim->pendingDeaths;
        }
    }

    // The caster plays its one attack motion whatever the skill; the blow lands on the clip's impact frame.
    float impactAt = 0.f;
    if (auto* anim = registry.try_get<FUnitAnim>(event.actor)) {
        const glm::vec2 toward = IsoDelta(CellOf(event.aim) - origin);
        anim->aim = glm::length(toward) > 1e-3f ? glm::normalize(toward) : glm::vec2{0.f, 1.f};
        anim->lunge = style.lunge;
        const FUnitAnimSet& set = context.data.sets[anim->set];
        Play(context, *anim, EUnitAnim::Attack);
        if (const FAnimClip* attack = context.library.get(set.clips[static_cast<std::size_t>(EUnitAnim::Attack)])) {
            impactAt = SecondsBeforeFrame(*attack, set.impactFrame);
        }
    }

    // WP-51: the cast is heard when the caster's blow leaves (release), the landing where it arrives (land).
    const FSkillAudio sound = SkillAudio(registry, event.skillId);
    const FAudioCueId land = sound.land;
    if (sound.release != kNoAudioCue) {
        const FAudioCueId release = sound.release;
        Later(registry, impactAt, [release](entt::registry& r) { PlayAudioCue(r, release); });
    }

    // Where the skill lands: every unit it reached, or the targeted cell when nobody stood there.
    std::vector<glm::vec2> spots;
    for (const auto& target : event.targets) {
        if (const auto* cell = registry.try_get<FGridPosition>(target.unit)) spots.push_back(CellOf(*cell));
        else spots.push_back(CellOf(event.aim));
    }
    if (spots.empty()) spots.push_back(CellOf(event.aim));

    switch (style.kind) {
    case EEffectKind::Impact:
        for (std::size_t index = 0; index < spots.size(); ++index) {
            const float at = impactAt + static_cast<float>(index) * style.stagger;
            const glm::vec2 spot = spots[index];
            Later(registry, at, [style, spot, land](entt::registry& r) {
                Spawn(r, style.clip, spot, spot, 0.f, style, false);
                PlayAudioCue(r, land);
            });
        }
        for (std::size_t index = 0; index < event.targets.size(); ++index) {
            React(registry, impactAt + static_cast<float>(index) * style.stagger, event.targets[index]);
        }
        break;
    case EEffectKind::Projectile:
        for (std::size_t index = 0; index < spots.size(); ++index) {
            const glm::vec2 spot = spots[index];
            const glm::vec2 from = style.fromSky ? spot - glm::vec2{kSkyDropCells} : origin;
            const float travel = std::max(kMinTravelSeconds, glm::length(IsoDelta(spot - from)) / style.speed);
            const float launch = impactAt + static_cast<float>(index) * style.stagger;
            Later(registry, launch, [style, from, spot, travel](entt::registry& r) {
                Spawn(r, style.clip, from, spot, travel, style, style.rotate);
            });
            if (style.impactClip) {
                FSkillEffect landing = style;
                if (style.impactScale > 0.f) landing.scale = style.impactScale;
                Later(registry, launch + travel, [landing, spot](entt::registry& r) {
                    Spawn(r, landing.impactClip, spot, spot, 0.f, landing, false);
                });
            }
            Later(registry, launch + travel, [land](entt::registry& r) { PlayAudioCue(r, land); });
            if (index < event.targets.size()) React(registry, launch + travel, event.targets[index]);
        }
        break;
    case EEffectKind::Zone: {
        const std::size_t count = event.cells.empty() ? 1 : event.cells.size();
        for (std::size_t index = 0; index < count; ++index) {
            const glm::vec2 spot = event.cells.empty() ? CellOf(event.aim) : CellOf(event.cells[index]);
            Later(registry, impactAt + static_cast<float>(index) * style.stagger, [style, spot](entt::registry& r) {
                Spawn(r, style.clip, spot, spot, 0.f, style, false);
            });
        }
        Later(registry, impactAt, [land](entt::registry& r) { PlayAudioCue(r, land); });
        for (const auto& target : event.targets) React(registry, impactAt, target);
        break;
    }
    }
}

void AdvanceUnit(const FContext& context, entt::registry& registry, entt::entity entity, FUnitAnim& anim, float dt) {
    // Knock-outs nobody announced (a spike trap, a burn) still end in the death pose once no blow is pending.
    if (anim.state != EUnitAnim::Death && anim.pendingDeaths == 0 && registry.all_of<FDead>(entity)) {
        anim.route.clear();
        Play(context, anim, EUnitAnim::Death);
    }

    if (anim.state != EUnitAnim::Death) {
        float travel = dt / context.data.moveSeconds;
        while (travel > 1e-6f && !anim.route.empty()) {
            const glm::vec2 delta = anim.route.front() - anim.visual;
            const float distance = glm::length(delta);
            if (distance <= travel) {
                anim.visual = anim.route.front();
                travel -= distance;
                anim.route.erase(anim.route.begin());
            } else {
                anim.visual += delta / distance * travel;
                travel = 0.f;
            }
        }
        if (anim.route.empty()) {
            // Nothing queued: the picture belongs on the unit's real cell.
            if (const auto* cell = registry.try_get<FGridPosition>(entity)) anim.visual = CellOf(*cell);
        }
        if (!anim.route.empty() && anim.state == EUnitAnim::Idle) Play(context, anim, EUnitAnim::Move);
        else if (anim.route.empty() && anim.state == EUnitAnim::Move) Play(context, anim, EUnitAnim::Idle);
    }

    const FAnimClip* clip = context.library.get(anim.player.clip);
    if (clip == nullptr) return;
    ecs::AdvanceAnimator(anim.player, *clip, static_cast<double>(dt));
    anim.hitTime = std::min(anim.hitTime + dt, 99.f);

    if (anim.state == EUnitAnim::Attack) {
        anim.attackTime += dt;
        const FUnitAnimSet& set = context.data.sets[anim.set];
        const float duration = static_cast<float>(clip->totalDuration);
        const float impact = std::clamp(SecondsBeforeFrame(*clip, set.impactFrame), 1e-3f, std::max(duration - 1e-3f, 1e-3f));
        const float step = anim.attackTime < impact ? anim.attackTime / impact
                                                    : 1.f - (anim.attackTime - impact) / std::max(duration - impact, 1e-3f);
        anim.push = anim.aim * (anim.lunge * std::clamp(step, 0.f, 1.f));
        if (anim.player.finished) {
            anim.push = {};
            Play(context, anim, EUnitAnim::Idle);
        }
    } else if (anim.state == EUnitAnim::Hit && anim.player.finished) {
        Play(context, anim, EUnitAnim::Idle);
    }
}

} // namespace

std::expected<FBattleAnimData, std::string> ParseBattleAnim(
    const nlohmann::json& document, const FAnimationLibrary& library, const gameplay::FContentRegistry& content) {
    if (!document.is_object()) return std::unexpected("the document must be an object");
    const auto version = document.find("schema_version");
    if (version == document.end() || !version->is_number_integer() || version->get<int>() != 1) {
        return Fail("schema_version", "must be 1");
    }
    FBattleAnimData data;
    const auto moveSeconds = ReadFloat(document, "move_seconds", data.moveSeconds, .05f, 2.f);
    if (!moveSeconds) return std::unexpected(moveSeconds.error());
    data.moveSeconds = *moveSeconds;

    const auto units = document.find("units");
    if (units == document.end() || !units->is_object() || units->empty()) return Fail("units", "must be an object");
    for (const auto& [unitId, entry] : units->items()) {
        if (content.units.find(unitId) == nullptr) return Fail("units", "'" + unitId + "' is not a unit");
        const auto prefix = entry.is_object() ? entry.find("clips") : entry.end();
        if (!entry.is_object() || prefix == entry.end() || !prefix->is_string()) {
            return Fail("units." + unitId, "needs a clips prefix");
        }
        FUnitAnimSet set;
        for (std::size_t action = 0; action < kUnitAnimCount; ++action) {
            const auto clip = FindClip(library, prefix->get<std::string>() + "." + kClipSuffix[action]);
            if (!clip) return Fail("units." + unitId, clip.error());
            set.clips[action] = *clip;
        }
        const auto impact = entry.find("impact_frame");
        if (impact == entry.end() || !impact->is_number_integer()) return Fail("units." + unitId, "needs an impact_frame");
        const FAnimClip* attack = library.get(set.clips[static_cast<std::size_t>(EUnitAnim::Attack)]);
        set.impactFrame = impact->get<int>();
        if (attack == nullptr || set.impactFrame < 0 || set.impactFrame >= static_cast<int>(attack->frames.size())) {
            return Fail("units." + unitId, "impact_frame is outside the attack clip");
        }
        // WP-24a (W3 correction #6): a one-shot action authored loop:true is a data defect, not a boot
        // failure — the gate deliberately excludes loop clips, so it cannot deadlock, but a looping death
        // would play under the victory modal forever. Warn so the data author sees it.
        for (const std::size_t action : {static_cast<std::size_t>(EUnitAnim::Attack),
                                         static_cast<std::size_t>(EUnitAnim::Death)}) {
            const FAnimClip* oneShot = library.get(set.clips[action]);
            if (oneShot != nullptr && oneShot->loop) {
                LOG_WARN("Unit '{}': loop clip on a one-shot action — gate will wait visually.", unitId);
            }
        }
        data.setByUnit.emplace(unitId, data.sets.size());
        data.sets.push_back(set);
    }

    const auto skills = document.find("skills");
    if (skills == document.end() || !skills->is_object()) return Fail("skills", "must be an object");
    for (const auto& [skillId, entry] : skills->items()) {
        if (content.skills.find(skillId) == nullptr) return Fail("skills", "'" + skillId + "' is not a skill");
        auto effect = ParseEffect(entry, library);
        if (!effect) return Fail("skills." + skillId, effect.error());
        data.skills.emplace(skillId, *effect);
    }
    const auto fallback = document.find("default_effect");
    if (fallback == document.end()) return Fail("default_effect", "is required");
    auto effect = ParseEffect(*fallback, library);
    if (!effect) return Fail("default_effect", effect.error());
    data.fallback = *effect;
    return data;
}

bool InitializeBattleAnim(entt::registry& registry) {
    const auto* library = FAnimationLibrary::Find(registry);
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    auto* assets = registry.ctx().find<FAssetManager>();
    if (library == nullptr || content == nullptr || assets == nullptr) {
        LOG_ERROR("Battle animation needs the animation library, gameplay content and assets.");
        return false;
    }
    const auto asset = assets->digest("data/battle_anim");
    if (!asset) {
        LOG_ERROR("Battle animation data (data/battle_anim) is missing.");
        return false;
    }
    auto parsed = ParseBattleAnim(asset->json(), *library, *content);
    if (!parsed) {
        LOG_ERROR("Battle animation data is invalid: {}", parsed.error());
        return false;
    }
    registry.ctx().erase<FBattleAnimData>();
    registry.ctx().emplace<FBattleAnimData>(std::move(*parsed));
    return true;
}

void BeginBattleAnim(entt::registry& registry) {
    EndBattleAnim(registry);
    registry.ctx().emplace<FBattleEvents>();
    registry.ctx().emplace<FBattleEffects>();
    registry.ctx().emplace<FBattleTimeline>();
}

void EndBattleAnim(entt::registry& registry) {
    registry.ctx().erase<FBattleEvents>();
    registry.ctx().erase<FBattleEffects>();
    registry.ctx().erase<FBattleTimeline>();
}

void AttachUnitAnimations(entt::registry& registry, std::span<const FUnitPlacement> units) {
    const auto* data = registry.ctx().find<FBattleAnimData>();
    const auto* library = FAnimationLibrary::Find(registry);
    if (data == nullptr || library == nullptr) return;
    for (const auto& unit : units) {
        const auto found = data->setByUnit.find(unit.unitTemplateId);
        if (found == data->setByUnit.end()) continue;
        FUnitAnim anim;
        anim.set = found->second;
        anim.visual = CellOf(unit.cell);
        const FAnimClipId idle = data->sets[anim.set].clips[static_cast<std::size_t>(EUnitAnim::Idle)];
        if (const FAnimClip* clip = library->get(idle)) ecs::StartClip(anim.player, idle, *clip);
        registry.emplace_or_replace<FUnitAnim>(unit.entity, std::move(anim));
    }
}

void SettleUnitAnimations(entt::registry& registry) {
    const auto* data = registry.ctx().find<FBattleAnimData>();
    const auto* library = FAnimationLibrary::Find(registry);
    if (data == nullptr || library == nullptr) return;
    const FContext context{*data, *library};
    for (const auto entity : registry.view<FUnitAnim, FDead>()) {
        auto& anim = registry.get<FUnitAnim>(entity);
        Play(context, anim, EUnitAnim::Death);
        if (const FAnimClip* clip = library->get(anim.player.clip); clip != nullptr && !clip->frames.empty()) {
            anim.player.frame = static_cast<std::uint32_t>(clip->frames.size() - 1);
            anim.player.playing = false;
            anim.player.finished = true;
        }
    }
}

void UpdateBattleAnimations(entt::registry& registry, float dt) {
    auto* events = registry.ctx().find<FBattleEvents>();
    auto* timeline = registry.ctx().find<FBattleTimeline>();
    auto* effects = registry.ctx().find<FBattleEffects>();
    const auto* data = registry.ctx().find<FBattleAnimData>();
    const auto* library = FAnimationLibrary::Find(registry);
    if (events == nullptr) return;
    if (timeline == nullptr || effects == nullptr || data == nullptr || library == nullptr) {
        events->pending.clear();  // nothing here can show them; do not let them pile up
        return;
    }
    const FContext context{*data, *library};

    auto pending = std::move(events->pending);
    events->pending.clear();
    for (const auto& event : pending) {
        if (event.kind == FBattleEvent::EKind::Move) QueueMove(registry, event, context);
        else QueueSkill(registry, event, context);
    }

    auto due = std::move(timeline->steps);
    timeline->steps.clear();
    for (auto& step : due) {
        step.delay -= dt;
        if (step.delay <= 0.f) step.run(registry);
        else timeline->steps.push_back(std::move(step));
    }

    for (const auto entity : registry.view<FUnitAnim>()) {
        AdvanceUnit(context, registry, entity, registry.get<FUnitAnim>(entity), dt);
    }

    for (auto& effect : effects->active) {
        effect.age += dt;
        if (const FAnimClip* clip = library->get(effect.clip)) {
            ecs::AdvanceAnimator(effect.player, *clip, static_cast<double>(dt));
        }
    }
    std::erase_if(effects->active, [](const FActiveEffect& effect) {
        return effect.duration > 0.f ? effect.age >= effect.duration : effect.player.finished;
    });
    // WP-12 damage pops: short-lived floating numbers over the released blow.
    for (FDamagePop& pop : effects->pops) {
        pop.age += dt;
    }
    std::erase_if(effects->pops, [](const FDamagePop& pop) { return pop.age >= pop.lifetime; });
}

bool DrawAnimFrame(entt::registry& registry, FAnimClipId clipId, std::uint32_t frame, glm::vec2 anchor, float scale,
                   Uint8 alpha, SDL_Color tint, double angleDegrees) {
    const auto* library = FAnimationLibrary::Find(registry);
    const auto* resources = registry.ctx().find<FResourceManager>();
    const auto* renderer = registry.ctx().find<SDL_Renderer*>();
    const FAnimClip* clip = library != nullptr ? library->get(clipId) : nullptr;
    if (clip == nullptr || clip->frames.empty() || resources == nullptr || renderer == nullptr || *renderer == nullptr) {
        return false;
    }
    const auto* texture = resources->find(clip->texture);
    if (texture == nullptr || texture->texture == nullptr) return false;

    const FSourceRect& source = clip->frames[std::min<std::size_t>(frame, clip->frames.size() - 1)].source;
    const SDL_FRect from{source.x, source.y, source.w, source.h};
    const SDL_FRect to{anchor.x - clip->anchor.x * scale, anchor.y - clip->anchor.y * scale, source.w * scale, source.h * scale};
    SDL_SetTextureAlphaMod(texture->texture, alpha);
    SDL_SetTextureColorMod(texture->texture, tint.r, tint.g, tint.b);
    if (angleDegrees == 0.0) {
        SDL_RenderTexture(*renderer, texture->texture, &from, &to);
    } else {
        const SDL_FPoint pivot{clip->anchor.x * scale, clip->anchor.y * scale};
        SDL_RenderTextureRotated(*renderer, texture->texture, &from, &to, angleDegrees, &pivot, SDL_FLIP_NONE);
    }
    SDL_SetTextureAlphaMod(texture->texture, 255);
    SDL_SetTextureColorMod(texture->texture, 255, 255, 255);
    return true;
}

void DrawBattleEffects(entt::registry& registry, const FIsoView& view, bool ground) {
    const auto* effects = registry.ctx().find<FBattleEffects>();
    if (effects == nullptr) return;
    for (const auto& effect : effects->active) {
        if (effect.ground != ground) continue;
        const float along = effect.duration > 0.f ? std::clamp(effect.age / effect.duration, 0.f, 1.f) : 1.f;
        const glm::vec2 cell = effect.from + (effect.to - effect.from) * along;
        glm::vec2 anchor = view.corner(cell.x + .5f, cell.y + .5f);
        anchor.y -= effect.lift * view.scale;
        double angle = 0.0;
        if (effect.rotate) {
            const glm::vec2 heading = IsoDelta(effect.to - effect.from);
            angle = std::atan2(heading.y, heading.x) * 180.0 / std::numbers::pi;
        }
        DrawAnimFrame(registry, effect.clip, effect.player.frame, anchor, effect.scale * view.scale, 255,
                      {255, 255, 255, 255}, angle);
    }
}

glm::vec2 UnitAnimOffset(const FUnitAnim& anim) {
    glm::vec2 offset = anim.push;
    if (anim.hitTime < kHitFlashSeconds) {
        offset.x += std::sin(anim.hitTime * 70.f) * kHitShakePixels * (1.f - anim.hitTime / kHitFlashSeconds);
    }
    return offset;
}

SDL_Color UnitAnimTint(const FUnitAnim& anim) {
    if (anim.hitTime >= kHitFlashSeconds) return {255, 255, 255, 255};
    const float strength = 1.f - anim.hitTime / kHitFlashSeconds;
    const auto channel = static_cast<Uint8>(255.f - 145.f * strength);
    return {255, channel, channel, 255};
}

} // namespace game
