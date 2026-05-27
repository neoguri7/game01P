#pragma once

#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include <string>
#include <string_view>
#include <vector>

namespace game::tactical_d20_config_reader {

std::string FindObjectBody(std::string_view text, std::string_view key);
std::string FindArrayBody(std::string_view text, std::string_view key);
std::vector<std::string> SplitObjectBodies(std::string_view arrayBody);
std::vector<FTacticalD20TileConfig> ParseTiles(std::string_view arrayBody);
std::vector<std::string> ReadStringArray(std::string_view text, std::string_view key, std::vector<std::string> fallback);
int ReadInt(std::string_view text, std::string_view key, int fallback);
bool ReadBool(std::string_view text, std::string_view key, bool fallback);
std::string ReadString(std::string_view text, std::string_view key, std::string fallback);
bool IsValidDiceString(std::string_view dice);

} // namespace game::tactical_d20_config_reader