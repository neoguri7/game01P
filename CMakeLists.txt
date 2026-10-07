# CMakeList.txt : CMake project for game01P, include source and define
#
cmake_minimum_required (VERSION 3.21)

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
list(FILTER SRC_FILES EXCLUDE REGEX "/main\\.cpp$")
add_library(game01P_runtime STATIC ${SRC_FILES})
add_executable(game01P "src/main.cpp")
target_link_libraries(game01P PRIVATE game01P_runtime)

# Add the src directory as an include path #include "core/Game.h"
target_include_directories(game01P_runtime PUBLIC "src")

target_compile_options(game01P_runtime PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
	$<$<CXX_COMPILER_ID:MSVC>:/utf-8>
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
find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(game01P_runtime PRIVATE ${MINIAUDIO_INCLUDE_DIRS})

target_link_libraries(game01P_runtime
	PUBLIC
		SDL3::SDL3
		EnTT::EnTT
		glm::glm
		imgui::imgui
		$<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
		Tracy::TracyClient
		SDL3_ttf::SDL3_ttf
		spdlog::spdlog
		nlohmann_json::nlohmann_json
)

set_target_properties(game01P game01P_runtime PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_compile_options(game01P PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

include(CTest)
if(BUILD_TESTING)
    add_executable(dungeon_tests "tests/DungeonTests.cpp")
    target_link_libraries(dungeon_tests PRIVATE game01P_runtime)
    set_target_properties(dungeon_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
    add_test(NAME dungeon_rules COMMAND dungeon_tests)
    set_tests_properties(dungeon_rules PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME dungeon_title_smoke COMMAND game01P --ember --smoke-test)
    add_test(NAME dungeon_battle_smoke COMMAND game01P --ember --smoke-test)
    add_test(NAME forest_title_smoke COMMAND game01P --forest --smoke-test)
    add_test(NAME forest_battle_smoke COMMAND game01P --forest --smoke-test)
    add_test(NAME forest_prototype_checks COMMAND game01P --forest --verify-prototype)
    set_tests_properties(dungeon_title_smoke dungeon_battle_smoke forest_title_smoke forest_battle_smoke forest_prototype_checks PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:game01P>"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software"
        TIMEOUT 30
        RUN_SERIAL TRUE)
    set_property(TEST dungeon_battle_smoke APPEND PROPERTY ENVIRONMENT "GAME01P_START=battle")
    set_property(TEST forest_battle_smoke APPEND PROPERTY ENVIRONMENT "GAME01P_START=battle")
    if(WIN32)
        add_custom_command(TARGET dungeon_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:dungeon_tests> $<TARGET_FILE_DIR:dungeon_tests>
            COMMAND_EXPAND_LISTS)
    endif()
endif()

add_custom_command(TARGET game01P POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/assets"
		"$<TARGET_FILE_DIR:game01P>/assets"
)

if (WIN32)
	add_custom_command(TARGET game01P POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_RUNTIME_DLLS:game01P>
			$<TARGET_FILE_DIR:game01P>
		COMMAND_EXPAND_LISTS
	)
endif()
