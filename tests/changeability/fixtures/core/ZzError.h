#pragma once
#include <iostream>
namespace game::core {
// V19 [R19]: swallowed error, no assert/log/test hook
inline int zzLoad(const char* path) {
    try { return path ? 1 : 0; }
    catch (...) { return -1; }        // V19: silent failure, no log, no test seam
}
}
