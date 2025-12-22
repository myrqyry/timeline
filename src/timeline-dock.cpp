#include "timeline-dock.h"
#include <obs-module.h>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QResizeEvent>
#include <obs-frontend-api.h>

#include <QFile>
#include <QJsonDocument>

#include "timeline-event-item.h"
#include "theme-helpers.h"
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QWheelEvent>
#include <cmath>
#include <QDialog>
#include <QFileDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QKeyEvent>

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
    connect(model, &TimelineModel::modelChanged, this, &TimelineDock::rebuildEventItems);
    
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
        QJsonObject root;
        root["events"] = model->toJson();
        
        QJsonObject settings;
        settings["loop"] = loopCheckbox->isChecked();
        settings["endTime"] = endTimeSpinbox->value();
        settings["snap"] = snapCheckbox->isChecked();
        root["settings"] = settings;
        
        QJsonDocument doc(root);
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
        
        if (doc.isArray()) {
            // Legacy format (just events)
            model->fromJson(doc.array());
        } else if (doc.isObject()) {
            // New format
            QJsonObject root = doc.object();
            model->fromJson(root["events"].toArray());
            
            QJsonObject settings = root["settings"].toObject();
            // Block signals to avoid triggering unnecessary logic during load
            bool loop = settings["loop"].toBool();
            double end = settings["endTime"].toDouble();
            bool snap = settings["snap"].toBool();
            
            loopCheckbox->setChecked(loop);
            endTimeSpinbox->setValue(end);
            snapCheckbox->setChecked(snap);
            
            // Apply settings manually since signals might be blocked (or just to be safe)
            onLoopToggled(loop);
            onEndTimeChanged(end);
            onSnapToggled(snap);
        }
        
        updateLayout(); // Refresh UI with new events
        blog(LOG_INFO, "Loaded timeline from %s", configPath);
    } else {
        blog(LOG_INFO, "No config file found at: %s", configPath);
    }
    bfree(configPath);
}

void TimelineDock::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Unified Controls Row
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    
    // Playback Controls
    playButton = new QPushButton("Play", this);
    playButton->setToolTip("Start/pause timeline playback");
    stopButton = new QPushButton("Stop", this);
    stopButton->setToolTip("Stop and reset to beginning");
    addEventButton = new QPushButton("Add Event", this);
    addEventButton->setToolTip("Add a new event at the current playhead position");
    
    connect(playButton, &QPushButton::clicked, this, &TimelineDock::onPlayClicked);
    connect(stopButton, &QPushButton::clicked, this, &TimelineDock::onStopClicked);
    connect(addEventButton, &QPushButton::clicked, this, &TimelineDock::onAddEventClicked);
    
    controlsLayout->addWidget(playButton);
    controlsLayout->addWidget(stopButton);
    controlsLayout->addWidget(addEventButton);
    
    // Save/Load
    QPushButton *saveButton = new QPushButton("Save", this);
    saveButton->setToolTip("Export timeline to JSON file");
    QPushButton *loadButton = new QPushButton("Load", this);
    loadButton->setToolTip("Import timeline from JSON file");
    
    connect(saveButton, &QPushButton::clicked, this, &TimelineDock::onSaveClicked);
    connect(loadButton, &QPushButton::clicked, this, &TimelineDock::onLoadClicked);
    
    controlsLayout->addWidget(saveButton);
    controlsLayout->addWidget(loadButton);
    
    // Time Display
    timeLabel = new QLabel("00:00:00", this);
    timeLabel->setToolTip("Current timeline position | Zoom level");
    controlsLayout->addWidget(timeLabel);
    
    // Spacer to push options to the right
    controlsLayout->addStretch();
    
    // Options
    snapCheckbox = new QCheckBox("Snap", this);
    snapCheckbox->setToolTip("Snap events to grid lines when dragging");
    connect(snapCheckbox, &QCheckBox::toggled, this, &TimelineDock::onSnapToggled);
    controlsLayout->addWidget(snapCheckbox);
    
    loopCheckbox = new QCheckBox("Loop", this);
    loopCheckbox->setToolTip("Loop playback when end time is reached");
    connect(loopCheckbox, &QCheckBox::toggled, this, &TimelineDock::onLoopToggled);
    controlsLayout->addWidget(loopCheckbox);
    
    QLabel *endLabel = new QLabel("End:", this);
    controlsLayout->addWidget(endLabel);
    
    endTimeSpinbox = new QDoubleSpinBox(this);
    endTimeSpinbox->setRange(0, 3600); // Up to 1 hour
    endTimeSpinbox->setValue(0);
    endTimeSpinbox->setDecimals(1);
    endTimeSpinbox->setSingleStep(1.0);
    endTimeSpinbox->setToolTip("Set timeline end time (0 = no limit)");
    connect(endTimeSpinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            this, &TimelineDock::onEndTimeChanged);
    controlsLayout->addWidget(endTimeSpinbox);
    
    mainLayout->addLayout(controlsLayout);
    
    // Connect scheduler signals
    connect(scheduler, &TimelineScheduler::finished, this, &TimelineDock::onTimelineFinished);
    connect(scheduler, &TimelineScheduler::looped, this, &TimelineDock::onTimelineLooped);
    
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
    playheadItem = scene->addLine(0, 0, 0, view->height(), QPen(Qt::red, 2));
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
    
    // 4. Redraw ruler and grid
    drawRuler();
    drawGrid();
    updateEndMarker();
    
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
    
        // Only start scrubbing on press, not on move (to allow item dragging)
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QPoint pos = mouseEvent->pos();
                QPointF scenePos = view->mapToScene(pos);
                
                // Check if clicking on a movable item - if so, let it handle the drag
                QList<QGraphicsItem*> items = scene->items(scenePos);
                for (QGraphicsItem* item : items) {
                    if (item && (item->flags() & QGraphicsItem::ItemIsMovable)) {
                        // Found a movable item - don't scrub, let item handle it
                        return false;
                    }
                }
                
                // Not on an item - perform scrub
                double x = scenePos.x();
                double timeMs = (x / pixelsPerSecond) * 1000.0;
                if (timeMs < 0) timeMs = 0;
                
                if (scheduler->isPlaying()) {
                    scheduler->pause();
                    playButton->setText("Play");
                }
                
                scheduler->seek(timeMs);
                return true;
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

void TimelineDock::onStopClicked() {
    scheduler->stop();
    playButton->setText("Play");
}

void TimelineDock::onLoopToggled(bool checked) {
    scheduler->setLooping(checked);
}

void TimelineDock::onEndTimeChanged(double value) {
    scheduler->setEndTime(value * 1000.0); // Convert seconds to ms
    updateEndMarker();
}

void TimelineDock::onTimelineFinished() {
    playButton->setText("Play");
    blog(LOG_INFO, "Timeline playback finished");
}

void TimelineDock::onTimelineLooped() {
    blog(LOG_INFO, "Timeline looped");
}

void TimelineDock::onSnapToggled(bool checked) {
    snapEnabled = checked;
    // Update all existing event items
    for (auto& pair : eventItemMap) {
        if (pair.second) {
            pair.second->setSnapEnabled(snapEnabled);
            // Determine snap interval based on zoom level
            double stepMs = 100.0; // 100ms snap
            pair.second->setSnapInterval(stepMs);
        }
    }
}

void TimelineDock::onSaveClicked() {
    QString fileName = QFileDialog::getSaveFileName(this, "Save Timeline", "", "JSON Files (*.json)");
    if (fileName.isEmpty()) return;
    
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) {
        fileName += ".json";
    }
    
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonObject root;
        root["events"] = model->toJson();
        
        QJsonObject settings;
        settings["loop"] = loopCheckbox->isChecked();
        settings["endTime"] = endTimeSpinbox->value();
        settings["snap"] = snapCheckbox->isChecked();
        root["settings"] = settings;
        
        QJsonDocument doc(root);
        file.write(doc.toJson());
        file.close();
        blog(LOG_INFO, "Exported timeline to %s", fileName.toStdString().c_str());
    }
}

void TimelineDock::onLoadClicked() {
    QString fileName = QFileDialog::getOpenFileName(this, "Load Timeline", "", "JSON Files (*.json)");
    if (fileName.isEmpty()) return;
    
    QFile file(fileName);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        
        if (doc.isArray()) {
            model->fromJson(doc.array());
        } else if (doc.isObject()) {
            QJsonObject root = doc.object();
            model->fromJson(root["events"].toArray());
            
            QJsonObject settings = root["settings"].toObject();
            loopCheckbox->setChecked(settings["loop"].toBool());
            endTimeSpinbox->setValue(settings["endTime"].toDouble());
            snapCheckbox->setChecked(settings["snap"].toBool());
            
            // Apply settings
            onLoopToggled(loopCheckbox->isChecked());
            onEndTimeChanged(endTimeSpinbox->value());
            onSnapToggled(snapCheckbox->isChecked());
        }
        
        rebuildEventItems(); // Refresh view
        blog(LOG_INFO, "Imported timeline from %s", fileName.toStdString().c_str());
    }
}

void TimelineDock::updateEndMarker() {
    double endTimeMs = scheduler->getEndTime();
    
    // Remove existing marker if any
    if (endMarkerItem) {
        scene->removeItem(endMarkerItem);
        delete endMarkerItem;
        endMarkerItem = nullptr;
    }
    
    // Only draw if end time is set
    if (endTimeMs > 0 && view) {
        double x = (endTimeMs / 1000.0) * pixelsPerSecond;
        double h = view->viewport()->height();
        endMarkerItem = scene->addLine(x, 0, x, h, QPen(QColor(255, 165, 0), 2, Qt::DashLine)); // Orange dashed line
        endMarkerItem->setZValue(99);
        endMarkerItem->setToolTip(QString("End time: %1 sec").arg(endTimeMs / 1000.0, 0, 'f', 1));
    }
}

void TimelineDock::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
        case Qt::Key_Space:
            onPlayClicked();
            break;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            deleteSelectedEvents();
            break;
        case Qt::Key_Home:
            scheduler->seek(0);
            break;
        case Qt::Key_Left:
            if (event->modifiers() & Qt::ShiftModifier) {
                nudgeSelectedEvents(-100); // -100ms with shift
            } else {
                nudgeSelectedEvents(-1000); // -1 second
            }
            break;
        case Qt::Key_Right:
            if (event->modifiers() & Qt::ShiftModifier) {
                nudgeSelectedEvents(100); // +100ms with shift
            } else {
                nudgeSelectedEvents(1000); // +1 second
            }
            break;
        default:
            QWidget::keyPressEvent(event);
            break;
    }
}

void TimelineDock::deleteSelectedEvents() {
    QList<TimelineEventItem*> toDelete;
    
    for (auto& pair : eventItemMap) {
        if (pair.second && pair.second->isSelected()) {
            toDelete.append(pair.second);
        }
    }
    
    if (toDelete.isEmpty()) return;
    
    // Find indices to delete (in reverse order to avoid index shifts)
    std::vector<int> indices;
    const auto& events = model->getEvents();
    for (TimelineEventItem* item : toDelete) {
        double itemTime = item->getTimeMs();
        for (size_t i = 0; i < events.size(); i++) {
            if (std::abs(events[i].timeMs - itemTime) < 1.0) {
                indices.push_back(static_cast<int>(i));
                break;
            }
        }
    }
    
    // Sort descending and remove
    std::sort(indices.rbegin(), indices.rend());
    for (int idx : indices) {
        model->removeEvent(idx);
    }
    
    rebuildEventItems();
}

void TimelineDock::nudgeSelectedEvents(double deltaMs) {
    for (auto& pair : eventItemMap) {
        if (pair.second && pair.second->isSelected()) {
            double currentTime = pair.second->getTimeMs();
            double newTime = std::max(0.0, currentTime + deltaMs);
            
            // Find this event in the model and update it
            const auto& events = model->getEvents();
            for (size_t i = 0; i < events.size(); i++) {
                if (std::abs(events[i].timeMs - currentTime) < 1.0) {
                    model->updateEventTime(static_cast<int>(i), newTime);
                    break;
                }
            }
        }
    }
    rebuildEventItems();
}

void TimelineDock::drawGrid() {
    // Clear existing grid
    for (QGraphicsItem* item : gridItems) {
        scene->removeItem(item);
        delete item;
    }
    gridItems.clear();
    
    if (!scene || !view) return;
    
    double sceneWidth = scene->width();
    double sceneHeight = view->viewport()->height();
    
    // Determine grid step based on zoom level
    double stepSeconds = 1.0;
    if (pixelsPerSecond < 20) stepSeconds = 10.0;
    else if (pixelsPerSecond < 50) stepSeconds = 5.0;
    else if (pixelsPerSecond > 200) stepSeconds = 0.5;
    
    double stepPx = stepSeconds * pixelsPerSecond;
    
    // Draw vertical grid lines
    QPen gridPen(QColor(60, 60, 60), 1, Qt::DotLine);
    for (double x = 0; x < sceneWidth; x += stepPx) {
        QGraphicsLineItem* line = scene->addLine(x, 20, x, sceneHeight, gridPen);
        line->setZValue(-10); // Behind everything
        gridItems.append(line);
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
    double time = scheduler->getCurrentTime(); // Place at playhead
    
    // Show configuration dialog first
    QDialog dialog(this);
    dialog.setWindowTitle("New Timeline Event");
    dialog.setMinimumWidth(400);
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    // Event type
    layout->addWidget(new QLabel("Event Type:"));
    QComboBox *typeCombo = new QComboBox(&dialog);
    typeCombo->addItem("Scene Switch", static_cast<int>(TimelineEventType::SceneSwitch));
    typeCombo->addItem("Source Show", static_cast<int>(TimelineEventType::SourceShow));
    typeCombo->addItem("Source Hide", static_cast<int>(TimelineEventType::SourceHide));
    typeCombo->addItem("Media Start", static_cast<int>(TimelineEventType::MediaStart));
    typeCombo->addItem("Media Stop", static_cast<int>(TimelineEventType::MediaStop));
    typeCombo->addItem("Filter Enable", static_cast<int>(TimelineEventType::FilterEnable));
    typeCombo->addItem("Filter Disable", static_cast<int>(TimelineEventType::FilterDisable));
    typeCombo->addItem("Trigger Hotkey", static_cast<int>(TimelineEventType::HotkeyTrigger));
    layout->addWidget(typeCombo);
    
    // Target label (updates based on type)
    QLabel *targetLabel = new QLabel("Target Scene:", &dialog);
    layout->addWidget(targetLabel);
    
    // Target combo (scenes, sources, or filters)
    QComboBox *targetCombo = new QComboBox(&dialog);
    layout->addWidget(targetCombo);
    
    // Helper lambda to populate targets based on event type
    auto populateTargets = [&](int eventType) {
        targetCombo->clear();
        targetCombo->setEnabled(true);
        
        TimelineEventType evtType = static_cast<TimelineEventType>(eventType);
        
        if (evtType == TimelineEventType::SceneSwitch) {
            targetLabel->setText("Target Scene:");
            struct obs_frontend_source_list scenes = {};
            obs_frontend_get_scenes(&scenes);
            for (size_t i = 0; i < scenes.sources.num; i++) {
                obs_source_t *source = scenes.sources.array[i];
                const char *name = obs_source_get_name(source);
                targetCombo->addItem(QString::fromUtf8(name));
            }
            obs_frontend_source_list_free(&scenes);
            
            if (targetCombo->count() == 0) {
                targetCombo->addItem("(No scenes available)");
                targetCombo->setEnabled(false);
            }
        } 
        else if (evtType == TimelineEventType::SourceShow || evtType == TimelineEventType::SourceHide) {
            targetLabel->setText("Target Source:");
            obs_source_t *currentScene = obs_frontend_get_current_scene();
            if (currentScene) {
                obs_scene_t *scene = obs_scene_from_source(currentScene);
                if (scene) {
                    obs_scene_enum_items(scene, [](obs_scene_t*, obs_sceneitem_t *item, void *param) {
                        QComboBox *combo = static_cast<QComboBox*>(param);
                        obs_source_t *source = obs_sceneitem_get_source(item);
                        const char *name = obs_source_get_name(source);
                        combo->addItem(QString::fromUtf8(name));
                        return true;
                    }, targetCombo);
                }
                obs_source_release(currentScene);
            }
            
            if (targetCombo->count() == 0) {
                targetCombo->addItem("(No sources in current scene)");
                targetCombo->setEnabled(false);
            }
        }
        else if (evtType == TimelineEventType::MediaStart || evtType == TimelineEventType::MediaStop) {
            targetLabel->setText("Target Media Source:");
            obs_source_t *currentScene = obs_frontend_get_current_scene();
            if (currentScene) {
                obs_scene_t *scene = obs_scene_from_source(currentScene);
                if (scene) {
                    obs_scene_enum_items(scene, [](obs_scene_t*, obs_sceneitem_t *item, void *param) {
                        QComboBox *combo = static_cast<QComboBox*>(param);
                        obs_source_t *source = obs_sceneitem_get_source(item);
                        const char *id = obs_source_get_id(source);
                        if (id && (strcmp(id, "ffmpeg_source") == 0 || strcmp(id, "vlc_source") == 0)) {
                            const char *name = obs_source_get_name(source);
                            combo->addItem(QString::fromUtf8(name));
                        }
                        return true;
                    }, targetCombo);
                }
                obs_source_release(currentScene);
            }
            
            if (targetCombo->count() == 0) {
                targetCombo->addItem("(No media sources in current scene)");
                targetCombo->setEnabled(false);
            }
        }
        else if (evtType == TimelineEventType::FilterEnable || evtType == TimelineEventType::FilterDisable) {
            targetLabel->setText("Target Filter (Source::Filter):");
            obs_source_t *currentScene = obs_frontend_get_current_scene();
            if (currentScene) {
                obs_scene_t *scene = obs_scene_from_source(currentScene);
                if (scene) {
                    // Enumerate sources and their filters
                    obs_scene_enum_items(scene, [](obs_scene_t*, obs_sceneitem_t *item, void *param) {
                        QComboBox *combo = static_cast<QComboBox*>(param);
                        obs_source_t *source = obs_sceneitem_get_source(item);
                        QString sourceName = QString::fromUtf8(obs_source_get_name(source));
                        
                        // Enumerate filters on this source
                        obs_source_enum_filters(source, [](obs_source_t*, obs_source_t *filter, void *data) {
                            auto *info = static_cast<std::pair<QComboBox*, QString>*>(data);
                            QString filterName = QString::fromUtf8(obs_source_get_name(filter));
                            info->first->addItem(QString("%1::%2").arg(info->second).arg(filterName));
                        }, new std::pair<QComboBox*, QString>(combo, sourceName));
                        
                        return true;
                    }, targetCombo);
                }
                obs_source_release(currentScene);
            }
            
            if (targetCombo->count() == 0) {
                targetCombo->addItem("(No filters found in current scene)");
                targetCombo->setEnabled(false);
            }
        }
        else if (evtType == TimelineEventType::HotkeyTrigger) {
            targetLabel->setText("Target Hotkey:");
            // Enumerate all registered hotkeys
            obs_enum_hotkeys([](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
                Q_UNUSED(id);
                QComboBox *combo = static_cast<QComboBox*>(data);
                const char *name = obs_hotkey_get_name(hotkey);
                if (name && strlen(name) > 0) {
                    // Get the description for a friendlier display
                    const char *desc = obs_hotkey_get_description(hotkey);
                    QString displayName = desc && strlen(desc) > 0 
                        ? QString::fromUtf8(desc) 
                        : QString::fromUtf8(name);
                    combo->addItem(displayName, QString::fromUtf8(name));
                }
                return true;
            }, targetCombo);
            
            if (targetCombo->count() == 0) {
                targetCombo->addItem("(No hotkeys registered)");
                targetCombo->setEnabled(false);
            }
        }
    };
    
    // Color picker setup (Move before type connection so lambda can capture)
    TimelineEventType initialType = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
    QColor selectedColor = ThemeIcons::getDefaultColor(initialType); 
    
    QHBoxLayout *colorLayout = new QHBoxLayout();
    QLabel *colorLabel = new QLabel("Event Color:", &dialog);
    QPushButton *colorButton = new QPushButton(&dialog);
    colorButton->setFixedSize(60, 24);
    colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
    colorButton->setToolTip("Click to change event color");
    
    connect(colorButton, &QPushButton::clicked, [&]() {
        QColor newColor = QColorDialog::getColor(selectedColor, &dialog, "Select Event Color");
        if (newColor.isValid()) {
            selectedColor = newColor;
            colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
        }
    });
    
    colorLayout->addWidget(colorLabel);
    colorLayout->addWidget(colorButton);
    colorLayout->addStretch();
    layout->addLayout(colorLayout); // Add color layout before connecting type change? Or after? Doesn't matter for layout order, just logic.

    // Initial population
    populateTargets(static_cast<int>(initialType));
    
    // Update targets AND color when type changes
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [&](int index) {
        TimelineEventType newType = static_cast<TimelineEventType>(typeCombo->itemData(index).toInt());
        populateTargets(static_cast<int>(newType));
        
        // Update color to default
        selectedColor = ThemeIcons::getDefaultColor(newType);
        colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
    });
    
    // Time display (read-only info)
    int minutes = static_cast<int>(time / 1000.0) / 60;
    int seconds = static_cast<int>(time / 1000.0) % 60;
    int ms = static_cast<int>(time) % 1000;
    QString timeStr = QString("Event will be placed at: %1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(ms / 10, 2, 10, QChar('0'));
    QLabel *timeInfo = new QLabel(timeStr, &dialog);
    timeInfo->setStyleSheet("color: gray; font-style: italic;");
    layout->addWidget(timeInfo);
    
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    
    if (dialog.exec() == QDialog::Accepted) {
        // Validate: ensure we have a valid target
        if (!targetCombo->isEnabled() || targetCombo->currentText().startsWith("(")) {
            blog(LOG_WARNING, "Cannot create event: No valid target selected");
            return;
        }
        
        TimelineEvent evt;
        evt.timeMs = time;
        evt.type = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
        
        // For hotkeys, use the internal name stored in userData; otherwise use display text
        if (evt.type == TimelineEventType::HotkeyTrigger) {
            evt.targetId = targetCombo->currentData().toString();
        } else {
            evt.targetId = targetCombo->currentText();
        }
        evt.color = selectedColor;
        
        model->addEvent(evt);
        rebuildEventItems();
    }
}

void TimelineDock::rebuildEventItems() {
    clearEventItems();
    
    const auto& events = model->getEvents();
    
    // Track stacking - count events at same time (within 50ms tolerance)
    std::map<int, int> timeSlotCount; // time bucket -> count
    const double STACK_TOLERANCE_MS = 50.0;
    
    for (size_t i = 0; i < events.size(); i++) {
        const auto& evt = events[i];
        int timeBucket = static_cast<int>(evt.timeMs / STACK_TOLERANCE_MS);
        int stackIndex = timeSlotCount[timeBucket]++;
        
        TimelineEventItem *item = new TimelineEventItem(evt.timeMs, pixelsPerSecond, evt.type, evt.color);
        item->setStackOffset(stackIndex);
        
        // Apply snap settings
        item->setSnapEnabled(snapEnabled);
        double stepMs = 100.0; // 100ms snap
        item->setSnapInterval(stepMs);
        
        scene->addItem(item);
        
        // Enhanced tooltip with event details
        QString typeStr;
        switch (evt.type) {
            case TimelineEventType::SceneSwitch: typeStr = "Scene Switch"; break;
            case TimelineEventType::SourceShow: typeStr = "Source Show"; break;
            case TimelineEventType::SourceHide: typeStr = "Source Hide"; break;
            case TimelineEventType::MediaStart: typeStr = "Media Start"; break;
            case TimelineEventType::MediaStop: typeStr = "Media Stop"; break;
            case TimelineEventType::FilterEnable: typeStr = "Filter Enable"; break;
            case TimelineEventType::FilterDisable: typeStr = "Filter Disable"; break;
            case TimelineEventType::HotkeyTrigger: typeStr = "Hotkey Trigger"; break;
            default: typeStr = "Unknown"; break;
        }
        double timeSec = evt.timeMs / 1000.0;
        int minutes = static_cast<int>(timeSec) / 60;
        int seconds = static_cast<int>(timeSec) % 60;
        int ms = static_cast<int>(evt.timeMs) % 1000;
        QString tooltip = QString("%1\nTarget: %2\nTime: %3:%4.%5\n\nDouble-click to edit | Right-click for options")
            .arg(typeStr)
            .arg(evt.targetId)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(ms / 10, 2, 10, QChar('0'));
        item->setToolTip(tooltip);
        item->setEventLabel(evt.targetId);
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
                [this, eventIndex]() {
            model->removeEvent(eventIndex);
            rebuildEventItems(); // Rebuild to clean up and re-index
        });
        
        // Connect color change
        connect(item, &TimelineEventItem::colorChangeRequested,
                [this, item, eventIndex]() {
            QColor newColor = QColorDialog::getColor(item->getEventColor(), this, "Select Event Color");
            if (newColor.isValid()) {
                item->setEventColor(newColor);
                // Update model
                auto events = model->getEvents();
                if (eventIndex < events.size()) {
                    TimelineEvent updatedEvt = events[eventIndex];
                    updatedEvt.color = newColor;
                    model->replaceEvent(eventIndex, updatedEvt);
                }
            }
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
    dialog.setMinimumWidth(400);
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    // Event type
    layout->addWidget(new QLabel("Event Type:"));
    QComboBox *typeCombo = new QComboBox(&dialog);
    typeCombo->addItem("Scene Switch", static_cast<int>(TimelineEventType::SceneSwitch));
    typeCombo->addItem("Source Show", static_cast<int>(TimelineEventType::SourceShow));
    typeCombo->addItem("Source Hide", static_cast<int>(TimelineEventType::SourceHide));
    typeCombo->addItem("Media Start", static_cast<int>(TimelineEventType::MediaStart));
    typeCombo->addItem("Media Stop", static_cast<int>(TimelineEventType::MediaStop));
    typeCombo->addItem("Filter Enable", static_cast<int>(TimelineEventType::FilterEnable));
    typeCombo->addItem("Filter Disable", static_cast<int>(TimelineEventType::FilterDisable));
    typeCombo->addItem("Trigger Hotkey", static_cast<int>(TimelineEventType::HotkeyTrigger));
    
    // Find and select current type
    for (int i = 0; i < typeCombo->count(); i++) {
        if (typeCombo->itemData(i).toInt() == static_cast<int>(evt.type)) {
            typeCombo->setCurrentIndex(i);
            break;
        }
    }
    layout->addWidget(typeCombo);
    
    // Target
    layout->addWidget(new QLabel("Target:"));
    QLineEdit *targetEdit = new QLineEdit(&dialog);
    targetEdit->setText(evt.targetId);
    layout->addWidget(targetEdit);
    
    // Color picker
    QColor selectedColor = evt.color;
    QHBoxLayout *colorLayout = new QHBoxLayout();
    QLabel *colorLabel = new QLabel("Event Color:", &dialog);
    QPushButton *colorButton = new QPushButton(&dialog);
    colorButton->setFixedSize(60, 24);
    colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
    
    connect(colorButton, &QPushButton::clicked, [&]() {
        QColor newColor = QColorDialog::getColor(selectedColor, &dialog, "Select Event Color");
        if (newColor.isValid()) {
            selectedColor = newColor;
            colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
        }
    });
    
    colorLayout->addWidget(colorButton);
    colorLayout->addStretch();
    layout->addLayout(colorLayout);

    // Update color when type changes (to default for that type)
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [&](int index) {
        TimelineEventType newType = static_cast<TimelineEventType>(typeCombo->itemData(index).toInt());
        selectedColor = ThemeIcons::getDefaultColor(newType);
        colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
    });
    
    // Time info
    double timeSec = evt.timeMs / 1000.0;
    QString timeStr = QString("Current time: %1 sec").arg(timeSec, 0, 'f', 2);
    QLabel *timeInfo = new QLabel(timeStr, &dialog);
    timeInfo->setStyleSheet("color: gray; font-style: italic;");
    layout->addWidget(timeInfo);
    
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    
    if (dialog.exec() == QDialog::Accepted) {
        TimelineEvent updatedEvt = evt;
        updatedEvt.type = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
        updatedEvt.targetId = targetEdit->text();
        updatedEvt.color = selectedColor;
        
        model->replaceEvent(eventIndex, updatedEvt);
        rebuildEventItems();
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
