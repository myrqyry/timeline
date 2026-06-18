#pragma once

#include <cmath>

namespace TimelineHitTesting {

constexpr double kMarkerGrabRadiusPx = 30.0;
constexpr int kMarkerHandleSizePx = static_cast<int>(kMarkerGrabRadiusPx * 2.0);

inline bool isOnTimelineMarker(double sceneX, double markerX)
{
	return std::abs(sceneX - markerX) <= kMarkerGrabRadiusPx;
}

inline bool isOnActiveTimelineMarker(double sceneX, double markerX, bool markerActive)
{
	return markerActive && isOnTimelineMarker(sceneX, markerX);
}

}
