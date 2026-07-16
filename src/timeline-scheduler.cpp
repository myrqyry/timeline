#include "timeline-scheduler.h"
#include <obs-module.h>

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
	startTime = std::chrono::high_resolution_clock::now();
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

void TimelineScheduler::resume()
{
	// start() will continue from currentOffsetMs
	start();
	blog(LOG_INFO, "[Timeline] Playback resumed");
}

void TimelineScheduler::seek(double timeMs)
{
	currentOffsetMs = timeMs;
	if (playing) {
		startTime = std::chrono::high_resolution_clock::now();
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

	auto now = std::chrono::high_resolution_clock::now();
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
	double currentTime = getCurrentTime();
	bool justLooped = false;

	// Check if we've reached the end time
	if (endTimeMs > 0 && currentTime >= endTimeMs) {
		if (looping) {
			// Reset state directly — avoid seek(0) which emits tick() re-entrantly
			currentOffsetMs = 0.0;
			startTime = std::chrono::high_resolution_clock::now();
			eventStartFired.clear();
			eventEndFired.clear();
			nextStartIndex = 0;
			currentTime = 0.0;
			justLooped = true;
			blog(LOG_DEBUG, "[Timeline] Looped back to start");
		} else {
			// Stop at end
			pause();
			emit finished();
			blog(LOG_INFO, "[Timeline] End time reached - playback finished");
			return;
		}
	}

	emit tick(currentTime);

	// Emit looped after tick so UI sees t=0 before the loop signal
	if (justLooped)
		emit looped();

	if (!model)
		return;

	// Reference to avoid an O(n) copy at the 125 Hz timer frequency.
	const auto &events = model->getEvents();

	// Fire starts through the sorted cursor so future events are not inspected
	// on every timer tick.
	while (nextStartIndex < events.size() && events[nextStartIndex].timeMs <= currentTime) {
		const TimelineEvent &evt = events[nextStartIndex++];
		if (!eventStartFired.contains(evt.id)) {
			emit eventStarted(evt);
			eventStartFired.insert(evt.id);
		}
	}

	// End times are not sorted by start time, so retain a separate scan.
	for (const TimelineEvent &evt : events) {
		if (!evt.hasDuration || evt.durationMs <= 0.0)
			continue;
		const double endMs = evt.timeMs + evt.durationMs;
		if (!eventEndFired.contains(evt.id) && endMs <= currentTime) {
			emit eventEnded(evt);
			eventEndFired.insert(evt.id);
		}
	}
}
