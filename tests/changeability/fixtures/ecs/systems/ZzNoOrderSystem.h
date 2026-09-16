#pragma once
#include "ecs/systems/ISystem.h"
#include <unordered_map>
#include <random>
namespace game::ecs {
// V16 [R16]: unordered iteration + wall-clock rand + variable dt => non-deterministic sim
struct ZzNoOrderSystem : public ISystem {
    std::string name() const override { return "ZzNoOrderSystem"; }
    void update(entt::registry& reg, float dt) override {
        static std::unordered_map<int, float> table;   // V16 + V6: state leak
        std::mt19937 rng{std::random_device{}()};      // V16: unseeded RNG per frame
        for (auto& [k, v] : table) { v += dt * rng(); (void)reg; }
    }
};
}
