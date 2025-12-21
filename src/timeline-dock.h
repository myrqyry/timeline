#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include "timeline-model.h"
#include "timeline-scheduler.h"
#include "obs-executor.h"

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

private slots:
    void onPlayClicked();
    void onTick(double currentTimeMs);
    void onAddEventClicked();

private:
    void setupUi();
    void updatePlayhead(double timeMs);

    void updateLayout();
    void drawRuler();

    TimelineModel *model;
    TimelineScheduler *scheduler;
    ObsExecutor *executor;
    
    // UI Elements
    // UI Elements
    QGraphicsView *view;
    QGraphicsScene *scene;
    QPushButton *playButton;
    QPushButton *addEventButton;
    QLabel *timeLabel;
    
    QGraphicsLineItem *playheadItem;
    QList<QGraphicsItem*> rulerItems;
    
    // Event tracking
    std::map<double, TimelineEventItem*> eventItemMap;
    void rebuildEventItems();
    void clearEventItems();
    void updateZoomLabel();

private slots:
    void onEventDoubleClicked(TimelineEventItem *item, int eventIndex);

private:
    double pixelsPerSecond = 100.0;
};
