# CMakeList.txt : CMake project for game01P, include source and define
#
cmake_minimum_required (VERSION 3.10)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDevInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDevInfo>:ProgramDatabase>>")
endif()

project ("game01P")

# Recursively find all source and header files in the src directory.
# CONFIGURE_DEPENDS ensures CMake re-runs if files are added or removed.
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS 
    "src/*.cpp" 
    "src/*.h"
    "src/*.hpp"
)

# Add source to this project's executable.
add_executable (game01P ${SRC_FILES})

# Add the src directory as an include path #include "core/Game.h"
target_include_directories(game01P PRIVATE "src")

target_compile_options(game01P PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)

find_package(SDL3 CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

target_include_directories(game01P PRIVATE ${MINIAUDIO_INCLUDE_DIRS})

target_link_libraries( game01P 
	PRIVATE 
		SDL3::SDL3 
		EnTT::EnTT
		glm::glm
		imgui::imgui
		$<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
		Tracy::TracyClient
		SDL3_ttf::SDL3_ttf
		spdlog::spdlog
)

set_property(TARGET game01P PROPERTY CXX_STANDARD 23)


# TODO: Add tests and install targets if needed.
