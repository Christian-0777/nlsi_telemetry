cmake_minimum_required(VERSION 3.20)
project(NLSIExclusiveLogbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Concurrent)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../scs_position_plugin" scs_position_plugin)

file(READ "${CMAKE_SOURCE_DIR}/version.json" NLSI_VERSION_JSON)
if(NOT NLSI_VERSION_JSON)
    message(FATAL_ERROR "Unable to read ${CMAKE_SOURCE_DIR}/version.json")
endif()

string(REGEX MATCH "\"version\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"" _nlsi_version_match "${NLSI_VERSION_JSON}")
if(NOT _nlsi_version_match)
    message(FATAL_ERROR "Could not find the version field in version.json")
endif()
set(NLSI_VERSION "${CMAKE_MATCH_1}")

string(REGEX MATCH "\"channel\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"" _nlsi_channel_match "${NLSI_VERSION_JSON}")
if(NOT _nlsi_channel_match)
    message(FATAL_ERROR "Could not find the channel field in version.json")
endif()
set(NLSI_CHANNEL "${CMAKE_MATCH_1}")
string(STRIP "${NLSI_CHANNEL}" NLSI_CHANNEL)

set(NLSI_RELEASE_TAG "v${NLSI_VERSION}")
if(NLSI_CHANNEL AND NOT NLSI_CHANNEL STREQUAL "public")
    set(NLSI_RELEASE_TAG "${NLSI_RELEASE_TAG}-${NLSI_CHANNEL}")
endif()

set(NLSI_BUILD_OUTPUT_ROOT "${CMAKE_SOURCE_DIR}/build")
set(NLSI_DEBUG_OUTPUT_DIR "${NLSI_BUILD_OUTPUT_ROOT}/debug/${NLSI_RELEASE_TAG}")
set(NLSI_INTERMEDIATE_ROOT "${NLSI_BUILD_OUTPUT_ROOT}/intermediate/${NLSI_RELEASE_TAG}")
set(NLSI_INTERMEDIATE_DEBUG_DIR "${NLSI_INTERMEDIATE_ROOT}/debug")
set(NLSI_INTERMEDIATE_RELEASE_DIR "${NLSI_INTERMEDIATE_ROOT}/release")
set(NLSI_RELEASE_OUTPUT_DIR "${NLSI_BUILD_OUTPUT_ROOT}/releases/${NLSI_RELEASE_TAG}")
set(NLSI_TEST_OUTPUT_DIR "${NLSI_BUILD_OUTPUT_ROOT}/tests/${NLSI_RELEASE_TAG}")

file(MAKE_DIRECTORY
    "${NLSI_DEBUG_OUTPUT_DIR}"
    "${NLSI_INTERMEDIATE_DEBUG_DIR}"
    "${NLSI_INTERMEDIATE_RELEASE_DIR}"
    "${NLSI_RELEASE_OUTPUT_DIR}"
    "${NLSI_TEST_OUTPUT_DIR}"
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${NLSI_DEBUG_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${NLSI_DEBUG_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${NLSI_RELEASE_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_RELEASE_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_RELEASE_OUTPUT_DIR}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${NLSI_INTERMEDIATE_DEBUG_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${NLSI_INTERMEDIATE_DEBUG_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${NLSI_INTERMEDIATE_RELEASE_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_INTERMEDIATE_RELEASE_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_INTERMEDIATE_RELEASE_DIR}")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${NLSI_INTERMEDIATE_DEBUG_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${NLSI_INTERMEDIATE_DEBUG_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${NLSI_INTERMEDIATE_RELEASE_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_INTERMEDIATE_RELEASE_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_INTERMEDIATE_RELEASE_DIR}")

set(CMAKE_PDB_OUTPUT_DIRECTORY "${NLSI_INTERMEDIATE_DEBUG_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG "${NLSI_INTERMEDIATE_DEBUG_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${NLSI_INTERMEDIATE_RELEASE_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_INTERMEDIATE_RELEASE_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_INTERMEDIATE_RELEASE_DIR}")

set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${NLSI_INTERMEDIATE_DEBUG_DIR}")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY_DEBUG "${NLSI_INTERMEDIATE_DEBUG_DIR}")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY_RELEASE "${NLSI_INTERMEDIATE_RELEASE_DIR}")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_INTERMEDIATE_RELEASE_DIR}")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_INTERMEDIATE_RELEASE_DIR}")

enable_testing()

qt_add_executable(NLSI-Exclusive-Logbook MANUAL_FINALIZATION
    app/main.cpp
    app/App.cpp
    app/SingleInstance.cpp
    gui/MainWindow.cpp
    gui/IconTheme.cpp
    gui/PageSupport.cpp
    gui/JobPdfExporter.cpp
    gui/DashboardPage.cpp
    gui/DetailPages.cpp
    gui/HistoryPages.cpp
    gui/ModLogParser.cpp
    telemetry/TelemetryCore.cpp
    telemetry/TelemetryModel.cpp
    telemetry/TelemetryUiState.cpp
    providers/TruckSimGpsProvider.cpp
    providers/ScsPositionProvider.cpp
    session/SessionManager.cpp
    session/JobManager.cpp
    session/HistoryStore.cpp
    logging/Logger.cpp
    logging/TelemetryRecorder.cpp
    updater/GitHubUpdater.cpp
    resources/app.rc
    resources/resources.qrc
)

set_target_properties(NLSI-Exclusive-Logbook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${NLSI_DEBUG_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${NLSI_RELEASE_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_RELEASE_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_RELEASE_OUTPUT_DIR}"
    PDB_OUTPUT_DIRECTORY "${NLSI_INTERMEDIATE_DEBUG_DIR}"
    PDB_OUTPUT_DIRECTORY_DEBUG "${NLSI_INTERMEDIATE_DEBUG_DIR}"
    PDB_OUTPUT_DIRECTORY_RELEASE "${NLSI_INTERMEDIATE_RELEASE_DIR}"
    PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_INTERMEDIATE_RELEASE_DIR}"
    PDB_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_INTERMEDIATE_RELEASE_DIR}"
    COMPILE_PDB_OUTPUT_DIRECTORY "${NLSI_INTERMEDIATE_DEBUG_DIR}"
    COMPILE_PDB_OUTPUT_DIRECTORY_DEBUG "${NLSI_INTERMEDIATE_DEBUG_DIR}"
    COMPILE_PDB_OUTPUT_DIRECTORY_RELEASE "${NLSI_INTERMEDIATE_RELEASE_DIR}"
    COMPILE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_INTERMEDIATE_RELEASE_DIR}"
    COMPILE_PDB_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_INTERMEDIATE_RELEASE_DIR}"
)

set_target_properties(NLSI-Exclusive-Logbook PROPERTIES WIN32_EXECUTABLE TRUE)

add_custom_command(TARGET NLSI-Exclusive-Logbook POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NLSI-Exclusive-Logbook>/resources"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NLSI-Exclusive-Logbook>/config"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NLSI-Exclusive-Logbook>/logs"
    VERBATIM
)

target_include_directories(NLSI-Exclusive-Logbook PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/providers
    ${CMAKE_CURRENT_SOURCE_DIR}/session
    ${CMAKE_CURRENT_SOURCE_DIR}/logging
    ${CMAKE_CURRENT_SOURCE_DIR}/updater
)

target_link_libraries(NLSI-Exclusive-Logbook PRIVATE
    Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Concurrent shell32
)
target_compile_definitions(NLSI-Exclusive-Logbook PRIVATE
    NLSI_VERSION="${NLSI_VERSION}"
    NLSI_CHANNEL="${NLSI_CHANNEL}"
)
if(MSVC)
    target_compile_options(NLSI-Exclusive-Logbook PRIVATE /W4)
endif()

qt_finalize_executable(NLSI-Exclusive-Logbook)

get_filename_component(NLSI_QT_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)
find_program(NLSI_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt
    HINTS "${NLSI_QT_ROOT}/bin"
)
if(NLSI_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET NLSI-Exclusive-Logbook POST_BUILD
        COMMAND "${NLSI_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:NLSI-Exclusive-Logbook>"
        COMMENT "Deploying Qt runtime beside NLSI-Exclusive-Logbook"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_executable(nlsi_core_tests
        tests/version_tests.cpp
        telemetry/TelemetryModel.cpp
        telemetry/TelemetryUiState.cpp
        providers/NLSIProvider.cpp
        providers/TruckSimGpsProvider.cpp
        providers/ScsPositionProvider.cpp
        session/SessionManager.cpp
        session/JobManager.cpp
        session/HistoryStore.cpp
        logging/Logger.cpp
        logging/TelemetryRecorder.cpp
        updater/GitHubUpdater.cpp
        app/SingleInstance.cpp
    )

    set_target_properties(nlsi_core_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${NLSI_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${NLSI_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${NLSI_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_TEST_OUTPUT_DIR}"
        PDB_OUTPUT_DIRECTORY "${NLSI_INTERMEDIATE_DEBUG_DIR}"
        PDB_OUTPUT_DIRECTORY_DEBUG "${NLSI_INTERMEDIATE_DEBUG_DIR}"
        PDB_OUTPUT_DIRECTORY_RELEASE "${NLSI_INTERMEDIATE_RELEASE_DIR}"
        PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_INTERMEDIATE_RELEASE_DIR}"
        PDB_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_INTERMEDIATE_RELEASE_DIR}"
        COMPILE_PDB_OUTPUT_DIRECTORY "${NLSI_INTERMEDIATE_DEBUG_DIR}"
        COMPILE_PDB_OUTPUT_DIRECTORY_DEBUG "${NLSI_INTERMEDIATE_DEBUG_DIR}"
        COMPILE_PDB_OUTPUT_DIRECTORY_RELEASE "${NLSI_INTERMEDIATE_RELEASE_DIR}"
        COMPILE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_INTERMEDIATE_RELEASE_DIR}"
        COMPILE_PDB_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_INTERMEDIATE_RELEASE_DIR}"
    )

    target_include_directories(nlsi_core_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/app
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
        ${CMAKE_CURRENT_SOURCE_DIR}/providers
        ${CMAKE_CURRENT_SOURCE_DIR}/session
        ${CMAKE_CURRENT_SOURCE_DIR}/logging
        ${CMAKE_CURRENT_SOURCE_DIR}/updater
    )
    target_link_libraries(nlsi_core_tests PRIVATE Qt6::Core Qt6::Network ws2_32)

    if(MSVC)
        target_compile_options(nlsi_core_tests PRIVATE /W4)
    endif()

    add_test(NAME nlsi_core_tests COMMAND nlsi_core_tests)
    set_tests_properties(nlsi_core_tests PROPERTIES
        ENVIRONMENT "PATH=${NLSI_QT_ROOT}/bin;NLSI_DISABLE_UPDATE_CHECK=1"
    )

    add_executable(nlsi_telemetry_benchmark
        tests/telemetry_benchmark.cpp
        logging/TelemetryRecorder.cpp
    )
    set_target_properties(nlsi_telemetry_benchmark PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${NLSI_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${NLSI_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${NLSI_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_TEST_OUTPUT_DIR}"
    )
    target_include_directories(nlsi_telemetry_benchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/logging
    )
    target_link_libraries(nlsi_telemetry_benchmark PRIVATE Qt6::Core psapi)
    if(MSVC)
        target_compile_options(nlsi_telemetry_benchmark PRIVATE /W4)
    endif()

    add_executable(nlsi_gui_tests
        tests/main_window_tests.cpp
        gui/MainWindow.cpp
        gui/IconTheme.cpp
        gui/PageSupport.cpp
        gui/JobPdfExporter.cpp
        gui/DashboardPage.cpp
        gui/DetailPages.cpp
        gui/HistoryPages.cpp
        gui/ModLogParser.cpp
        telemetry/TelemetryCore.cpp
        telemetry/TelemetryModel.cpp
        telemetry/TelemetryUiState.cpp
        providers/TruckSimGpsProvider.cpp
        providers/ScsPositionProvider.cpp
        session/SessionManager.cpp
        session/JobManager.cpp
        session/HistoryStore.cpp
        logging/Logger.cpp
        logging/TelemetryRecorder.cpp
        updater/GitHubUpdater.cpp
        app/SingleInstance.cpp
        resources/resources.qrc
    )

    set_target_properties(nlsi_gui_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${NLSI_RELEASE_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${NLSI_RELEASE_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${NLSI_RELEASE_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NLSI_RELEASE_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${NLSI_RELEASE_OUTPUT_DIR}"
    )

    target_include_directories(nlsi_gui_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/app
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
        ${CMAKE_CURRENT_SOURCE_DIR}/providers
        ${CMAKE_CURRENT_SOURCE_DIR}/session
        ${CMAKE_CURRENT_SOURCE_DIR}/logging
        ${CMAKE_CURRENT_SOURCE_DIR}/updater
    )
    target_link_libraries(nlsi_gui_tests PRIVATE
        Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Concurrent shell32
    )

    if(MSVC)
        target_compile_options(nlsi_gui_tests PRIVATE /W4)
    endif()

    add_test(NAME nlsi_gui_tests COMMAND nlsi_gui_tests)
    set_tests_properties(nlsi_gui_tests PROPERTIES
        ENVIRONMENT
            "PATH=${NLSI_QT_ROOT}/bin;QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${NLSI_QT_ROOT}/plugins/platforms;NLSI_DISABLE_UPDATE_CHECK=1"
    )
endif()
