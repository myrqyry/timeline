cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# OBS expects plugin .so without 'lib' prefix
set(CMAKE_SHARED_LIBRARY_PREFIX "")

option(ENABLE_OBS_WEBSOCKET_VENDOR "Enable obs-websocket vendor integration" ON)
option(TIMELINE_BUILD_OBS_FROM_SOURCE "Build the selected OBS compatibility target from source" OFF)

if(TIMELINE_OBS_COMPATIBILITY STREQUAL "obs31")
  set(TIMELINE_EXPECTED_OBS_MAJOR 31)
else()
  set(TIMELINE_EXPECTED_OBS_MAJOR 32)
endif()

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Match the official OBS plugin template's platform setup and imported targets.
include(compilerconfig)
include(defaults)
include(helpers)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

# Auto-generate Qt MOC files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(TIMELINE_BUILD_TESTS "Build Qt Test regression suite" ON)

if(TIMELINE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Plugin source files
set(
  PLUGIN_SOURCES
  src/plugin-main.cpp
  src/timeline-dock.cpp
  src/timeline-model.cpp
  src/timeline-scheduler.cpp
  src/obs-executor.cpp
  src/timeline-event-item.cpp
  src/headless-timeline.cpp
  src/event-editor.cpp
  src/timeline-serialization.cpp
  src/timecode-display.cpp
  src/macro-button-model.cpp
  src/macro-button-widget.cpp
  src/macro-serialization.cpp
  src/macro-strip-dock.cpp
)

set(
  PLUGIN_HEADERS
  src/timeline-dock.h
  src/timeline-model.h
  src/timeline-scheduler.h
  src/obs-executor.h
  src/obs-compatibility.h
  src/event-types.h
  src/timeline-event-item.h
  src/theme-helpers.h
  src/headless-timeline.h
  src/timeline-serialization.h
  src/timecode-display.h
  src/macro-button.h
  src/macro-button-model.h
  src/macro-button-widget.h
  src/macro-serialization.h
  src/macro-strip-dock.h
)

# Create library
add_library(${PROJECT_NAME} MODULE ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

# Link libraries
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(${PROJECT_NAME} PRIVATE TIMELINE_EXPECTED_OBS_MAJOR=${TIMELINE_EXPECTED_OBS_MAJOR})

# Optional obs-websocket vendor integration
if(ENABLE_OBS_WEBSOCKET_VENDOR)
  find_path(
    OBS_WEBSOCKET_INCLUDE_DIR
    NAMES obs-websocket-api.h
    PATHS ${OBS_INCLUDE_DIRS} /usr/include/obs /usr/local/include/obs
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

if(APPLE)
  # macOS requires a .plugin bundle with its data inside Resources.
  set_target_properties_plugin(${PROJECT_NAME})
else()
  if(WIN32)
    set(_timeline_user_plugin_root "$ENV{APPDATA}/obs-studio/plugins/timeline")
  else()
    set(_timeline_user_plugin_root "$ENV{HOME}/.config/obs-studio/plugins/timeline")
  endif()

  set(TIMELINE_PLUGIN_ROOT "${_timeline_user_plugin_root}" CACHE PATH "Timeline for OBS plugin install root")

  # STRING permits relative CI staging paths without making them absolute.
  set(OBS_PLUGIN_DESTINATION "${TIMELINE_PLUGIN_ROOT}/bin/64bit" CACHE STRING "OBS plugin binary destination")

  set(OBS_DATA_DESTINATION "${TIMELINE_PLUGIN_ROOT}/data" CACHE STRING "OBS plugin data destination")

  install(
    TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION "${OBS_PLUGIN_DESTINATION}"
    RUNTIME DESTINATION "${OBS_PLUGIN_DESTINATION}"
  )

  # Agent guidance belongs in source control, not in users' OBS plugin data.
  install(DIRECTORY data/ DESTINATION "${OBS_DATA_DESTINATION}" PATTERN "AGENTS.md" EXCLUDE)
endif()
