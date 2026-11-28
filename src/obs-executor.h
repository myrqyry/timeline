#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <obs-frontend-api.h>
#include <obs.h>
#include "obs-compatibility.h"
#include "event-types.h"
#include <vector>

class HeadlessTimeline;

class ObsExecutor : public QObject {
	Q_OBJECT

public:
	ObsExecutor(QObject *parent = nullptr);
	~ObsExecutor();
	void setObsWebSocketVendor(void *vendor);
	static bool hasMissingTarget(const TimelineEvent &evt);
	void setAncestorPaths(const QStringList &paths) { timelineAncestry = paths; }

public slots:
	void executeEventStart(const TimelineEvent &evt);
	void executeEventEnd(const TimelineEvent &evt);
	void executeParameterProgress(const TimelineEvent &evt, double progress);
	void cancelAll();

private:
	void handleSceneSwitch(const TimelineEvent &evt);
	void handleMediaAction(const TimelineEvent &evt, bool start);
	void handleSourceVisibility(const TimelineEvent &evt, bool visible);
	void handleFilterEnable(const TimelineEvent &evt, bool enable);
	void handleHotkey(const TimelineEvent &evt);
	void handleDownstreamKeyerScene(const TimelineEvent &evt);
	void handleTriggerTimeline(const TimelineEvent &evt);
	void performAction(const TimelineEvent &evt, bool isStartAction);
	void emitCueTelemetry(const TimelineEvent &evt, const char *phase);
	void restoreOriginalTransition();
	void cancelRestoreTimer();

	std::vector<HeadlessTimeline *> activeTimelines;
	QStringList timelineAncestry;
	obs_source_t *originalTransition = nullptr;
	obs_source_t *currentTemporaryTransition = nullptr;
	QTimer *restoreTimer = nullptr;
	void *obsWebSocketVendor = nullptr;
};
