#pragma once

#include <string>
#include <unordered_map>

// V23 [R23] the universal key/value bag as runtime currency (DOOM 3 idDict shape: neo/idlib/Dict.h:240
// GetString/GetFloat, neo/game/Entity.h:122 spawnArgs). String-key reads and string/string bags belong to
// the asset loader boundary only.
struct FZzDictBag {
	std::unordered_map<std::string, std::string> spawnArgs;

	int Health( const FZzDictBag & args ) const {
		return args.GetInt( "health" );
	}

	float Speed( const FZzDictBag & args ) const {
		return args.GetFloat( "speed" );
	}
};
