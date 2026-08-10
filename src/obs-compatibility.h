#pragma once

#include <obs-config.h>

#ifndef TIMELINE_EXPECTED_OBS_MAJOR
#error "TIMELINE_EXPECTED_OBS_MAJOR must be set by CMake"
#endif

#if LIBOBS_API_MAJOR_VER != TIMELINE_EXPECTED_OBS_MAJOR
#error "The selected OBS compatibility target does not match the OBS headers"
#endif
