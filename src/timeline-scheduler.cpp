#include "timeline-scheduler.h"
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
		connect(model, &TimelineModel::modelChanged, this, [this]() {
			double t = getCurrentTime();
			const auto &evts = this->model->getEvents();
			nextStartIndex = 0;
			QSet<QString> newStartFired;
			QSet<QString> newEndFired;

			for (const auto &evt : evts) {
				if (evt.timeMs < t) {
					newStartFired.insert(evt.id);
				}
				if (evt.hasDuration && evt.durationMs > 0) {
					const double endMs = evt.timeMs + evt.durationMs;
					if (endMs < t) {
						newEndFired.insert(evt.id);
					}
				}
			}

			eventStartFired = newStartFired;
			eventEndFired = newEndFired;
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

	playing = false;
	timer->stop();
	blog(LOG_INFO, "[Timeline] Playback paused at %.1f ms", currentOffsetMs);
}

void TimelineScheduler::seek(double timeMs)
{
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
		for (const auto &evt : events) {
			if (evt.timeMs < timeMs) {
				eventStartFired.insert(evt.id);
			}
			// Only duration events have an "end" to track —
			// instant events never emit eventEnded.
			if (evt.hasDuration && evt.durationMs > 0) {
				const double endMs = evt.timeMs + evt.durationMs;
				if (endMs < timeMs) {
					eventEndFired.insert(evt.id);
				}
			}
		}
	}
	if (!playing) {
		emit tick(currentOffsetMs);
	}
	blog(LOG_DEBUG, "[Timeline] Seeked to %.1f ms (fired sets reset)", timeMs);
}

double TimelineScheduler::getCurrentTime() const
{
	if (!playing)
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

void TimelineScheduler::onTimerTick()
{
	const double currentTime = getCurrentTime();
	if (endTimeMs > 0 && currentTime >= endTimeMs) {
		const double boundary = endTimeMs;
		// Drain the outgoing cycle, including cues exactly on the end marker.
		// A late Qt timer tick must not drop these or run cues past the marker.
		dispatchDueCues(boundary);
		if (!looping) {
			playing = false;
			timer->stop();
			currentOffsetMs = boundary;
			emit tick(boundary);
			emit finished();
			blog(LOG_INFO, "[Timeline] End time reached - playback finished");
			return;
		}

		currentOffsetMs = 0.0;
		startTime = std::chrono::steady_clock::now();
		eventStartFired.clear();
		eventEndFired.clear();
		nextStartIndex = 0;
		emit tick(0.0);
		emit looped();
		dispatchDueCues(0.0);
		blog(LOG_DEBUG, "[Timeline] Looped back to start");
		return;
	}

	emit tick(currentTime);
	dispatchDueCues(currentTime);
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
	for (const auto &evt : events) {
		if (!evt.hasDuration || evt.durationMs <= 0.0)
			continue;
		const double end = evt.timeMs + evt.durationMs;
		if (end <= timeMs && !eventEndFired.contains(evt.id))
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
	for (const auto &cue : due) {
		auto &fired = cue.isEnd ? eventEndFired : eventStartFired;
		if (fired.contains(cue.event.id))
			continue;
		fired.insert(cue.event.id);
		if (cue.isEnd)
			emit eventEnded(cue.event);
		else
			emit eventStarted(cue.event);
	}
}
