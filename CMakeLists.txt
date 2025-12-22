cmake_minimum_required(VERSION 3.16)

project(obs-timeline-plugin VERSION 1.0.0)

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

# Plugin source files
set(PLUGIN_SOURCES
    src/plugin-main.cpp
    src/timeline-dock.cpp
    src/timeline-model.cpp
    src/timeline-scheduler.cpp
    src/obs-executor.cpp
    src/timeline-event-item.cpp
)

set(PLUGIN_HEADERS
    src/timeline-dock.h
    src/timeline-model.h
    src/timeline-scheduler.h
    src/obs-executor.h
    src/event-types.h
    src/timeline-event-item.h
    src/theme-helpers.h
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

# Installation
if(LINUX)
    install(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION ~/.config/obs-studio/plugins/obs-timeline-plugin/bin/64bit/)
endif()

if(WIN32)
    install(TARGETS ${PROJECT_NAME} 
            RUNTIME DESTINATION "C:/Program Files/obs-studio/obs-plugins/64bit/")
endif()

# Install data (locale, themes, effects)
if(NOT DEFINED OBS_DATA_DESTINATION)
    install(DIRECTORY data/ DESTINATION $ENV{HOME}/.config/obs-studio/plugins/obs-timeline-plugin/data)
else()
    install(DIRECTORY data/ DESTINATION ${OBS_DATA_DESTINATION})
endif()
