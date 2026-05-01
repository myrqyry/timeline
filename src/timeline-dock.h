#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <map>
#include "timeline-model.h"
#include "timeline-scheduler.h"
#include "obs-executor.h"

class TimelineEventItem;
class QGraphicsLineItem;

class TimelineDock : public QWidget {
    Q_OBJECT

public:
    TimelineDock(QWidget *parent = nullptr);
    ~TimelineDock();

    // Persistence
    void save();
    void load();
    
    void setObsWebSocketVendor(void* vendor);
    
    static void saveCallback(obs_data_t *save_data, bool saving, void *private_data);

    // Hotkey callbacks
    static void hotkeyPlayPause(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
    static void hotkeyStop(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
    static void hotkeyRestart(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
    static void frontendEvent(enum obs_frontend_event event, void *private_data);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onPlayClicked();
    void onStopClicked();
    void onTick(double currentTimeMs);
    void onAddEventClicked();
    void onEventDoubleClicked(TimelineEventItem *item, const QString &eventId);
    void onLoopToggled(bool checked);
    void onEndTimeChanged(double value);
    void onSnapToggled(bool checked);
    void onSaveClicked();
    void onLoadClicked();
    void onTimelineFinished();
    void onTimelineLooped();
    void onRecordClicked();
    void onBpmGridToggled(bool checked);
    void onBpmChanged(double value);
    void onCenterControlsToggled(bool checked);

private:
    void setupUi();
    void updatePlayhead(double timeMs);
    void updateLayout();
    void drawRuler();
    void drawGrid();
    void clearRuler();
    void rebuildEventItems();
    void clearEventItems();
    void updateZoomLabel();
    void updateEndMarker();
    void deleteSelectedEvents();
    void copySelectedEvents();
    void pasteEvents();
    void nudgeSelectedEvents(double deltaMs);
    void updateControlsLayout();

    QList<TimelineEvent> clipboard;

    TimelineModel *model;
    TimelineScheduler *scheduler;
    ObsExecutor *executor;
    
    // UI Elements
    QGraphicsView *view;
    QGraphicsScene *scene;

    // Layout
    QHBoxLayout *controlsLayout;

    // Buttons & Controls
    QPushButton *playButton;
    QPushButton *stopButton;
    QPushButton *recordButton;
    QPushButton *addEventButton;
    QPushButton *saveButton;
    QPushButton *loadButton;

    // Options
    QCheckBox *loopCheckbox;
    QCheckBox *snapCheckbox;
    QCheckBox *centerControlsCheckbox;
    QCheckBox *bpmGridCheckbox;
    QDoubleSpinBox *endTimeSpinbox;
    QDoubleSpinBox *bpmSpinbox;
    QLabel *timeLabel;
    QLabel *endLabel;

    // Settings
    bool centerControls = false;
    double bpm = 120.0;
    bool gridToBpm = false;
    
    QGraphicsLineItem *playheadItem;
    
    QGraphicsLineItem *endMarkerItem = nullptr;
    QGraphicsLineItem *dragGuideItem = nullptr;

    QList<QGraphicsItem*> rulerItems;
    QList<QGraphicsItem*> gridItems;
    
    // Event tracking
    QList<TimelineEventItem *> eventItems;
    
    double pixelsPerSecond = 100.0;
    bool snapEnabled = false;
    
    // Hotkeys
    obs_hotkey_id hotkeyIdPlayPause = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hotkeyIdStop = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hotkeyIdRestart = OBS_INVALID_HOTKEY_ID;

    void* obsWebSocketVendor = nullptr;
    qint64 lastTickTime = 0;
    bool rebuildScheduled = false;
};
