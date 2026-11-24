#include "macro-automation-recorder.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMaxSamples = 1200;
constexpr qint64 kMaxElapsedMs = 120000;
constexpr qint64 kMaxRampGapMs = 750;
constexpr qint64 kMinCaptureStepMs = 80;
} // namespace

bool MacroAutomationRecorder::start(double baseTimeMs)
{
	if (recording || !std::isfinite(baseTimeMs) || baseTimeMs < 0)
		return false;
	lanes.clear();
	laneIndices.clear();
	totalSamples = 0;
	baseTime = baseTimeMs;
	recording = true;
	return true;
}

bool MacroAutomationRecorder::record(const MacroParameterBinding &binding, double value, qint64 elapsedMs)
{
	if (!recording || elapsedMs < 0 || elapsedMs > kMaxElapsedMs || !std::isfinite(value) ||
	    binding.sourceUuid.isEmpty() || binding.key.isEmpty() || binding.type == MacroParameterType::Text ||
	    (binding.type != MacroParameterType::Float && value != std::round(value)))
		return false;

	const QString key = MacroParameter::bindingIdentity(binding);
	if (key.isEmpty())
		return false;
	size_t index;
	const auto found = laneIndices.constFind(key);
	if (found == laneIndices.constEnd()) {
		if (totalSamples >= kMaxSamples)
			return false;
		index = lanes.size();
		lanes.push_back({binding, {}});
		laneIndices.insert(key, index);
	} else {
		index = found.value();
		if (lanes[index].binding.type != binding.type)
			return false;
	}
	auto &samples = lanes[index].samples;
	if (!samples.empty()) {
		if (elapsedMs < samples.back().timeMs || value == samples.back().value)
			return false;
		if (samples.size() >= 2 && elapsedMs - samples.back().timeMs < kMinCaptureStepMs) {
			samples.back() = {elapsedMs, value};
			return true;
		}
	}
	if (totalSamples >= kMaxSamples)
		return false;
	samples.push_back({elapsedMs, value});
	++totalSamples;
	return true;
}

std::vector<TimelineEvent> MacroAutomationRecorder::finish()
{
	if (!recording)
		return {};
	recording = false;
	std::vector<TimelineEvent> output;
	output.reserve(totalSamples);
	for (const auto &lane : lanes) {
		const auto &samples = lane.samples;
		const bool numeric = lane.binding.type == MacroParameterType::Float ||
				     lane.binding.type == MacroParameterType::Integer;
		for (size_t i = 0; i < samples.size(); ++i) {
			const auto &point = samples[i];
			TimelineEvent cue;
			cue.type = TimelineEventType::SetParameterValue;
			cue.timeMs = baseTime + point.timeMs;
			cue.sourceName = lane.binding.key;
			cue.label = lane.binding.key;
			cue.data = MacroParameter::encodeValue({lane.binding, point.value});
			// The next captured point turns the current one into a linear
			// ramp. A gap starts a new segment, never an implicit slow fade.
			if (numeric && i + 1 < samples.size()) {
				const auto &next = samples[i + 1];
				const auto gap = next.timeMs - point.timeMs;
				if (gap > 0 && gap <= kMaxRampGapMs) {
					cue.hasDuration = true;
					cue.durationMs = gap;
					cue.data = MacroParameter::encodeRamp(
						{{lane.binding, point.value}, next.value, "linear"});
				}
			}
			// A ramp already lands on this point. Don't add an overlapping
			// instantaneous action unless a gap or discrete toggle requires it.
			if (i && numeric && !cue.hasDuration && samples[i].timeMs > samples[i - 1].timeMs &&
			    samples[i].timeMs - samples[i - 1].timeMs <= kMaxRampGapMs)
				continue;
			output.push_back(std::move(cue));
		}
	}
	std::stable_sort(output.begin(), output.end(),
			 [](const auto &a, const auto &b) { return a.timeMs < b.timeMs; });
	cancel();
	return output;
}

void MacroAutomationRecorder::cancel()
{
	recording = false;
	lanes.clear();
	laneIndices.clear();
	totalSamples = 0;
}
