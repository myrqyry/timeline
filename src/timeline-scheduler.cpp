#include "timeline-scheduler.h"
#include <obs-module.h>

TimelineScheduler::TimelineScheduler(TimelineModel *model, QObject *parent)
    : QObject(parent), model(model)
{
    blog(LOG_INFO, "TimelineScheduler created");
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TimelineScheduler::onTimerTick);
    timer->setInterval(timerIntervalMs);

    if (model) {
        connect(model, &TimelineModel::modelChanged, this, [this]() {
            double t = getCurrentTime();
            const auto &evts = this->model->getEvents();
            QSet<QString> newStartFired;
            QSet<QString> newEndFired;

            for (const auto &evt : evts) {
                // Preserve fired status for existing events; mark new ones
                // as fired only if their time is already behind the playhead.
                if (eventStartFired.contains(evt.id) || evt.timeMs < t) {
                    newStartFired.insert(evt.id);
                }
                if (evt.hasDuration && evt.durationMs > 0) {
                    const double endMs = evt.timeMs + evt.durationMs;
                    if (eventEndFired.contains(evt.id) || endMs < t) {
                        newEndFired.insert(evt.id);
                    }
                } else {
                    if (eventEndFired.contains(evt.id)) {
                        newEndFired.insert(evt.id);
                    }
                }
            }

            eventStartFired = newStartFired;
            eventEndFired = newEndFired;
        });
    }
}

TimelineScheduler::~TimelineScheduler() {
    stop();
}

void TimelineScheduler::start() {
    if (playing) return;
    
    playing = true;
    // Reset start time to now, preserving currentOffsetMs
    startTime = std::chrono::high_resolution_clock::now();
    timer->start();
}

void TimelineScheduler::stop() {
    playing = false;
    timer->stop();
    currentOffsetMs = 0; // Reset to beginning
    seek(0);
}

void TimelineScheduler::pause() {
    if (!playing) return;
    
    // Capture current playback position
    currentOffsetMs = getCurrentTime();
    
    playing = false;
    timer->stop();
}

void TimelineScheduler::resume() {
    // start() will continue from currentOffsetMs
    start();
}

void TimelineScheduler::seek(double timeMs) {
    currentOffsetMs = timeMs;
    startTime = std::chrono::high_resolution_clock::now();

    // Reset fired status for events
    if (model) {
        const auto &events = model->getEvents();
        eventStartFired.clear();
        eventEndFired.clear();
        for (const auto &evt : events) {
            if (evt.timeMs <= timeMs) {
                eventStartFired.insert(evt.id);
            }
            if (evt.hasDuration && evt.durationMs > 0) {
                const double endMs = evt.timeMs + evt.durationMs;
                if (endMs < timeMs) {
                    eventEndFired.insert(evt.id);
                }
            } else {
                eventEndFired.insert(evt.id);
            }
        }
    }
    if (!playing) {
        emit tick(currentOffsetMs);
    }
}

double TimelineScheduler::getCurrentTime() const {
    if (!playing) return currentOffsetMs;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - startTime).count();
    return currentOffsetMs + elapsed;
}

void TimelineScheduler::setEndTime(double endTime) {
    endTimeMs = endTime;
    blog(LOG_INFO, "Timeline end time set to %.1f ms", endTimeMs);
}

void TimelineScheduler::setLooping(bool loop) {
    looping = loop;
    blog(LOG_INFO, "Timeline looping %s", looping ? "enabled" : "disabled");
}

void TimelineScheduler::onTimerTick() {
    double currentTime = getCurrentTime();

    // Check if we've reached the end time
    if (endTimeMs > 0 && currentTime >= endTimeMs) {
        if (looping) {
            // Loop back to start
            seek(0);
            // Reset all event fired states
            eventStartFired.clear();
            eventEndFired.clear();
            emit looped();
            blog(LOG_INFO, "Timeline looped back to start");
            return;
        } else {
            // Stop at end
            pause();
            emit finished();
            blog(LOG_INFO, "Timeline reached end time");
            return;
        }
    }

    emit tick(currentTime);

    if (!model) return;

    // Reference to avoid O(n) copy at ~60fps
    const auto &events = model->getEvents();

    // Fire events
    for (const TimelineEvent &evt : events) {

        if (!eventStartFired.contains(evt.id) && evt.timeMs <= currentTime) {
            emit eventStarted(evt);
            eventStartFired.insert(evt.id);
        }

        if (evt.hasDuration && evt.durationMs > 0) {
            const double endMs = evt.timeMs + evt.durationMs;
            if (!eventEndFired.contains(evt.id) && endMs <= currentTime) {
                emit eventEnded(evt);
                eventEndFired.insert(evt.id);
            }
        }
    }
}
