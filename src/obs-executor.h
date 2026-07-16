#pragma once

#include <QObject>
#include <QPointer>
#include <obs-frontend-api.h>
#include <obs.h>
#include "event-types.h"
#include <vector>

class HeadlessTimeline;

class ObsExecutor : public QObject {
	Q_OBJECT

public:
	ObsExecutor(QObject *parent = nullptr);
	~ObsExecutor();
	void setObsWebSocketVendor(void *vendor);

public slots:
	void executeEventStart(const TimelineEvent &evt);
	void executeEventEnd(const TimelineEvent &evt);
	void cancelAll();

private:
	struct PendingTransition {
		obs_source_t *oldTransition = nullptr;
		obs_source_t *transition = nullptr;
	};

	void handleSceneSwitch(const TimelineEvent &evt);
	void handleMediaStart(const TimelineEvent &evt);
	void handleMediaStop(const TimelineEvent &evt);
	void handleSourceVisibility(const TimelineEvent &evt, bool visible);
	void handleFilterEnable(const TimelineEvent &evt, bool enable);
	void handleHotkey(const TimelineEvent &evt);
	void handleTriggerTimeline(const TimelineEvent &evt);
	void performAction(const TimelineEvent &evt, bool isStartAction);
	void releasePendingTransitions();

	std::vector<HeadlessTimeline *> activeTimelines;
	std::vector<PendingTransition> pendingTransitions;
	void *obsWebSocketVendor = nullptr;
};
