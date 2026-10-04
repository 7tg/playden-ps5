cmake_minimum_required(VERSION 3.24)
project(PlaydenLauncher VERSION 0.1.0 LANGUAGES C CXX)

set(PLAYDEN_CAPTURE_AFTER_SECONDS "0" CACHE STRING "Explicit native framebuffer capture time; 0 disables")
option(PLAYDEN_MSAA_PROBE "Run a bounded native 4/0/4 MSAA comparison" OFF)
option(PLAYDEN_DEVELOPER_FIXTURES "Compile explicitly synthetic development scenes" OFF)
option(PLAYDEN_PLATFORM_PS5 "Build a PS5 native title" OFF)
set(PLAYDEN_PS5_TITLE_ID "" CACHE STRING "Explicit PS5 title id (required for PS5 packaging)")
set(PLAYDEN_RMLFOCUS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../rmlfocus" CACHE PATH "Pinned RmlFocus source checkout")
set(PLAYDEN_RMLFOCUS_REVISION "31d2d8fd39ca3e3bb3ba91a43f50561c04429d57")

execute_process(COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
	OUTPUT_VARIABLE playden_revision OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git -C "${CMAKE_SOURCE_DIR}" status --porcelain
	OUTPUT_VARIABLE playden_changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(playden_changes)
	string(APPEND playden_revision "+dirty")
endif()

execute_process(COMMAND git -C "${PLAYDEN_RMLFOCUS_SOURCE_DIR}" rev-parse HEAD
	OUTPUT_VARIABLE rmlfocus_revision OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE revision_status)
if(NOT revision_status EQUAL 0 OR NOT rmlfocus_revision STREQUAL PLAYDEN_RMLFOCUS_REVISION)
	message(FATAL_ERROR "RmlFocus must be ${PLAYDEN_RMLFOCUS_REVISION}; found '${rmlfocus_revision}' in ${PLAYDEN_RMLFOCUS_SOURCE_DIR}")
endif()

if(PLAYDEN_PLATFORM_PS5)
	set(playden_dependency_platform ps5)
else()
	set(playden_dependency_platform host)
	execute_process(COMMAND "${CMAKE_SOURCE_DIR}/scripts/prepare-rmlfocus.sh" host
		"${PLAYDEN_RMLFOCUS_SOURCE_DIR}" "${CMAKE_BINARY_DIR}"
		RESULT_VARIABLE prepare_status COMMAND_ECHO STDOUT)
	if(NOT prepare_status EQUAL 0)
		message(FATAL_ERROR "Could not prepare the pinned RmlFocus package")
	endif()
endif()

set(playden_rmlfocus_prefix "${CMAKE_BINARY_DIR}/dependencies/${playden_dependency_platform}-prefix")
if(PLAYDEN_PLATFORM_PS5)
	set(playden_rmlui_prefix "${PLAYDEN_RMLFOCUS_SOURCE_DIR}/build/ps5-prefix")
else()
	set(playden_rmlui_prefix "${PLAYDEN_RMLFOCUS_SOURCE_DIR}/build/mac-prefix")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${playden_rmlfocus_prefix}" "${playden_rmlui_prefix}")
set(RmlFocus_DIR "${playden_rmlfocus_prefix}/lib/cmake/RmlFocus")
set(RmlUi_DIR "${playden_rmlui_prefix}/lib/cmake/RmlUi")
if(PLAYDEN_PLATFORM_PS5 AND NOT TARGET Freetype::Freetype)
	add_library(Freetype::Freetype UNKNOWN IMPORTED)
	set_target_properties(Freetype::Freetype PROPERTIES
		IMPORTED_LOCATION "${playden_rmlui_prefix}/lib/libfreetype.a"
		INTERFACE_INCLUDE_DIRECTORIES "${playden_rmlui_prefix}/include/freetype2")
elseif(APPLE)
	find_package(Freetype 2.14.3 EXACT REQUIRED)
	find_package(CURL 8.7.1 EXACT REQUIRED)
endif()
find_package(RmlFocus CONFIG REQUIRED COMPONENTS RmlUi)
find_package(Threads REQUIRED)

file(READ "${CMAKE_SOURCE_DIR}/dependencies.lock.json" playden_dependency_lock)
foreach(utf8proc_file utf8proc.c utf8proc.h utf8proc_data.c LICENSE.md)
 string(JSON expected_utf8proc_hash GET "${playden_dependency_lock}" utf8proc files "${utf8proc_file}")
 file(SHA256 "${CMAKE_SOURCE_DIR}/third_party/utf8proc/${utf8proc_file}" actual_utf8proc_hash)
 if(NOT actual_utf8proc_hash STREQUAL expected_utf8proc_hash)
  message(FATAL_ERROR "Pinned utf8proc source hash mismatch: ${utf8proc_file}")
 endif()
endforeach()

add_library(playden_core STATIC
 third_party/utf8proc/utf8proc.c
 src/library/query.cpp
 src/library/cache.cpp
 src/ui/library.cpp
 src/ui/details.cpp
 src/ui/install_fixture.cpp
	src/app/application.cpp
	src/app/preferences.cpp
	src/diagnostics/frame_stats.cpp
 src/diagnostics/phase_stats.cpp
	src/platform/storage.cpp
	src/ui/shell.cpp)
target_include_directories(playden_core PUBLIC "${CMAKE_SOURCE_DIR}/src" PRIVATE "${CMAKE_SOURCE_DIR}/third_party/utf8proc")
target_compile_definitions(playden_core PRIVATE UTF8PROC_STATIC)
target_compile_features(playden_core PUBLIC cxx_std_17)
target_compile_definitions(playden_core PUBLIC PLAYDEN_REVISION="${playden_revision}"
	PLAYDEN_RMLFOCUS_REVISION="${PLAYDEN_RMLFOCUS_REVISION}")
target_link_libraries(playden_core PUBLIC RmlFocus::RmlUi Threads::Threads)
if(PLAYDEN_DEVELOPER_FIXTURES)
 find_package(Python3 REQUIRED COMPONENTS Interpreter)
 add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/developer_fixture.cpp"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/compile_library_fixture.py"
   "${CMAKE_SOURCE_DIR}/fixtures/library.json" "${CMAKE_BINARY_DIR}/developer_fixture.cpp"
  DEPENDS tools/compile_library_fixture.py fixtures/library.json VERBATIM)
 target_sources(playden_core PRIVATE "${CMAKE_BINARY_DIR}/developer_fixture.cpp")
 target_compile_definitions(playden_core PUBLIC PLAYDEN_DEVELOPER_FIXTURES=1)
endif()


set(playden_renderer "${PLAYDEN_RMLFOCUS_SOURCE_DIR}/poc/rmlui-gl3/RmlUi_Renderer_GL3_rf.cpp")
set(playden_rmlui_backends "${PLAYDEN_RMLFOCUS_SOURCE_DIR}/.deps/RmlUi/Backends")
set(playden_fonts "${PLAYDEN_RMLFOCUS_SOURCE_DIR}/demo/assets/fonts")

if(PLAYDEN_PLATFORM_PS5)
	include(cmake/PS5.cmake)
	playden_add_ps5_title()
elseif(APPLE)
	find_package(OpenGL REQUIRED)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET "sdl2=2.32.72")

	add_executable(playden-launcher
		src/platform/mac/main.cpp
		"${playden_renderer}"
		"${playden_rmlui_backends}/RmlUi_Platform_SDL.cpp")
	target_include_directories(playden-launcher PRIVATE "${playden_rmlui_backends}")
	target_compile_definitions(playden-launcher PRIVATE RMLUI_SDL_VERSION_MAJOR=2
		PLAYDEN_ASSETS_DIR="${CMAKE_BINARY_DIR}/assets"
		PLAYDEN_STORAGE_DIR="${CMAKE_BINARY_DIR}/storage")
	target_link_libraries(playden-launcher PRIVATE playden_core RmlUi::Core PkgConfig::SDL2 OpenGL::GL)
	set_target_properties(playden-launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
	add_custom_target(playden-assets ALL
		COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${CMAKE_BINARY_DIR}/assets"
		COMMAND "${CMAKE_COMMAND}" -E copy_directory "${playden_fonts}" "${CMAKE_BINARY_DIR}/assets/fonts"
		VERBATIM)
	add_dependencies(playden-launcher playden-assets)
else()
	message(FATAL_ERROR "The graphical launcher currently supports macOS and PS5")
endif()

if(NOT PLAYDEN_PLATFORM_PS5)
	include(CTest)
	if(BUILD_TESTING)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME library_fixture_compiler COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/tests" -p test_library_fixture.py)
        set_tests_properties(library_fixture_compiler PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
        add_executable(memory_probe_test tests/memory_probe_test.cpp src/platform/ps5/memory_probe.cpp)
        target_include_directories(memory_probe_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_features(memory_probe_test PRIVATE cxx_std_17)
        add_test(NAME memory_probe COMMAND memory_probe_test)
        add_executable(process_memory_test tests/process_memory_test.cpp src/platform/ps5/process_memory.cpp)
        target_include_directories(process_memory_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_features(process_memory_test PRIVATE cxx_std_17)
        add_test(NAME process_memory COMMAND process_memory_test)
		foreach(test_name preferences frame_stats phase_stats library_query library_cache)
			add_executable("${test_name}_test" "tests/${test_name}_test.cpp")
			target_link_libraries("${test_name}_test" PRIVATE playden_core)
			add_test(NAME "${test_name}" COMMAND "${test_name}_test")
		endforeach()
		foreach(test_name shell library_navigation artwork_binding search)
			add_executable(${test_name}_test tests/${test_name}_test.cpp)
			target_link_libraries(${test_name}_test PRIVATE playden_core RmlUi::Core)
			add_dependencies(${test_name}_test playden-assets)
			add_test(NAME ${test_name} COMMAND ${test_name}_test "${CMAKE_BINARY_DIR}/assets")
		endforeach()
	endif()
endif()
