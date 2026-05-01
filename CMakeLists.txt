cmake_minimum_required(VERSION 3.16)

project(obs-timeline-plugin VERSION 1.0.0)

option(ENABLE_OBS_WEBSOCKET_VENDOR "Enable obs-websocket vendor integration" ON)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OBS REQUIRED libobs)

# Auto-generate Qt MOC files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

enable_testing()
add_subdirectory(tests)

# Plugin source files
set(PLUGIN_SOURCES
    src/plugin-main.cpp
    src/timeline-dock.cpp
    src/timeline-model.cpp
    src/timeline-scheduler.cpp
    src/obs-executor.cpp
    src/timeline-event-item.cpp
    src/headless-timeline.cpp
    src/event-editor.cpp
    src/timeline-serialization.cpp
)

set(PLUGIN_HEADERS
    src/timeline-dock.h
    src/timeline-model.h
    src/timeline-scheduler.h
    src/obs-executor.h
    src/event-types.h
    src/timeline-event-item.h
    src/theme-helpers.h
    src/headless-timeline.h
    src/timeline-serialization.h
)

# Create library
add_library(${PROJECT_NAME} SHARED 
    ${PLUGIN_SOURCES} 
    ${PLUGIN_HEADERS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    ${OBS_LIBRARIES} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
)

# Include directories
target_include_directories(${PROJECT_NAME} 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OBS_INCLUDE_DIRS}
    /usr/include/obs
)

# Optional obs-websocket vendor integration
if(ENABLE_OBS_WEBSOCKET_VENDOR)
    find_path(OBS_WEBSOCKET_INCLUDE_DIR
        NAMES obs-websocket-api.h
        PATHS
            ${OBS_INCLUDE_DIRS}
            /usr/include/obs
            /usr/local/include/obs
    )

    if(OBS_WEBSOCKET_INCLUDE_DIR)
        target_compile_definitions(${PROJECT_NAME} PRIVATE OBS_TIMELINE_HAS_WEBSOCKET=1)
        target_include_directories(${PROJECT_NAME} PRIVATE ${OBS_WEBSOCKET_INCLUDE_DIR})
        message(STATUS "obs-websocket vendor API enabled")
    else()
        target_compile_definitions(${PROJECT_NAME} PRIVATE OBS_TIMELINE_HAS_WEBSOCKET=0)
        message(WARNING "obs-websocket-api.h not found; websocket vendor integration disabled")
    endif()
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE OBS_TIMELINE_HAS_WEBSOCKET=0)
endif()

set(OBS_TIMELINE_PLUGIN_ROOT
    "$ENV{HOME}/.config/obs-studio/plugins/obs-timeline-plugin"
    CACHE PATH "OBS Timeline plugin install root"
)

set(OBS_PLUGIN_DESTINATION
    "${OBS_TIMELINE_PLUGIN_ROOT}/bin/64bit"
    CACHE PATH "OBS plugin binary destination"
)

set(OBS_DATA_DESTINATION
    "${OBS_TIMELINE_PLUGIN_ROOT}/data"
    CACHE PATH "OBS plugin data destination"
)

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION "${OBS_PLUGIN_DESTINATION}"
    RUNTIME DESTINATION "${OBS_PLUGIN_DESTINATION}"
)

install(DIRECTORY data/
    DESTINATION "${OBS_DATA_DESTINATION}"
)
