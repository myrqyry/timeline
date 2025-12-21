#include "timeline-dock.h"
#include <obs-module.h>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QResizeEvent>
#include <obs-frontend-api.h>

#include <QFile>
#include <QJsonDocument>

#include "timeline-event-item.h"
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QWheelEvent>
#include <cmath>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>

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
    clearRuler();
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
    
    // 2. Rebuild event items from model
    rebuildEventItems();
    
    // 3. Update all existing event items (setPixelsPerSecond recalculates position)
    for (auto item : scene->items()) {
        TimelineEventItem *evtItem = dynamic_cast<TimelineEventItem*>(item);
        if (evtItem) {
            evtItem->setPixelsPerSecond(pixelsPerSecond);
        }
    }
    
    // 4. Redraw ruler
    drawRuler();
    
    // 5. Update playhead
    updatePlayhead(scheduler->getCurrentTime());
}

void TimelineDock::clearRuler() {
    for (QGraphicsItem* item : rulerItems) {
        scene->removeItem(item);
        delete item;
    }
    rulerItems.clear();
}

void TimelineDock::drawRuler() {
    if (!scene) return;
    
    clearRuler(); // Clean up old ruler
    
    // Draw ticks every second
    double durationSec = scene->width() / pixelsPerSecond;
    int step = 1;
    if (pixelsPerSecond < 50) step = 5;
    if (pixelsPerSecond < 10) step = 10;
    
    for (int i = 0; i <= durationSec; i += step) {
        double x = i * pixelsPerSecond;
        QGraphicsLineItem *line = scene->addLine(x, 0, x, 20, QPen(Qt::lightGray));
        rulerItems.append(line);
        
        QGraphicsSimpleTextItem *text = scene->addSimpleText(QString::number(i) + "s");
        text->setPos(x + 2, 0);
        text->setBrush(QBrush(Qt::lightGray));
        text->setFont(QFont("Arial", 8));
        rulerItems.append(text);
        
        // Minor ticks
        if (pixelsPerSecond > 50) {
           for (int j=1; j<10; j++) {
               double xm = x + (j * pixelsPerSecond / 10.0);
               QGraphicsLineItem *minor = scene->addLine(xm, 10, xm, 20, QPen(Qt::darkGray));
               rulerItems.append(minor);
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
                 double factor = std::pow(1.15, steps); // Slightly faster zoom
                 pixelsPerSecond *= factor;
                 
                 // Expanded zoom range
                 if (pixelsPerSecond < 5.0) pixelsPerSecond = 5.0;
                 if (pixelsPerSecond > 1000.0) pixelsPerSecond = 1000.0;
                 
                 updateLayout();
                 updateZoomLabel();
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
    
    QString baseText = QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(ms / 10, 2, 10, QChar('0'));
        
    // Append zoom info if available
    QString currentText = timeLabel->text();
    if (currentText.contains("|")) {
        QString zoomPart = currentText.split("|").last();
        timeLabel->setText(baseText + " |" + zoomPart);
    } else {
        timeLabel->setText(baseText);
    }
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
    
    // Add to model first
    model->addEvent(evt);
    
    // Rebuild visual items to reflect model state and setup connections
    rebuildEventItems();
}

void TimelineDock::rebuildEventItems() {
    clearEventItems();
    
    const auto& events = model->getEvents();
    for (size_t i = 0; i < events.size(); i++) {
        const auto& evt = events[i];
        TimelineEventItem *item = new TimelineEventItem(evt.timeMs, pixelsPerSecond);
        scene->addItem(item);
        item->setToolTip(QString("Event: %1").arg(evt.targetId));
        eventItemMap[evt.timeMs] = item;
        
        int eventIndex = static_cast<int>(i);
        
        // Connect movement
        connect(item, &TimelineEventItem::eventMoved, 
                [this, eventIndex](double newTime) {
            model->updateEventTime(eventIndex, newTime);
        });
        
        // Connect double-click
        connect(item, &TimelineEventItem::doubleClicked,
                [this, item, eventIndex]() {
            onEventDoubleClicked(item, eventIndex);
        });
        
        // Connect deletion
        connect(item, &TimelineEventItem::deleteRequested,
                [this, item, eventIndex]() {
            model->removeEvent(eventIndex);
            rebuildEventItems(); // Rebuild to clean up and re-index
        });
    }
}

void TimelineDock::clearEventItems() {
    for (auto& pair : eventItemMap) {
        if (pair.second) {
             scene->removeItem(pair.second);
             delete pair.second;
        }
    }
    eventItemMap.clear();
}

void TimelineDock::onEventDoubleClicked(TimelineEventItem *item, int eventIndex) {
    if (eventIndex < 0 || eventIndex >= model->getEvents().size()) return;
    
    const TimelineEvent& evt = model->getEvents()[eventIndex];
    
    QDialog dialog(this);
    dialog.setWindowTitle("Edit Timeline Event");
    dialog.setMinimumWidth(300);
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    // Event type
    layout->addWidget(new QLabel("Event Type:"));
    QComboBox *typeCombo = new QComboBox(&dialog);
    typeCombo->addItem("Scene Switch", static_cast<int>(TimelineEventType::SceneSwitch));
    typeCombo->addItem("Media Start", static_cast<int>(TimelineEventType::MediaStart));
    typeCombo->addItem("Media Stop", static_cast<int>(TimelineEventType::MediaStop));
    typeCombo->setCurrentIndex(static_cast<int>(evt.type));
    layout->addWidget(typeCombo);
    
    // Target scene
    layout->addWidget(new QLabel("Target Scene:"));
    QComboBox *sceneCombo = new QComboBox(&dialog);
    
    // Populate with OBS scenes
    struct obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);
    for (size_t i = 0; i < scenes.sources.num; i++) {
        obs_source_t *source = scenes.sources.array[i];
        const char *name = obs_source_get_name(source);
        sceneCombo->addItem(QString::fromUtf8(name));
        if (evt.targetId == QString::fromUtf8(name)) {
            sceneCombo->setCurrentIndex(sceneCombo->count() - 1);
        }
    }
    obs_frontend_source_list_free(&scenes);
    layout->addWidget(sceneCombo);
    
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    
    if (dialog.exec() == QDialog::Accepted) {
        TimelineEvent updatedEvt = evt;
        updatedEvt.type = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
        updatedEvt.targetId = sceneCombo->currentText();
        
        model->replaceEvent(eventIndex, updatedEvt);
        item->setToolTip(QString("Event: %1").arg(updatedEvt.targetId));
        rebuildEventItems(); // Refresh view
    }
}

void TimelineDock::updateZoomLabel() {
    QString zoomText;
    if (pixelsPerSecond < 20) {
        zoomText = QString("Zoom: %1x (Wide)").arg(pixelsPerSecond / 10.0, 0, 'f', 1);
    } else if (pixelsPerSecond > 200) {
        zoomText = QString("Zoom: %1x (Close)").arg(pixelsPerSecond / 10.0, 0, 'f', 1);
    } else {
        zoomText = QString("Zoom: %1x").arg(pixelsPerSecond / 10.0, 0, 'f', 1);
    }
    
    QString currentText = timeLabel->text();
    if (currentText.contains("|")) {
        currentText = currentText.split("|").first().trimmed();
    }
    timeLabel->setText(currentText + " | " + zoomText);
}
