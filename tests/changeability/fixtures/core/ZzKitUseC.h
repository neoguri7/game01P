#pragma once
// V8 [R8] prefix-free enum class with ==-style dispatch spread over 3 files.
enum class ZzKit { Blade, Bow, Staff };

inline void zzKitC(ZzKit k) {
  if (k == ZzKit::Blade) { return; }
  if (k == ZzKit::Bow) { return; }
  case ZzKit::Staff: // dispatch in the C file
    return;
}
