# CMake Linux defaults module

include_guard(GLOBAL)

# Set default installation directories
include(GNUInstallDirs)

if(CMAKE_INSTALL_LIBDIR MATCHES "(CMAKE_SYSTEM_PROCESSOR)")
  string(REPLACE "CMAKE_SYSTEM_PROCESSOR" "${CMAKE_SYSTEM_PROCESSOR}" CMAKE_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
endif()

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

option(TIMELINE_BUILD_OBS_FROM_SOURCE "Build the selected OBS compatibility target from source" OFF)

if(TIMELINE_BUILD_OBS_FROM_SOURCE)
  include(buildspec)
endif()

set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(
  CPACK_PACKAGE_FILE_NAME
  "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-obs${TIMELINE_EXPECTED_OBS_MAJOR}-${CMAKE_C_LIBRARY_ARCHITECTURE}"
)

set(CPACK_GENERATOR "DEB")
if(TIMELINE_BUILD_OBS_FROM_SOURCE)
  # The pinned CI SDK is built but not installed as Debian packages.
  # dpkg-shlibdeps cannot resolve libobs or obs-frontend-api on the runner,
  # even though both are supplied by the user's installed OBS Studio.
  # Require the matching OBS major explicitly instead of dropping the package.
  math(EXPR _timeline_next_obs_major "${TIMELINE_EXPECTED_OBS_MAJOR} + 1")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
  set(
    CPACK_DEBIAN_PACKAGE_DEPENDS
    "obs-studio (>= ${TIMELINE_EXPECTED_OBS_MAJOR}.0), obs-studio (<< ${_timeline_next_obs_major}.0)"
  )
else()
  # Distro-provided OBS libraries have dpkg metadata, so use native scanning.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()
if(PLUGIN_EMAIL)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${PLUGIN_AUTHOR} <${PLUGIN_EMAIL}>")
else()
  # CPack requires a non-empty maintainer; a public email is optional.
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${PLUGIN_AUTHOR}")
endif()
set(CPACK_SET_DESTDIR ON)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25.0 OR NOT CMAKE_CROSSCOMPILING)
  set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)
endif()

set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/release")

set(CPACK_SOURCE_GENERATOR "TXZ")
set(
  CPACK_SOURCE_IGNORE_FILES
  ".*~$"
  \\.git/
  \\.github/
  \\.gitignore
  \\.ccache/
  \\.deps/
  build_.*
  cmake/\\.CMakeBuildNumber
  release/
)

set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-source")
set(CPACK_ARCHIVE_THREADS 0)

include(CPack)

find_package(libobs QUIET)

if(NOT TARGET OBS::libobs)
  find_package(LibObs REQUIRED)
  add_library(OBS::libobs ALIAS libobs)

  find_path(
    obs-frontend-api_INCLUDE_DIR
    NAMES obs-frontend-api.h
    PATHS /usr/include /usr/local/include
    PATH_SUFFIXES obs
  )

  find_library(obs-frontend-api_LIBRARY NAMES obs-frontend-api PATHS /usr/lib /usr/local/lib)

  if(obs-frontend-api_LIBRARY)
    if(NOT TARGET OBS::obs-frontend-api)
      if(IS_ABSOLUTE "${obs-frontend-api_LIBRARY}")
        add_library(OBS::obs-frontend-api UNKNOWN IMPORTED)
        set_property(TARGET OBS::obs-frontend-api PROPERTY IMPORTED_LOCATION "${obs-frontend-api_LIBRARY}")
      else()
        add_library(OBS::obs-frontend-api INTERFACE IMPORTED)
        set_property(TARGET OBS::obs-frontend-api PROPERTY IMPORTED_LIBNAME "${obs-frontend-api_LIBRARY}")
      endif()

      set_target_properties(
        OBS::obs-frontend-api
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${obs-frontend-api_INCLUDE_DIR}"
      )
    endif()
  endif()

  macro(find_package)
    if(NOT "${ARGV0}" STREQUAL libobs AND NOT "${ARGV0}" STREQUAL obs-frontend-api)
      _find_package(${ARGV})
    endif()
  endmacro()
endif()
