# Playden - Direct ps5-homebrew-ui candidate integration.
# Copyright (C) 2026 Leo Lobato
# SPDX-License-Identifier: GPL-3.0-or-later

set(PLAYDEN_HOMEBREW_UI_REVISION "8a716f294835e26a24ef5612f1535aabffef4080")
set(PLAYDEN_HUI_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/build/dependencies/homebrew-ui" CACHE PATH
    "Isolated ps5-homebrew-ui checkout")
set(PLAYDEN_HOMEBREW_UI_SOURCE_DIR "${PLAYDEN_HUI_SOURCE_DIR}" CACHE PATH
    "Compatibility alias for PLAYDEN_HUI_SOURCE_DIR" FORCE)
set(PLAYDEN_STB_IMAGE_INCLUDE_DIR
    "/Users/leolobato/Documents/Projetos/Personal/GameNative/rmlfocus/third_party/stb" CACHE PATH
    "Directory containing the pinned header-only stb_image.h decoder")

if(NOT EXISTS "${PLAYDEN_HUI_SOURCE_DIR}/src/gfx/renderer.cpp")
    message(FATAL_ERROR
        "ps5-homebrew-ui is not prepared. Run scripts/build-homebrew.sh --prepare-only")
endif()

execute_process(
    COMMAND git -C "${PLAYDEN_HUI_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE playden_hui_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE playden_hui_revision_status)
if(NOT playden_hui_revision_status EQUAL 0 OR
   NOT playden_hui_revision STREQUAL PLAYDEN_HOMEBREW_UI_REVISION)
    message(FATAL_ERROR
        "ps5-homebrew-ui must be ${PLAYDEN_HOMEBREW_UI_REVISION}; found '${playden_hui_revision}'")
endif()
if(NOT EXISTS "${PLAYDEN_STB_IMAGE_INCLUDE_DIR}/stb_image.h")
    message(FATAL_ERROR "PLAYDEN_STB_IMAGE_INCLUDE_DIR must contain stb_image.h")
endif()

file(GLOB playden_hui_gfx_sources CONFIGURE_DEPENDS
    "${PLAYDEN_HUI_SOURCE_DIR}/src/gfx/*.cpp")
file(GLOB playden_hui_widget_sources CONFIGURE_DEPENDS
    "${PLAYDEN_HUI_SOURCE_DIR}/src/ui/*.cpp"
    "${PLAYDEN_HUI_SOURCE_DIR}/src/ui/components/*.cpp")

add_library(playden_hui STATIC
    ${playden_hui_gfx_sources}
    ${playden_hui_widget_sources}
    "${PLAYDEN_HUI_SOURCE_DIR}/src/core/input.cpp")
target_compile_features(playden_hui PUBLIC cxx_std_20)
target_include_directories(playden_hui PUBLIC
    "${PLAYDEN_HUI_SOURCE_DIR}/src"
    "${PLAYDEN_HUI_SOURCE_DIR}/third_party/stb"
    "${PLAYDEN_STB_IMAGE_INCLUDE_DIR}")

add_library(playden_homebrew_model STATIC
    "${CMAKE_SOURCE_DIR}/src/homebrew/simulation.cpp"
    "${CMAKE_SOURCE_DIR}/src/homebrew/storage_sim.cpp"
    "${CMAKE_SOURCE_DIR}/src/homebrew/browse.cpp")
target_compile_features(playden_homebrew_model PUBLIC cxx_std_20)
target_include_directories(playden_homebrew_model PUBLIC "${CMAKE_SOURCE_DIR}/src")

add_library(playden_homebrew_ui STATIC
    "${CMAKE_SOURCE_DIR}/src/homebrew/view.cpp")
target_compile_features(playden_homebrew_ui PUBLIC cxx_std_20)
target_include_directories(playden_homebrew_ui PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(playden_homebrew_ui PUBLIC playden_homebrew_model playden_hui)

if(PLAYDEN_PLATFORM_PS5)
    target_include_directories(playden_hui PUBLIC
        "${PLAYDEN_PS5_DEPS_DIR}/ps5-opengl/ps5-opengl-sdk-1.0.0/sdk/include")
    target_compile_definitions(playden_hui PUBLIC GL_GLEXT_PROTOTYPES=1)
    target_compile_options(playden_hui PUBLIC -fno-exceptions -fno-rtti)
    target_compile_options(playden_homebrew_model PUBLIC -fno-exceptions -fno-rtti)
    target_compile_options(playden_homebrew_ui PUBLIC -fno-exceptions -fno-rtti)
    include("${CMAKE_CURRENT_LIST_DIR}/HomebrewPS5.cmake")
    playden_add_homebrew_ps5_title()
    return()
endif()

if(NOT APPLE)
    message(FATAL_ERROR "The direct ps5-homebrew-ui host currently supports macOS only")
endif()

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET "sdl2>=2.0")
target_sources(playden_hui PRIVATE
    "${PLAYDEN_HUI_SOURCE_DIR}/host/platform_host.cpp")
target_compile_definitions(playden_hui PUBLIC GL_SILENCE_DEPRECATION=1)
target_link_libraries(playden_hui PUBLIC OpenGL::GL)
target_compile_options(playden_homebrew_ui PRIVATE -Wno-deprecated-declarations)

set(playden_homebrew_assets "${CMAKE_BINARY_DIR}/homebrew-assets")
set(playden_homebrew_fonts "${CMAKE_BINARY_DIR}/homebrew-fonts")
set(PLAYDEN_HOMEBREW_FONT_DIR "${playden_homebrew_fonts}" CACHE INTERNAL
    "Baked Barlow atlas directory")
set(playden_hui_baker "${CMAKE_BINARY_DIR}/hui-bake-font")

add_executable(playden_hui_font_baker EXCLUDE_FROM_ALL
    "${PLAYDEN_HUI_SOURCE_DIR}/tools/font-baker/bake_font.cpp")
target_compile_features(playden_hui_font_baker PRIVATE cxx_std_20)
set_target_properties(playden_hui_font_baker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    OUTPUT_NAME "hui-bake-font")

set(playden_barlow_outputs)
foreach(font_spec IN ITEMS
        "Barlow-Regular.ttf|barlow-regular.huifont"
        "Barlow-Medium.ttf|barlow-medium.huifont"
        "Barlow-SemiBold.ttf|barlow-semibold.huifont"
        "BarlowCondensed-Medium.ttf|barlow-condensed-medium.huifont"
        "BarlowCondensed-SemiBold.ttf|barlow-condensed-semibold.huifont")
    string(REPLACE "|" ";" font_fields "${font_spec}")
    list(GET font_fields 0 font_source)
    list(GET font_fields 1 font_output)
    set(font_input "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison/assets/fonts/${font_source}")
    set(font_file "${playden_homebrew_fonts}/${font_output}")
    add_custom_command(
        OUTPUT "${font_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${playden_homebrew_fonts}"
        COMMAND "$<TARGET_FILE:playden_hui_font_baker>" "${font_input}" "${font_file}"
            56 8 2048 european
        DEPENDS playden_hui_font_baker "${font_input}"
        COMMENT "Baking ${font_output} with Latin, Greek, and Cyrillic glyphs"
        VERBATIM)
    list(APPEND playden_barlow_outputs "${font_file}")
endforeach()

set(playden_catalog "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison/catalog.json")
foreach(font_spec IN ITEMS
        "NotoSansCJKsc-Regular.otf|noto-cjk-fallback.huifont|european"
        "NotoSansArabic.ttf|noto-arabic-fallback.huifont|basic")
    string(REPLACE "|" ";" font_fields "${font_spec}")
    list(GET font_fields 0 font_source)
    list(GET font_fields 1 font_output)
    list(GET font_fields 2 font_glyphs)
    set(font_input "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison/assets/fonts/${font_source}")
    set(font_file "${playden_homebrew_fonts}/${font_output}")
    add_custom_command(
        OUTPUT "${font_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${playden_homebrew_fonts}"
        COMMAND "$<TARGET_FILE:playden_hui_font_baker>" "${font_input}" "${font_file}"
            56 8 2048 "${font_glyphs},text=${playden_catalog}"
        DEPENDS playden_hui_font_baker "${font_input}" "${playden_catalog}"
        COMMENT "Baking ${font_output} for shared international fixture text"
        VERBATIM)
    list(APPEND playden_barlow_outputs "${font_file}")
endforeach()

# Supplemental Greek coverage lives outside the immutable comparison assets.
set(playden_greek_font "${playden_homebrew_fonts}/noto-greek-fallback.huifont")
add_custom_command(
    OUTPUT "${playden_greek_font}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${playden_homebrew_fonts}"
    COMMAND "$<TARGET_FILE:playden_hui_font_baker>"
        "${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf" "${playden_greek_font}"
        56 8 2048 "european,text=${playden_catalog}"
    DEPENDS playden_hui_font_baker "${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-OFL.txt" "${playden_catalog}"
    COMMENT "Baking complete Greek fallback coverage"
    VERBATIM)
list(APPEND playden_barlow_outputs "${playden_greek_font}")

add_custom_target(playden_homebrew_assets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-OFL.txt" "${playden_homebrew_fonts}/NotoSans-OFL.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison/assets" "${playden_homebrew_assets}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${playden_homebrew_fonts}" "${playden_homebrew_assets}/fonts"
    DEPENDS ${playden_barlow_outputs}
    VERBATIM)

add_executable(playden-homebrew "${CMAKE_SOURCE_DIR}/src/homebrew/host.cpp")
target_compile_features(playden-homebrew PRIVATE cxx_std_20)
target_compile_definitions(playden-homebrew PRIVATE
    GL_SILENCE_DEPRECATION=1
    PLAYDEN_HOMEBREW_ASSETS_DIR="${playden_homebrew_assets}"
    PLAYDEN_HOMEBREW_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures/ui-comparison"
    PLAYDEN_HOMEBREW_DEFAULT_PROFILE="${CMAKE_BINARY_DIR}/homebrew-profile.json")
target_link_libraries(playden-homebrew PRIVATE
    playden_homebrew_ui PkgConfig::SDL2 OpenGL::GL)
add_dependencies(playden-homebrew playden_homebrew_assets)
set_target_properties(playden-homebrew PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

include(CTest)
if(BUILD_TESTING)
    add_executable(homebrew_focus_motion_test "${CMAKE_SOURCE_DIR}/tests/homebrew_focus_motion_test.cpp")
    target_compile_features(homebrew_focus_motion_test PRIVATE cxx_std_20)
    target_include_directories(homebrew_focus_motion_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
    add_test(NAME homebrew_focus_motion COMMAND homebrew_focus_motion_test)
    add_executable(homebrew_storage_test "${CMAKE_SOURCE_DIR}/tests/homebrew_storage_test.cpp")
    target_compile_features(homebrew_storage_test PRIVATE cxx_std_20)
    target_link_libraries(homebrew_storage_test PRIVATE playden_homebrew_model)
    add_test(NAME homebrew_storage COMMAND homebrew_storage_test)
    add_executable(homebrew_controller_tour_test "${CMAKE_SOURCE_DIR}/tests/homebrew_controller_tour_test.cpp")
    target_compile_features(homebrew_controller_tour_test PRIVATE cxx_std_20)
    target_link_libraries(homebrew_controller_tour_test PRIVATE playden_homebrew_model)
    add_test(NAME homebrew_controller_tour COMMAND homebrew_controller_tour_test "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison")
    add_executable(homebrew_host_input_test "${CMAKE_SOURCE_DIR}/tests/homebrew_host_input_test.cpp")
    target_compile_features(homebrew_host_input_test PRIVATE cxx_std_20)
    target_compile_definitions(homebrew_host_input_test PRIVATE
        PLAYDEN_HOMEBREW_DEFAULT_PROFILE="unused"
        PLAYDEN_HOMEBREW_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures/ui-comparison"
        PLAYDEN_HOMEBREW_ASSETS_DIR="${playden_homebrew_assets}")
    target_link_libraries(homebrew_host_input_test PRIVATE playden_homebrew_ui PkgConfig::SDL2 OpenGL::GL)
    add_test(NAME homebrew_host_input COMMAND homebrew_host_input_test "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison")
    add_executable(homebrew_browse_test "${CMAKE_SOURCE_DIR}/tests/homebrew_browse_test.cpp")
    target_compile_features(homebrew_browse_test PRIVATE cxx_std_20)
    target_link_libraries(homebrew_browse_test PRIVATE playden_homebrew_model)
    add_test(NAME homebrew_browse COMMAND homebrew_browse_test "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison")
    add_executable(homebrew_simulation_test
        "${CMAKE_SOURCE_DIR}/tests/homebrew_simulation_test.cpp")
    target_compile_features(homebrew_simulation_test PRIVATE cxx_std_20)
    target_link_libraries(homebrew_simulation_test PRIVATE playden_homebrew_model)
    add_test(NAME homebrew_simulation
        COMMAND homebrew_simulation_test "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison")

    add_executable(homebrew_font_atlas_test "${CMAKE_SOURCE_DIR}/tests/homebrew_font_atlas_test.cpp")
    target_compile_features(homebrew_font_atlas_test PRIVATE cxx_std_20)
    target_link_libraries(homebrew_font_atlas_test PRIVATE playden_hui)
    add_dependencies(homebrew_font_atlas_test playden_homebrew_assets)
    foreach(font_spec IN ITEMS
            "Barlow-Regular.ttf|barlow-regular.huifont|latin"
            "NotoSansCJKsc-Regular.otf|noto-cjk-fallback.huifont|cff"
            "NotoSansArabic.ttf|noto-arabic-fallback.huifont|arabic")
        string(REPLACE "|" ";" font_fields "${font_spec}")
        list(GET font_fields 0 font_source)
        list(GET font_fields 1 font_output)
        list(GET font_fields 2 font_test)
        add_test(NAME homebrew_font_${font_test}
            COMMAND homebrew_font_atlas_test
                "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison/assets/fonts/${font_source}"
                "${playden_homebrew_fonts}/${font_output}")
    endforeach()

    file(READ "${playden_catalog}" playden_font_catalog_json)
    string(JSON playden_font_game_count LENGTH "${playden_font_catalog_json}" games)
    math(EXPR playden_font_game_last "${playden_font_game_count} - 1")
    set(playden_greek_title "")
    foreach(game_index RANGE 0 ${playden_font_game_last})
        string(JSON game_id GET "${playden_font_catalog_json}" games ${game_index} id)
        if(game_id EQUAL 900003)
            string(JSON playden_greek_title GET "${playden_font_catalog_json}" games ${game_index} title)
        endif()
    endforeach()
    if(playden_greek_title STREQUAL "")
        message(FATAL_ERROR "Greek fixture title missing from font coverage test")
    endif()
    add_test(NAME homebrew_font_greek
        COMMAND homebrew_font_atlas_test
            "${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf"
            "${playden_greek_font}" "${playden_greek_title}")

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME ui_comparison_fixture
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/verify_ui_comparison.py"
            "${CMAKE_SOURCE_DIR}/fixtures/ui-comparison")
    set_tests_properties(ui_comparison_fixture PROPERTIES
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()
