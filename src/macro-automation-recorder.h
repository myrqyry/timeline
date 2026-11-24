#pragma once

#include "event-types.h"
#include "macro-parameter.h"

#include <QHash>
#include <QString>
#include <vector>

// A session-only, side-effect-free capture buffer. OBS settings are updated by
// the existing Macro Strip; the recorder only receives successful writes.
// Captured points are converted to editable parameter cues in one model edit.
class MacroAutomationRecorder {
public:
	bool start(double baseTimeMs);
	bool active() const { return recording; }
	bool record(const MacroParameterBinding &binding, double value, qint64 elapsedMs);
	std::vector<TimelineEvent> finish();
	void cancel();
	int sampleCount() const { return totalSamples; }

private:
	struct Sample {
		qint64 timeMs;
		double value;
	};
	struct Lane {
		MacroParameterBinding binding;
		std::vector<Sample> samples;
	};
	std::vector<Lane> lanes;
	QHash<QString, size_t> laneIndices;
	double baseTime = 0.0;
	int totalSamples = 0;
	bool recording = false;
};
