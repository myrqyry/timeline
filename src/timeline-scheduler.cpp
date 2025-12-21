#include "timeline-scheduler.h"
#include <obs-module.h>

TimelineScheduler::TimelineScheduler(TimelineModel *model, QObject *parent) 
    : QObject(parent), model(model) 
{
    blog(LOG_INFO, "TimelineScheduler created");
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TimelineScheduler::onTimerTick);
    timer->setInterval(timerIntervalMs);
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
        const auto& events = model->getEvents();
        eventFired.resize(events.size());
        for (size_t i = 0; i < events.size(); i++) {
            eventFired[i] = (events[i].timeMs < timeMs);
        }
    }
    emit tick(currentOffsetMs);
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
            if (model) {
                eventFired.assign(model->getEvents().size(), false);
            }
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

    const auto& events = model->getEvents();
    if (eventFired.size() != events.size()) {
        eventFired.resize(events.size(), false);
    }

    // Fire events
    for (size_t i = 0; i < events.size(); i++) {
        if (!eventFired[i] && events[i].timeMs <= currentTime) {
            emit eventTriggered(events[i]);
            eventFired[i] = true;
        }
    }
}

