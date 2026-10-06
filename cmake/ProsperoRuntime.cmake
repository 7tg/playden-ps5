# Offline kit admission; ordinary launcher builds do not compile Wine.
set(PLAYDEN_PROSPERO_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/prospero-win" CACHE PATH
    "Exact pinned prospero-win source; overrides must match dependencies.lock.json")
option(PLAYDEN_PROSPERO_RELEASE_SOURCE "Require a published prospero source pin" OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(prospero_source_check_args --source "${PLAYDEN_PROSPERO_SOURCE_DIR}")
if(PLAYDEN_PROSPERO_RELEASE_SOURCE)
    list(APPEND prospero_source_check_args --release)
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prospero_source.py"
    ${prospero_source_check_args}
    RESULT_VARIABLE prospero_source_check_result ERROR_VARIABLE prospero_source_check_error
    OUTPUT_QUIET)
if(NOT prospero_source_check_result STREQUAL "0")
    message(FATAL_ERROR "${prospero_source_check_error}\nInitialize third_party/prospero-win or select a clean checkout at the locked revision.")
endif()

if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
    add_test(NAME prospero_source_pin COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/tests" -p test_prospero_source.py)
    add_test(NAME prospero_bottle_preparation_evidence COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/tests" -p test_bottle_preparation_evidence.py)
    foreach(contract title_launch title_session app_profile game_profile profile_catalog)
        set(contract_sources
            "${PLAYDEN_PROSPERO_SOURCE_DIR}/tests/test_pw_${contract}.c"
            "${PLAYDEN_PROSPERO_SOURCE_DIR}/src/pw_${contract}.c")
        if(contract STREQUAL "game_profile")
            list(APPEND contract_sources "${PLAYDEN_PROSPERO_SOURCE_DIR}/src/pw_app_profile.c")
        elseif(contract STREQUAL "profile_catalog")
            list(APPEND contract_sources "${PLAYDEN_PROSPERO_SOURCE_DIR}/src/pw_game_profile.c"
                "${PLAYDEN_PROSPERO_SOURCE_DIR}/src/pw_app_profile.c")
        endif()
        add_executable(prospero_${contract}_contract ${contract_sources})
        target_compile_features(prospero_${contract}_contract PRIVATE c_std_11)
        target_compile_options(prospero_${contract}_contract PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        if(contract STREQUAL "title_session")
            find_package(Threads REQUIRED)
            target_link_libraries(prospero_${contract}_contract PRIVATE Threads::Threads)
        endif()
        add_test(NAME prospero_${contract}_contract COMMAND prospero_${contract}_contract)
    endforeach()
    # Consume records emitted by the pinned production wrapper, so durable
    # schema and identifier differences cannot be hidden by hand-built mocks.
    target_sources(runtime_session_reconciler_test PRIVATE "${PLAYDEN_PROSPERO_SOURCE_DIR}/src/pw_title_session.c")
    target_include_directories(runtime_session_reconciler_test PRIVATE "${PLAYDEN_PROSPERO_SOURCE_DIR}/src")
    target_link_libraries(runtime_session_reconciler_test PRIVATE Threads::Threads)
    foreach(session_storage_test runtime_session_storage_test runtime_session_storage_native_policy_test)
        target_sources(${session_storage_test} PRIVATE "${PLAYDEN_PROSPERO_SOURCE_DIR}/src/pw_title_session.c")
        target_include_directories(${session_storage_test} PRIVATE "${PLAYDEN_PROSPERO_SOURCE_DIR}/src")
        target_link_libraries(${session_storage_test} PRIVATE Threads::Threads)
    endforeach()
endif()

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
    target_link_libraries(playden_runtime_artifact_hash PRIVATE playden_curl_8_22 playden_managed_directory)
    add_library(playden_runtime_kit_admission STATIC
        "${CMAKE_SOURCE_DIR}/src/runtime/kit_admission.cpp")
    target_include_directories(playden_runtime_kit_admission PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(playden_runtime_kit_admission PUBLIC cxx_std_17)
    target_link_libraries(playden_runtime_kit_admission PUBLIC
        playden_runtime_kit_manifest playden_runtime_artifact_hash)
    if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
        add_executable(prospero_kit_consumer_test
            "${CMAKE_SOURCE_DIR}/tests/kit_producer_consumer_test.cpp")
        target_link_libraries(prospero_kit_consumer_test PRIVATE playden_runtime_kit_admission)
        add_test(NAME prospero_kit_producer_consumer
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_kit_producer_consumer.py"
                --source "${PLAYDEN_PROSPERO_SOURCE_DIR}"
                --consumer $<TARGET_FILE:prospero_kit_consumer_test>)
        add_executable(runtime_kit_admission_test
            "${CMAKE_SOURCE_DIR}/tests/runtime_kit_manifest_test.cpp")
        target_compile_definitions(runtime_kit_admission_test PRIVATE PLAYDEN_TEST_KIT_ADMISSION=1)
        target_link_libraries(runtime_kit_admission_test PRIVATE playden_runtime_kit_admission)
        add_test(NAME runtime_kit_admission COMMAND runtime_kit_admission_test)
        add_executable(runtime_artifact_hash_test
            "${CMAKE_SOURCE_DIR}/tests/runtime_artifact_hash_test.cpp")
        target_link_libraries(runtime_artifact_hash_test PRIVATE playden_runtime_artifact_hash)
        add_test(NAME runtime_artifact_hash COMMAND runtime_artifact_hash_test)
        add_executable(runtime_artifact_hash_native_policy_test
            "${CMAKE_SOURCE_DIR}/tests/runtime_artifact_hash_test.cpp"
            "${CMAKE_SOURCE_DIR}/src/runtime/artifact_hash.cpp"
            "${CMAKE_SOURCE_DIR}/src/platform/managed_directory.cpp")
        target_include_directories(runtime_artifact_hash_native_policy_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_features(runtime_artifact_hash_native_policy_test PRIVATE cxx_std_17)
        target_compile_definitions(runtime_artifact_hash_native_policy_test PRIVATE
            PLAYDEN_PLATFORM_PS5=1 PLAYDEN_TEST_AT_UNAVAILABLE=1)
        target_link_libraries(runtime_artifact_hash_native_policy_test PRIVATE playden_curl_8_22)
        add_test(NAME runtime_artifact_hash_native_policy COMMAND runtime_artifact_hash_native_policy_test)
    endif()
endif()

if(BUILD_TESTING AND NOT PLAYDEN_PLATFORM_PS5)
    add_executable(runtime_kit_manifest_test
        "${CMAKE_SOURCE_DIR}/tests/runtime_kit_manifest_test.cpp")
    target_link_libraries(runtime_kit_manifest_test PRIVATE playden_runtime_kit_manifest)
    add_test(NAME runtime_kit_manifest COMMAND runtime_kit_manifest_test)
endif()
