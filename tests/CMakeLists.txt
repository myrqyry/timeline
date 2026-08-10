cmake_minimum_required(VERSION 3.28...3.30)

project(obs-timeline-plugin-tests)

find_package(Qt6 REQUIRED COMPONENTS Core Test Gui Widgets Svg)

set(CMAKE_AUTOMOC ON)

set(TEST_SOURCES
    main_test.cpp
    mock_frontend.cpp
    mock_obs_websocket.cpp
    ../src/obs-executor.cpp
    ../src/headless-timeline.cpp
    ../src/timeline-scheduler.cpp
    ../src/timeline-model.cpp
    ../src/timeline-event-item.cpp
    ../src/timeline-serialization.cpp
    ../src/macro-button-model.cpp
    ../src/macro-button-widget.cpp
    ../src/macro-serialization.cpp
    ../src/macro-strip-dock.cpp
    ../src/event-editor.cpp
    # Exclude plugin-main.cpp because it defines obs_module_load/unload which conflicts or isn't needed
)

# Include directories from src
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(run_tests ${TEST_SOURCES})

target_compile_definitions(run_tests PRIVATE
    TIMELINE_EXPECTED_OBS_MAJOR=${TIMELINE_EXPECTED_OBS_MAJOR}
)

target_link_libraries(run_tests PRIVATE
    Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Svg
    OBS::libobs
)

add_test(NAME timeline_tests COMMAND run_tests)
