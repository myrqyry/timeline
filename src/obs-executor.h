#pragma once

#include <QObject>
#include <obs-frontend-api.h>
#include <obs.h>
#include "event-types.h"

class ObsExecutor : public QObject {
    Q_OBJECT

public:
    ObsExecutor(QObject *parent = nullptr);

public slots:
    void executeEvent(const TimelineEvent &evt);

private:
    void handleSceneSwitch(const TimelineEvent &evt);
    void handleMediaStart(const TimelineEvent &evt);
    void handleMediaStop(const TimelineEvent &evt);
    void handleSourceVisibility(const TimelineEvent &evt, bool visible);
    void handleFilterEnable(const TimelineEvent &evt, bool enable);
    void handleHotkey(const TimelineEvent &evt);
};

