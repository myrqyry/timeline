#pragma once

#include <QObject>
#include <QTimer>
#include <chrono>
#include "timeline-model.h"

class TimelineScheduler : public QObject {
    Q_OBJECT

public:
    TimelineScheduler(TimelineModel *model, QObject *parent = nullptr);
    ~TimelineScheduler();

    bool isPlaying() const { return playing; }
    double getCurrentTime() const;

public slots:
    void start();
    void stop();
    void pause();
    void resume();
    void seek(double timeMs);
    
    // End time and looping
    void setEndTime(double endTimeMs);
    double getEndTime() const { return endTimeMs; }
    void setLooping(bool loop);
    bool isLooping() const { return looping; }

signals:
    void tick(double currentTimeMs);
    void eventTriggered(const TimelineEvent &evt);
    void finished();
    void looped();

private slots:
    void onTimerTick();

private:
    TimelineModel *model = nullptr;
    QTimer *timer = nullptr;
    
    // Time tracking
    std::chrono::high_resolution_clock::time_point startTime;
    double currentOffsetMs = 0.0; // Where we are in the timeline relative to 0
    double endTimeMs = 0.0;       // 0 = no end time (infinite)
    
    bool playing = false;
    bool looping = false;
    std::vector<bool> eventFired;
    int timerIntervalMs = 16; // ~60 FPS
};

