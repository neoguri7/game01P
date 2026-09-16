#pragma once
namespace game::ecs {
// V11b [R11]: same entity's state spread over mutually exclusive bools
struct FZzStats { bool isIdle{true}; bool isRunning{false}; bool isJumping{false}; bool isFalling{false}; };
}
