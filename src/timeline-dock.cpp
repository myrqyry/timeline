#include "timeline-dock.h"
#include <obs-module.h>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QResizeEvent>

#include <QFile>
#include <QJsonDocument>

#include "timeline-event-item.h"
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QWheelEvent>
#include <cmath>

TimelineDock::TimelineDock(QWidget *parent) : QWidget(parent) {
    blog(LOG_INFO, "TimelineDock constructor started");
    model = new TimelineModel(this);
    blog(LOG_INFO, "TimelineDock: Model created");
    scheduler = new TimelineScheduler(model, this);
    blog(LOG_INFO, "TimelineDock: Scheduler created");
    executor = new ObsExecutor(this);
    blog(LOG_INFO, "TimelineDock: Executor created");
    
    setupUi();
    
    connect(scheduler, &TimelineScheduler::tick, this, &TimelineDock::onTick);
    connect(scheduler, &TimelineScheduler::eventTriggered, executor, &ObsExecutor::executeEvent);
    
    // Register save callback
    obs_frontend_add_save_callback(saveCallback, this);
    
    // Initial Load
    blog(LOG_INFO, "TimelineDock loading initial data");
    load();
    blog(LOG_INFO, "TimelineDock constructor finished");
}

TimelineDock::~TimelineDock() {
    obs_frontend_remove_save_callback(saveCallback, this);
}

void TimelineDock::saveCallback(obs_data_t *save_data, bool saving, void *private_data) {
    TimelineDock *dock = static_cast<TimelineDock*>(private_data);
    if (saving) {
        dock->save();
    }
}

void TimelineDock::save() {
    char *configPath = obs_module_get_config_path(obs_current_module(), "timeline_events.json");
    if (!configPath) return;
    
    QFile file(configPath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(model->toJson());
        file.write(doc.toJson());
        file.close();
        blog(LOG_INFO, "Saved timeline to %s", configPath);
    }
    bfree(configPath);
}

void TimelineDock::load() {
    char *configPath = obs_module_get_config_path(obs_current_module(), "timeline_events.json");
    if (!configPath) return;
    
    QFile file(configPath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        blog(LOG_INFO, "Loading config file: %s", configPath);
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        model->fromJson(doc.array());
        updateLayout(); // Refresh UI with new events
        blog(LOG_INFO, "Loaded timeline from %s", configPath);
    } else {
        blog(LOG_INFO, "No config file found at: %s", configPath);
    }
    bfree(configPath);
}

void TimelineDock::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Controls
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    playButton = new QPushButton("Play", this);
    addEventButton = new QPushButton("Add Event", this);
    timeLabel = new QLabel("00:00:00", this);
    
    connect(playButton, &QPushButton::clicked, this, &TimelineDock::onPlayClicked);
    connect(addEventButton, &QPushButton::clicked, this, &TimelineDock::onAddEventClicked);
    
    controlsLayout->addWidget(playButton);
    controlsLayout->addWidget(addEventButton);
    controlsLayout->addWidget(timeLabel);
    controlsLayout->addStretch();
    
    mainLayout->addLayout(controlsLayout);
    
    // Timeline View
    view = new QGraphicsView(this);
    scene = new QGraphicsScene(this);
    view->setScene(scene);
    view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Initial scene rect - height will be updated in resizeEvent
    scene->setSceneRect(0, 0, 5000, view->height());
    
    // Install event filter for scrubbing handling on viewport
    view->viewport()->installEventFilter(this);

    // Playhead
    playheadItem = scene->addLine(0, 0, 0, view->height(), QPen(Qt::red, 1));
    playheadItem->setZValue(100);

    // Initial layout
    blog(LOG_INFO, "setupUi: calling updateLayout");
    updateLayout();
    
    mainLayout->addWidget(view);
}

void TimelineDock::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (view && scene) {
        QRectF rect = scene->sceneRect();
        rect.setHeight(view->viewport()->height());
        scene->setSceneRect(rect);
        
        // Update playhead height
        if (playheadItem) {
            QLineF line = playheadItem->line();
            line.setP2(QPointF(line.x1(), view->viewport()->height()));
            playheadItem->setLine(line);
        }
    }
}

// Helper to update event items
void TimelineDock::updateLayout() {
    blog(LOG_INFO, "updateLayout: started");
    if (!view || !view->viewport() || !scene) {
        blog(LOG_WARNING, "updateLayout called before UI initialized");
        return;
    }

    // 1. Update scene rect
    scene->setSceneRect(0, 0, std::max(5000.0, 300.0 * pixelsPerSecond), view->viewport()->height());
    
    // ... rest of function ...
    
    // 2. Update all event items
    for (auto item : scene->items()) {
        TimelineEventItem *evtItem = dynamic_cast<TimelineEventItem*>(item);
        if (evtItem) {
            evtItem->setPixelsPerSecond(pixelsPerSecond);
        }
    }
    
    // 3. Redraw ruler
    drawRuler();
    
    // 4. Update playhead
    updatePlayhead(scheduler->getCurrentTime());
}

void TimelineDock::drawRuler() {
    if (!scene) return;

    if (rulerGroup) {
        // destroyItemGroup only removes the group item and deletes it.
        // It does NOT delete children. We must do that manually.
        QList<QGraphicsItem*> items = rulerGroup->childItems();
        scene->destroyItemGroup(rulerGroup);
        for (auto item : items) {
             delete item;
        }
        rulerGroup = nullptr;
    }
    
    rulerGroup = scene->createItemGroup({});
    
    // Draw ticks every second
    double durationSec = scene->width() / pixelsPerSecond;
    int step = 1;
    if (pixelsPerSecond < 50) step = 5;
    if (pixelsPerSecond < 10) step = 10;
    
    for (int i = 0; i < durationSec; i += step) {
        double x = i * pixelsPerSecond;
        QGraphicsLineItem *line = scene->addLine(x, 0, x, 20, QPen(Qt::lightGray));
        rulerGroup->addToGroup(line);
        
        QGraphicsSimpleTextItem *text = scene->addSimpleText(QString::number(i) + "s");
        text->setPos(x + 2, 0);
        text->setBrush(QBrush(Qt::lightGray));
        text->setFont(QFont("Arial", 8));
        rulerGroup->addToGroup(text);
        
        // Minor ticks
        if (pixelsPerSecond > 50) {
           for (int j=1; j<10; j++) {
               double xm = x + (j * pixelsPerSecond / 10.0);
               QGraphicsLineItem *minor = scene->addLine(xm, 10, xm, 20, QPen(Qt::darkGray));
               rulerGroup->addToGroup(minor);
           }
        }
    }
}

bool TimelineDock::eventFilter(QObject *obj, QEvent *event) {
    if (obj == view->viewport()) {
        if (event->type() == QEvent::Wheel) {
             QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
             if (wheelEvent->modifiers() & Qt::ControlModifier) {
                 double steps = wheelEvent->angleDelta().y() / 120.0;
                 double factor = std::pow(1.1, steps);
                 pixelsPerSecond *= factor;
                 if (pixelsPerSecond < 10.0) pixelsPerSecond = 10.0;
                 if (pixelsPerSecond > 500.0) pixelsPerSecond = 500.0;
                 
                 updateLayout();
                 return true;
             }
        }
    
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->buttons() & Qt::LeftButton) {
                // Check if we are clicking on an item (like an event) - if so, don't scrub
                QPoint pos = mouseEvent->pos();
                QPointF scenePos = view->mapToScene(pos);
                
                // If there's an item under cursor that IS moveable, let it handle the event
                QGraphicsItem *item = scene->itemAt(scenePos, QTransform());
                if (item && (item->flags() & QGraphicsItem::ItemIsMovable)) {
                    return false; // Propagate to item
                }
                
                // Otherwise, scrub
                double x = scenePos.x();
                double timeMs = (x / pixelsPerSecond) * 1000.0;
                if (timeMs < 0) timeMs = 0;
                
                // Pause if playing to scrub smoothly
                if (scheduler->isPlaying()) {
                    scheduler->pause();
                    playButton->setText("Play");
                }
                
                scheduler->seek(timeMs);
                return true; // Consumed
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TimelineDock::onPlayClicked() {
    if (scheduler->isPlaying()) {
        scheduler->pause();
        playButton->setText("Play");
    } else {
        scheduler->start();
        playButton->setText("Pause");
    }
}

void TimelineDock::onTick(double currentTimeMs) {
    updatePlayhead(currentTimeMs);
    
    int totalSeconds = static_cast<int>(currentTimeMs / 1000.0);
    int ms = static_cast<int>(currentTimeMs) % 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    
    timeLabel->setText(QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(ms / 10, 2, 10, QChar('0')));
}

void TimelineDock::updatePlayhead(double timeMs) {
    if (!playheadItem || !view) return;
    double x = (timeMs / 1000.0) * pixelsPerSecond;
    double h = view->viewport()->height();
    playheadItem->setLine(x, 0, x, h);
    view->ensureVisible(QRectF(x, 0, 10, h), 50, 0);
}

void TimelineDock::onAddEventClicked() {
    double time = scheduler->getCurrentTime() + 2000.0;
    TimelineEvent evt;
    evt.timeMs = time;
    evt.type = TimelineEventType::SceneSwitch;
    evt.targetId = "Scene 2";
    model->addEvent(evt);
    
    TimelineEventItem *item = new TimelineEventItem(time, pixelsPerSecond);
    scene->addItem(item);
    item->setToolTip(QString("Event: %1").arg(evt.targetId));
    
    // Connect move signal
    connect(item, &TimelineEventItem::eventMoved, [this, evt](double newTime) {
         // Logic to update model would go here. 
         // For now, demo just logs. Ideally Model needs updateEvent(index)
         blog(LOG_INFO, "Event moved to %f", newTime);
    });
}
