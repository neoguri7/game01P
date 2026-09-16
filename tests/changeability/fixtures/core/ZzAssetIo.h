#pragma once

#include <filesystem>
#include <fstream>
#include <string>

// V25 [R25] raw file IO and path assembly outside the asset boundary (DOOM 3 FileSystem leak shape:
// neo/framework/FileSystem.cpp:1002 search-path walk, :462 AddGameDirectory). Asset resolution belongs to
// AssetManager/ResourceManager only.
inline std::filesystem::path ZzAssetPath() {
	return std::filesystem::path( "assets/data/zz.json" );
}

inline std::string ZzReadAsset() {
	std::ifstream in( "assets/data/zz.json" );
	std::string line;
	std::getline( in, line );
	return line;
}
