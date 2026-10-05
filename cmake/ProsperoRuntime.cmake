# Offline kit admission; ordinary launcher builds do not compile Wine.
add_library(playden_runtime_kit_manifest STATIC
    "${CMAKE_SOURCE_DIR}/src/runtime/kit_manifest.cpp")
target_include_directories(playden_runtime_kit_manifest PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(playden_runtime_kit_manifest PUBLIC cxx_std_17)
target_link_libraries(playden_runtime_kit_manifest PUBLIC
    playden_runtime_package_installer playden_steam_protocol)

if(PLAYDEN_HOMEBREW_UI)
    add_library(playden_runtime_artifact_hash STATIC
        "${CMAKE_SOURCE_DIR}/src/runtime/artifact_hash.cpp")
    target_include_directories(playden_runtime_artifact_hash PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(playden_runtime_artifact_hash PUBLIC cxx_std_17)
    target_link_libraries(playden_runtime_artifact_hash PRIVATE playden_curl_8_22)
    add_library(playden_runtime_kit_admission STATIC
        "${CMAKE_SOURCE_DIR}/src/runtime/kit_admission.cpp")
    target_include_directories(playden_runtime_kit_admission PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(playden_runtime_kit_admission PUBLIC cxx_std_17)
    target_link_libraries(playden_runtime_kit_admission PUBLIC
        playden_runtime_kit_manifest playden_runtime_artifact_hash)
    if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
        add_executable(runtime_kit_admission_test
            "${CMAKE_SOURCE_DIR}/tests/runtime_kit_manifest_test.cpp")
        target_compile_definitions(runtime_kit_admission_test PRIVATE PLAYDEN_TEST_KIT_ADMISSION=1)
        target_link_libraries(runtime_kit_admission_test PRIVATE playden_runtime_kit_admission)
        add_test(NAME runtime_kit_admission COMMAND runtime_kit_admission_test)
        add_executable(runtime_artifact_hash_test
            "${CMAKE_SOURCE_DIR}/tests/runtime_artifact_hash_test.cpp")
        target_link_libraries(runtime_artifact_hash_test PRIVATE playden_runtime_artifact_hash)
        add_test(NAME runtime_artifact_hash COMMAND runtime_artifact_hash_test)
    endif()
endif()

if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
    add_executable(runtime_kit_manifest_test
        "${CMAKE_SOURCE_DIR}/tests/runtime_kit_manifest_test.cpp")
    target_link_libraries(runtime_kit_manifest_test PRIVATE playden_runtime_kit_manifest)
    add_test(NAME runtime_kit_manifest COMMAND runtime_kit_manifest_test)
endif()
