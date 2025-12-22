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
    
    static void saveCallback(obs_data_t *save_data, bool saving, void *private_data);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onPlayClicked();
    void onStopClicked();
    void onTick(double currentTimeMs);
    void onAddEventClicked();
    void onEventDoubleClicked(TimelineEventItem *item, int eventIndex);
    void onLoopToggled(bool checked);
    void onEndTimeChanged(double value);
    void onSnapToggled(bool checked);
    void onSaveClicked();
    void onLoadClicked();
    void onTimelineFinished();
    void onTimelineLooped();

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
    void nudgeSelectedEvents(double deltaMs);

    TimelineModel *model;
    TimelineScheduler *scheduler;
    ObsExecutor *executor;
    
    // UI Elements
    QGraphicsView *view;
    QGraphicsScene *scene;
    QPushButton *playButton;
    QPushButton *stopButton;
    QPushButton *addEventButton;
    QCheckBox *loopCheckbox;
    QCheckBox *snapCheckbox;
    QDoubleSpinBox *endTimeSpinbox;
    QLabel *timeLabel;
    
    QGraphicsLineItem *playheadItem;
    QGraphicsLineItem *endMarkerItem = nullptr;
    QList<QGraphicsItem*> rulerItems;
    QList<QGraphicsItem*> gridItems;
    
    // Event tracking
    std::map<double, TimelineEventItem*> eventItemMap;
    
    double pixelsPerSecond = 100.0;
    bool snapEnabled = false;
};
