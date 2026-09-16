#pragma once
#include <string>
#include <SDL3/SDL.h>
namespace game::core {
// V15 [R15]: owning raw pointer + manual new/delete, no RAII type
struct ZzTexture {
    SDL_Texture* tex{nullptr};
    std::string* name{nullptr};
    ZzTexture() { name = new std::string("zz"); }
    ~ZzTexture() { delete name; }
};
inline void zzLeakOnce(entt::registry& reg) {
    auto* raw = new ZzTexture();      // V15: raw ownership escaping the scope
    reg.ctx().emplace<ZzTexture*>(raw);
}                                     // nothing deletes it
}
