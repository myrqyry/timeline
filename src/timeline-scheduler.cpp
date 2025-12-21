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
    startTime = std::chrono::high_resolution_clock::now();
    
    // Reset event fired status if starting from 0 (implied by seek(0) before start usually)
    // But if just resuming, we need to handle that. 
    // Implementing simpler start/resume logic:
    // start() resumes from currentOffsetMs
    
    timer->start();
}

void TimelineScheduler::stop() {
    playing = false;
    timer->stop();
    seek(0);
}

void TimelineScheduler::pause() {
    if (!playing) return;
    
    // Calculate elapsed time and add to offset
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - startTime).count();
    currentOffsetMs += elapsed;
    
    playing = false;
    timer->stop();
}

void TimelineScheduler::resume() {
    start();
}

void TimelineScheduler::seek(double timeMs) {
    currentOffsetMs = timeMs;
    // Reset fired status for events after this time
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

void TimelineScheduler::onTimerTick() {
    double currentTime = getCurrentTime();
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
