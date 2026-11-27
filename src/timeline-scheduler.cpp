#include "timeline-scheduler.h"
#include "macro-parameter.h"
#include <obs-module.h>
#include <algorithm>
#include <vector>

TimelineScheduler::TimelineScheduler(TimelineModel *model, QObject *parent) : QObject(parent), model(model)
{
	blog(LOG_INFO, "[Timeline] ├─ Scheduler : created");
	timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &TimelineScheduler::onTimerTick);
	timer->setInterval(timerIntervalMs);

	if (model) {
		rebuildEndIndex();
		connect(model, &TimelineModel::modelChanged, this, [this]() {
			double t = getCurrentTime();
			const auto &evts = this->model->getEvents();
			nextStartIndex = 0;
			QSet<QString> newStartFired;
			QSet<QString> newEndFired;

			for (const auto &evt : evts) {
				if (evt.timeMs < t ||
				    (isFrozen() && evt.timeMs == t && eventStartFired.contains(evt.id))) {
					newStartFired.insert(evt.id);
				}
				if (evt.type != TimelineEventType::FreezeTime && evt.hasDuration &&
				    evt.durationMs > 0) {
					const double endMs = evt.timeMs + evt.durationMs;
					if (endMs < t || (isFrozen() && endMs == t && eventEndFired.contains(evt.id))) {
						newEndFired.insert(evt.id);
					}
				}
			}

			eventStartFired = newStartFired;
			eventEndFired = newEndFired;
			rebuildEndIndex();
			rebuildParameterRamps(t);
		});
	}
}

TimelineScheduler::~TimelineScheduler()
{
	stop();
}

void TimelineScheduler::start()
{
	if (playing)
		return;

	++clockRevision;
	playing = true;
	// Reset start time to now, preserving currentOffsetMs
	startTime = std::chrono::steady_clock::now();
	timer->start();
	blog(LOG_INFO, "[Timeline] Playback started");
}

void TimelineScheduler::stop()
{
	if (!playing)
		return;
	currentOffsetMs = getCurrentTime(); // Preserve position
	freezeMs = freezeRemainingMs();
	nextStartIndex = 0;
	nextEndIndex = 0;
	++clockRevision;
	playing = false;
	timer->stop();
	blog(LOG_INFO, "[Timeline] Playback stopped at %.1f ms", currentOffsetMs);
}

void TimelineScheduler::restart()
{
	playing = false;
	timer->stop();
	seek(0.0);
	blog(LOG_INFO, "[Timeline] Playback restarted");
}

void TimelineScheduler::pause()
{
	if (!playing)
		return;

	// Capture current playback position
	currentOffsetMs = getCurrentTime();
	freezeMs = freezeRemainingMs();
	nextStartIndex = 0;
	nextEndIndex = 0;
	++clockRevision;

	playing = false;
	timer->stop();
	blog(LOG_INFO, "[Timeline] Playback paused at %.1f ms", currentOffsetMs);
}

void TimelineScheduler::seek(double timeMs)
{
	++clockRevision;
	freezeMs = 0.0;
	currentOffsetMs = timeMs;
	if (playing) {
		startTime = std::chrono::steady_clock::now();
	}

	// Reset fired status for events
	if (model) {
		const auto &events = model->getEvents();
		eventStartFired.clear();
		eventEndFired.clear();
		nextStartIndex = 0;
		rebuildEndIndex();
		for (const auto &evt : events) {
			if (evt.timeMs < timeMs) {
				eventStartFired.insert(evt.id);
			}
			// Only duration events have an "end" to track —
			// instant events never emit eventEnded.
			if (evt.type != TimelineEventType::FreezeTime && evt.hasDuration && evt.durationMs > 0) {
				const double endMs = evt.timeMs + evt.durationMs;
				if (endMs < timeMs) {
					eventEndFired.insert(evt.id);
				}
			}
		}
	}
	rebuildParameterRamps(timeMs);
	if (!playing) {
		emit tick(currentOffsetMs);
	}
	blog(LOG_DEBUG, "[Timeline] Seeked to %.1f ms (fired sets reset)", timeMs);
}

double TimelineScheduler::getCurrentTime() const
{
	if (!playing || isFrozen())
		return currentOffsetMs;

	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration<double, std::milli>(now - startTime).count();
	return currentOffsetMs + elapsed;
}

void TimelineScheduler::setEndTime(double endTime)
{
	endTimeMs = endTime;
	blog(LOG_INFO, "[Timeline] End time set: %.1f ms", endTimeMs);
}

void TimelineScheduler::setLooping(bool loop)
{
	looping = loop;
	blog(LOG_INFO, "[Timeline] Looping: %s", looping ? "enabled" : "disabled");
}

double TimelineScheduler::freezeRemainingMs() const
{
	if (!isFrozen() || !playing)
		return freezeMs;
	const auto elapsed =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
	return std::max(0.0, freezeMs - elapsed);
}

void TimelineScheduler::beginDueFreeze(double &timeMs)
{
	if (!model)
		return;
	// Identify the earliest due hold before publishing a tick or advancing
	// the start cursor. Same-time holds add together; all other cues at
	// this timestamp still dispatch in the normal chronological order.
	const auto &events = model->getEvents();
	double wait = 0.0;
	for (size_t i = nextStartIndex; i < events.size() && events[i].timeMs <= timeMs; ++i) {
		const auto &evt = events[i];
		if (eventStartFired.contains(evt.id))
			continue;
		const double duration = freezeDurationMs(evt);
		if (duration <= 0.0)
			continue;
		if (wait == 0.0)
			timeMs = evt.timeMs;
		wait += duration;
	}
	if (wait <= 0.0)
		return;
	currentOffsetMs = timeMs;
	startTime = std::chrono::steady_clock::now();
	freezeMs = wait;
}

void TimelineScheduler::onTimerTick()
{
	if (!playing)
		return;
	const auto revision = clockRevision;
	if (isFrozen()) {
		if (freezeRemainingMs() > 0.0) {
			emit tick(currentOffsetMs);
			return;
		}
		// Advance the wall-clock origin by precisely the hold duration.
		// A late wake-up after the wait retains the post-hold elapsed time.
		startTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double, std::milli>(freezeMs));
		freezeMs = 0.0;
	}
	double currentTime = getCurrentTime();
	if (endTimeMs > 0)
		currentTime = std::min(currentTime, endTimeMs);
	beginDueFreeze(currentTime);
	// A looping boundary publishes only the next cycle's zero tick,
	// unless this cycle must visibly hold at the boundary first.
	if (isFrozen() || !looping || endTimeMs <= 0 || currentTime < endTimeMs)
		emit tick(currentTime);
	if (!playing || clockRevision != revision)
		return;
	dispatchDueCues(currentTime);
	if (!playing || clockRevision != revision || isFrozen())
		return;
	if (endTimeMs > 0 && currentTime >= endTimeMs) {
		if (!looping) {
			playing = false;
			timer->stop();
			currentOffsetMs = endTimeMs;
			parameterRamps.clear();
			emit finished();
			blog(LOG_INFO, "[Timeline] End time reached - playback finished");
			return;
		}
		currentOffsetMs = 0.0;
		startTime = std::chrono::steady_clock::now();
		eventStartFired.clear();
		eventEndFired.clear();
		nextStartIndex = 0;
		nextEndIndex = 0;
		parameterRamps.clear();
		double start = 0.0;
		beginDueFreeze(start);
		emit tick(start);
		if (!playing || clockRevision != revision)
			return;
		emit looped();
		if (!playing || clockRevision != revision)
			return;
		dispatchDueCues(start);
		blog(LOG_DEBUG, "[Timeline] Looped back to start");
	}
}

void TimelineScheduler::registerParameterCue(const TimelineEvent &evt)
{
	if (evt.type != TimelineEventType::SetParameterValue)
		return;
	if (const auto step = MacroParameter::decodeValue(evt.data.toString())) {
		parameterRamps.remove(MacroParameter::bindingIdentity(step->binding));
	} else if (const auto ramp = MacroParameter::decodeRamp(evt.data.toString());
		   ramp && evt.hasDuration && std::isfinite(evt.durationMs) && evt.durationMs > 0) {
		parameterRamps.insert(MacroParameter::bindingIdentity(ramp->start.binding), {evt, evt.timeMs});
	}
}

void TimelineScheduler::rebuildParameterRamps(double timeMs)
{
	parameterRamps.clear();
	if (!model)
		return;
	for (const auto &evt : model->getEvents()) {
		if (evt.timeMs > timeMs)
			break;
		if (eventStartFired.contains(evt.id))
			registerParameterCue(evt);
	}
	// Do not replay completed animations on a seek.
	for (auto it = parameterRamps.begin(); it != parameterRamps.end();) {
		if (it->event.timeMs + it->event.durationMs < timeMs)
			it = parameterRamps.erase(it);
		else
			++it;
	}
}

void TimelineScheduler::sampleParameterRamps(double timeMs)
{
	const auto revision = clockRevision;
	// Snapshot keys; callbacks can seek/stop or edit the model.
	const auto keys = parameterRamps.keys();
	for (const auto &key : keys) {
		if (!playing || clockRevision != revision)
			return;
		auto it = parameterRamps.find(key);
		if (it == parameterRamps.end() || timeMs < it->event.timeMs)
			continue;
		const auto evt = it->event;
		const double sample = std::min(timeMs, evt.timeMs + evt.durationMs);
		if (it->lastTime == sample)
			continue;
		it->lastTime = sample;
		const double progress = sample >= evt.timeMs + evt.durationMs ? 1.0
									      : (sample - evt.timeMs) / evt.durationMs;
		if (progress >= 1.0)
			parameterRamps.erase(it);
		emit parameterProgress(evt, progress);
	}
}

void TimelineScheduler::rebuildEndIndex()
{
	endIndices.clear();
	nextEndIndex = 0;
	if (!model)
		return;
	const auto &events = model->getEvents();
	for (size_t i = 0; i < events.size(); ++i) {
		const auto &evt = events[i];
		if (evt.type != TimelineEventType::FreezeTime && evt.hasDuration && evt.durationMs > 0.0)
			endIndices.push_back(i);
	}
	std::stable_sort(endIndices.begin(), endIndices.end(), [&](size_t a, size_t b) {
		return events[a].timeMs + events[a].durationMs < events[b].timeMs + events[b].durationMs;
	});
}

void TimelineScheduler::dispatchDueCues(double timeMs)
{
	if (!model)
		return;

	struct Cue {
		double time;
		bool isEnd;
		TimelineEvent event;
	};
	std::vector<Cue> due;
	const auto &events = model->getEvents();

	// Retain the sorted start cursor. Only snapshot cues actually due, so
	// signal handlers never hold references into the mutable model vector.
	while (nextStartIndex < events.size() && events[nextStartIndex].timeMs <= timeMs) {
		const auto &evt = events[nextStartIndex++];
		if (!eventStartFired.contains(evt.id))
			due.push_back({evt.timeMs, false, evt});
	}
	// The end index is rebuilt only on model mutation, not on every timer tick.
	while (nextEndIndex < endIndices.size()) {
		const auto &evt = events[endIndices[nextEndIndex]];
		const double end = evt.timeMs + evt.durationMs;
		if (end > timeMs)
			break;
		++nextEndIndex;
		if (!eventEndFired.contains(evt.id))
			due.push_back({end, true, evt});
	}

	// A late tick can cover several distinct cue times. Execute them in
	// chronological order; at a shared boundary, expire old spans before
	// starting new cues so the new action wins.
	std::stable_sort(due.begin(), due.end(), [](const Cue &a, const Cue &b) {
		if (a.time != b.time)
			return a.time < b.time;
		return a.isEnd && !b.isEnd;
	});
	const auto revision = clockRevision;
	for (const auto &cue : due) {
		if (!playing || clockRevision != revision)
			break;
		auto &fired = cue.isEnd ? eventEndFired : eventStartFired;
		if (fired.contains(cue.event.id))
			continue;
		sampleParameterRamps(cue.time);
		if (!playing || clockRevision != revision)
			return;
		fired.insert(cue.event.id);
		if (!cue.isEnd)
			registerParameterCue(cue.event);
		if (cue.isEnd)
			emit eventEnded(cue.event);
		else
			emit eventStarted(cue.event);
	}
	if (playing && clockRevision == revision)
		sampleParameterRamps(timeMs);
}
