#pragma once
#include <string>
namespace game::ecs {
// V10 [R10]: abstraction with 1 impl and 1 call site — premature abstraction
struct IZzSolo { virtual ~IZzSolo() = default; virtual void run() = 0; };
struct ZzSoloImpl final : IZzSolo { void run() override {} };
inline void zzSoloOnce() { ZzSoloImpl s; s.run(); }
}
