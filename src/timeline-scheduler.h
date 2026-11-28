#pragma once

#include <QObject>
#include <QTimer>
#include <QSet>
#include <QMap>
#include <chrono>
#include <vector>
#include "timeline-model.h"

class TimelineScheduler : public QObject {
	Q_OBJECT

public:
	TimelineScheduler(TimelineModel *model, QObject *parent = nullptr);
	~TimelineScheduler();

	bool isPlaying() const { return playing; }
	double getCurrentTime() const;
	double freezeRemainingMs() const;
	bool isFrozen() const { return freezeMs > 0.0; }

public slots:
	void start();
	void stop();
	void restart();
	void pause();
	void seek(double timeMs);

	// End time and looping
	void setEndTime(double endTimeMs);
	double getEndTime() const { return endTimeMs; }
	void setLooping(bool loop);
	bool isLooping() const { return looping; }

signals:
	void tick(double currentTimeMs);
	void eventStarted(const TimelineEvent &evt);
	void eventEnded(const TimelineEvent &evt);
	void parameterProgress(const TimelineEvent &evt, double progress);
	void finished();
	void looped();

private slots:
	void onTimerTick();

private:
	void dispatchDueCues(double timeMs);
	void rebuildEndIndex();
	void beginDueFreeze(double &timeMs);
	void registerParameterCue(const TimelineEvent &evt);
	void rebuildParameterRamps(double timeMs);
	void sampleParameterRamps(double timeMs);
	struct ActiveRamp {
		TimelineEvent event;
		double lastTime = -1.0;
	};
	QMap<QString, ActiveRamp> parameterRamps;
	TimelineModel *model = nullptr;
	QTimer *timer = nullptr;

	// Time tracking
	std::chrono::steady_clock::time_point startTime;
	double freezeMs = 0.0; // remaining wall-clock wait, measured from startTime when playing
	quint64 clockRevision = 0;
	double currentOffsetMs = 0.0; // Where we are in the timeline relative to 0
	double endTimeMs = 0.0;       // 0 = no end time (infinite)

	bool playing = false;
	bool looping = false;
	QSet<QString> eventStartFired;
	QSet<QString> eventEndFired;
	QHash<QString, TimelineEvent> startedSpans;
	QSet<QString> bypassedStarts;
	size_t nextStartIndex = 0;
	std::vector<size_t> endIndices;
	size_t nextEndIndex = 0;
	int timerIntervalMs = 8; // ~125 Hz for tight 32nd-note BPM timing
};
