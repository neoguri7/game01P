#include "gameplay/tactical_d20/config/FTacticalD20ConfigReader.h"

#include <regex>

namespace game::tactical_d20_config_reader {

std::string FindObjectBody(std::string_view text, std::string_view key) {
    const auto keyToken = std::string("\"") + std::string(key) + "\"";
    auto keyPos = text.find(keyToken);
    if (keyPos == std::string_view::npos) return {};
    auto open = text.find('{', keyPos + keyToken.size());
    if (open == std::string_view::npos) return {};

    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        if (text[i] == '}') --depth;
        if (depth == 0) return std::string(text.substr(open + 1, i - open - 1));
    }
    return {};
}

std::string FindArrayBody(std::string_view text, std::string_view key) {
    const auto keyToken = std::string("\"") + std::string(key) + "\"";
    auto keyPos = text.find(keyToken);
    if (keyPos == std::string_view::npos) return {};
    auto open = text.find('[', keyPos + keyToken.size());
    if (open == std::string_view::npos) return {};

    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '[') ++depth;
        if (text[i] == ']') --depth;
        if (depth == 0) return std::string(text.substr(open + 1, i - open - 1));
    }
    return {};
}

std::vector<std::string> SplitObjectBodies(std::string_view arrayBody) {
    std::vector<std::string> objects;
    for (std::size_t i = 0; i < arrayBody.size(); ++i) {
        if (arrayBody[i] != '{') continue;
        int depth = 0;
        const auto start = i;
        for (; i < arrayBody.size(); ++i) {
            if (arrayBody[i] == '{') ++depth;
            if (arrayBody[i] == '}') --depth;
            if (depth == 0) {
                objects.emplace_back(arrayBody.substr(start + 1, i - start - 1));
                break;
            }
        }
    }
    return objects;
}

std::vector<FTacticalD20TileConfig> ParseTiles(std::string_view arrayBody) {
    std::vector<FTacticalD20TileConfig> tiles;
    const std::regex tilePattern(R"(\{\s*"x"\s*:\s*(-?\d+)\s*,\s*"y"\s*:\s*(-?\d+)\s*\})");
    const std::string source(arrayBody);
    for (std::sregex_iterator it(source.begin(), source.end(), tilePattern), end; it != end; ++it) {
        tiles.push_back({std::stoi((*it)[1].str()), std::stoi((*it)[2].str())});
    }
    return tiles;
}

std::vector<std::string> ReadStringArray(std::string_view text, std::string_view key, std::vector<std::string> fallback) {
    const auto array = FindArrayBody(text, key);
    if (array.empty()) return fallback;

    std::vector<std::string> values;
    const std::regex itemPattern("\\\"([^\\\"]*)\\\"");
    const std::string source(array);
    for (std::sregex_iterator it(source.begin(), source.end(), itemPattern), end; it != end; ++it) {
        values.push_back((*it)[1].str());
    }
    return values.empty() ? fallback : values;
}

int ReadInt(std::string_view text, std::string_view key, int fallback) {
    const std::regex pattern(std::string("\"") + std::string(key) + R"("\s*:\s*(-?\d+))");
    const std::string source(text);
    std::smatch match;
    if (!std::regex_search(source, match, pattern)) return fallback;
    return std::stoi(match[1].str());
}

float ReadFloat(std::string_view text, std::string_view key, float fallback) {
    const std::regex pattern(std::string("\"") + std::string(key) + R"("\s*:\s*(-?\d+(?:\.\d+)?))");
    const std::string source(text);
    std::smatch match;
    if (!std::regex_search(source, match, pattern)) return fallback;
    return std::stof(match[1].str());
}

bool ReadBool(std::string_view text, std::string_view key, bool fallback) {
    const std::regex pattern(std::string("\"") + std::string(key) + R"("\s*:\s*(true|false))");
    const std::string source(text);
    std::smatch match;
    if (!std::regex_search(source, match, pattern)) return fallback;
    return match[1].str() == "true";
}

std::string ReadString(std::string_view text, std::string_view key, std::string fallback) {
    const std::regex pattern(std::string("\"") + std::string(key) + "\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    const std::string source(text);
    std::smatch match;
    if (!std::regex_search(source, match, pattern)) return fallback;
    return match[1].str();
}

bool IsValidDiceString(std::string_view dice) {
    const std::regex pattern(R"(^[1-9][0-9]*d[1-9][0-9]*$)");
    const std::string source(dice);
    return std::regex_match(source, pattern);
}

} // namespace game::tactical_d20_config_reader
