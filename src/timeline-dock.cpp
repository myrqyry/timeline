#include "timeline-dock.h"
#include <obs-module.h>

#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QResizeEvent>
#include <obs-frontend-api.h>
#if OBS_TIMELINE_HAS_WEBSOCKET
#include <obs-websocket-api.h>
#endif
#include <QDateTime>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDateTime>

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
#include <QUuid>
#include <QApplication>
#include <QShortcut>
#include "event-editor.h"
#include "timeline-serialization.h"

TimelineDock::TimelineDock(QWidget *parent) : QWidget(parent)
{
	blog(LOG_INFO, "TimelineDock constructor started");
	model = new TimelineModel(this);
	blog(LOG_INFO, "TimelineDock: Model created");
	scheduler = new TimelineScheduler(model, this);
	blog(LOG_INFO, "TimelineDock: Scheduler created");
	executor = new ObsExecutor(this);
	blog(LOG_INFO, "TimelineDock: Executor created");

	setupUi();

	connect(scheduler, &TimelineScheduler::tick, this, &TimelineDock::onTick);
	connect(scheduler, &TimelineScheduler::eventStarted, executor, &ObsExecutor::executeEventStart);
	connect(scheduler, &TimelineScheduler::eventEnded, executor, &ObsExecutor::executeEventEnd);
	connect(model, &TimelineModel::modelChanged, this, [this]() {
		if (!rebuildScheduled) {
			rebuildScheduled = true;
			QMetaObject::invokeMethod(this, &TimelineDock::rebuildEventItems, Qt::QueuedConnection);
		}
	});

	// Register save callback
	obs_frontend_add_save_callback(saveCallback, this);

	// Register event callback for recording status
	obs_frontend_add_event_callback(frontendEvent, this);

	// Register Hotkeys
	hotkeyIdPlayPause = obs_hotkey_register_frontend("Timeline.PlayPause", "Timeline: Play/Pause",
							 TimelineDock::hotkeyPlayPause, this);
	hotkeyIdStop = obs_hotkey_register_frontend("Timeline.Stop", "Timeline: Stop", TimelineDock::hotkeyStop, this);
	hotkeyIdRestart = obs_hotkey_register_frontend("Timeline.Restart", "Timeline: Restart",
						       TimelineDock::hotkeyRestart, this);

	// Initial Load
	blog(LOG_INFO, "TimelineDock loading initial data");
	load();

	blog(LOG_INFO, "TimelineDock constructor finished");
}

TimelineDock::~TimelineDock()
{
	obs_frontend_remove_save_callback(saveCallback, this);
	obs_frontend_remove_event_callback(frontendEvent, this);

	obs_hotkey_unregister(hotkeyIdPlayPause);
	obs_hotkey_unregister(hotkeyIdStop);
	obs_hotkey_unregister(hotkeyIdRestart);

	clearRuler();
}

void TimelineDock::saveCallback(obs_data_t *save_data, bool saving, void *private_data)
{
	TimelineDock *dock = static_cast<TimelineDock *>(private_data);
	if (saving) {
		dock->save();
	}
}

void TimelineDock::frontendEvent(enum obs_frontend_event event, void *private_data)
{
	TimelineDock *dock = static_cast<TimelineDock *>(private_data);
	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		QMetaObject::invokeMethod(dock->recordButton, "setText", Qt::QueuedConnection,
					  Q_ARG(QString, "Stop Rec"));
		QMetaObject::invokeMethod(dock->recordButton, "setStyleSheet", Qt::QueuedConnection,
					  Q_ARG(QString, "color: red; font-weight: bold;"));
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		QMetaObject::invokeMethod(dock->recordButton, "setText", Qt::QueuedConnection,
					  Q_ARG(QString, "Record"));
		QMetaObject::invokeMethod(dock->recordButton, "setStyleSheet", Qt::QueuedConnection,
					  Q_ARG(QString, ""));
	}
}

void TimelineDock::setObsWebSocketVendor(void *vendor)
{
	this->obsWebSocketVendor = vendor;
	if (executor) {
		executor->setObsWebSocketVendor(vendor);
	}
}

void TimelineDock::hotkeyPlayPause(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;
	TimelineDock *dock = static_cast<TimelineDock *>(data);
	QMetaObject::invokeMethod(dock, "onPlayClicked", Qt::QueuedConnection);
}

void TimelineDock::hotkeyStop(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;
	TimelineDock *dock = static_cast<TimelineDock *>(data);
	QMetaObject::invokeMethod(dock, "onStopClicked", Qt::QueuedConnection);
}

void TimelineDock::hotkeyRestart(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;
	TimelineDock *dock = static_cast<TimelineDock *>(data);
	// Restart logic: Stop then Start
	// Using invokeMethod to ensure thread safety if hotkey comes from a different thread
	QMetaObject::invokeMethod(dock->scheduler, "stop", Qt::QueuedConnection);
	QMetaObject::invokeMethod(dock->scheduler, "start", Qt::QueuedConnection);
	// Also update UI
	QMetaObject::invokeMethod(dock->playButton, "setText", Qt::QueuedConnection, Q_ARG(QString, "Pause"));
}

void TimelineDock::save()
{
	char *configPath = obs_module_get_config_path(obs_current_module(), "timeline_events.json");
	if (!configPath)
		return;

	const QJsonObject root = TimelineSerialization::toJsonObject(
		*model, loopCheckbox->isChecked(), endTimeSpinbox->value(), snapCheckbox->isChecked(), bpm,
		gridToBpm, centerControls);
	QString errorMessage;
	if (TimelineSerialization::saveToFile(configPath, root, &errorMessage)) {
		blog(LOG_INFO, "Saved timeline to %s", configPath);
	} else {
		blog(LOG_WARNING, "Failed to save timeline to %s: %s", configPath,
		     errorMessage.toUtf8().constData());
	}
	bfree(configPath);
}

void TimelineDock::load()
{
	char *configPath = obs_module_get_config_path(obs_current_module(), "timeline_events.json");
	if (!configPath)
		return;

	QJsonDocument doc;
	QString errorMessage;
	if (TimelineSerialization::loadFromFile(configPath, doc, &errorMessage)) {
		blog(LOG_INFO, "Loading config file: %s", configPath);
		TimelineDocument timelineDoc;
		if (!TimelineSerialization::fromJsonDocument(doc, *model, timelineDoc, &errorMessage)) {
			blog(LOG_WARNING, "Failed to parse timeline config %s: %s", configPath,
			     errorMessage.toUtf8().constData());
			bfree(configPath);
			return;
		}

		loopCheckbox->setChecked(timelineDoc.loop);
		endTimeSpinbox->setValue(timelineDoc.endTimeSeconds);
		snapCheckbox->setChecked(timelineDoc.snap);
		bpmSpinbox->setValue(timelineDoc.bpm);
		bpmGridCheckbox->setChecked(timelineDoc.gridToBpm);
		centerControlsCheckbox->setChecked(timelineDoc.centerControls);

		onLoopToggled(timelineDoc.loop);
		onEndTimeChanged(timelineDoc.endTimeSeconds);
		onSnapToggled(timelineDoc.snap);
		onBpmChanged(timelineDoc.bpm);
		onBpmGridToggled(timelineDoc.gridToBpm);
		onCenterControlsToggled(timelineDoc.centerControls);

		updateLayout(); // Refresh UI with new events
		blog(LOG_INFO, "Loaded timeline from %s", configPath);
	} else {
		blog(LOG_INFO, "No config file found at: %s (%s)", configPath, errorMessage.toUtf8().constData());
	}
	bfree(configPath);
}

void TimelineDock::setupUi()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	auto shouldIgnoreShortcut = [this]() -> bool {
		QWidget *focusWidget = QApplication::focusWidget();
		return focusWidget &&
		       (qobject_cast<QLineEdit *>(focusWidget) || qobject_cast<QComboBox *>(focusWidget));
	};

	auto *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
	spaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(spaceShortcut, &QShortcut::activated, this, [this, shouldIgnoreShortcut]() {
		if (!shouldIgnoreShortcut())
			onPlayClicked();
	});

	auto *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);
	deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(deleteShortcut, &QShortcut::activated, this, [this, shouldIgnoreShortcut]() {
		if (!shouldIgnoreShortcut())
			deleteSelectedEvents();
	});

	auto *homeShortcut = new QShortcut(QKeySequence(Qt::Key_Home), this);
	homeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(homeShortcut, &QShortcut::activated, this, [this, shouldIgnoreShortcut]() {
		if (!shouldIgnoreShortcut())
			scheduler->seek(0);
	});

	auto *copyShortcut = new QShortcut(QKeySequence::Copy, this);
	copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(copyShortcut, &QShortcut::activated, this, [this, shouldIgnoreShortcut]() {
		if (!shouldIgnoreShortcut())
			copySelectedEvents();
	});

	auto *pasteShortcut = new QShortcut(QKeySequence::Paste, this);
	pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(pasteShortcut, &QShortcut::activated, this, [this, shouldIgnoreShortcut]() {
		if (!shouldIgnoreShortcut())
			pasteEvents();
	});

	// Unified Controls Row
	controlsLayout = new QHBoxLayout();

	// Playback Controls
	playButton = new QPushButton("Play", this);
	playButton->setToolTip("Start/pause timeline playback");
	stopButton = new QPushButton("Stop", this);
	stopButton->setToolTip("Stop and reset to beginning");
	recordButton = new QPushButton("Record", this);
	recordButton->setToolTip("Start/Stop Recording");
	addEventButton = new QPushButton("Add Event", this);
	addEventButton->setToolTip("Add a new event at the current playhead position");

	connect(playButton, &QPushButton::clicked, this, &TimelineDock::onPlayClicked);
	connect(stopButton, &QPushButton::clicked, this, &TimelineDock::onStopClicked);
	connect(recordButton, &QPushButton::clicked, this, &TimelineDock::onRecordClicked);
	connect(addEventButton, &QPushButton::clicked, this, &TimelineDock::onAddEventClicked);

	// Save/Load (will be added in updateControlsLayout)

	// Time Display
	timeLabel = new QLabel("00:00:00", this);
	timeLabel->setToolTip("Current timeline position | Zoom level");

	// Options
	snapCheckbox = new QCheckBox("Snap", this);
	snapCheckbox->setToolTip("Snap events to grid lines when dragging");
	connect(snapCheckbox, &QCheckBox::toggled, this, &TimelineDock::onSnapToggled);

	loopCheckbox = new QCheckBox("Loop", this);
	loopCheckbox->setToolTip("Loop playback when end time is reached");
	connect(loopCheckbox, &QCheckBox::toggled, this, &TimelineDock::onLoopToggled);

	centerControlsCheckbox = new QCheckBox("Center", this);
	centerControlsCheckbox->setToolTip("Center controls in the interface");
	connect(centerControlsCheckbox, &QCheckBox::toggled, this, &TimelineDock::onCenterControlsToggled);

	bpmGridCheckbox = new QCheckBox("BPM Grid", this);
	bpmGridCheckbox->setToolTip("Align grid to BPM");
	connect(bpmGridCheckbox, &QCheckBox::toggled, this, &TimelineDock::onBpmGridToggled);

	bpmSpinbox = new QDoubleSpinBox(this);
	bpmSpinbox->setRange(10, 300);
	bpmSpinbox->setValue(120);
	bpmSpinbox->setToolTip("Beats Per Minute");
	connect(bpmSpinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TimelineDock::onBpmChanged);

	endTimeSpinbox = new QDoubleSpinBox(this);
	endTimeSpinbox->setRange(0, 3600); // Up to 1 hour
	endTimeSpinbox->setValue(0);
	endTimeSpinbox->setDecimals(1);
	endTimeSpinbox->setSingleStep(1.0);
	endTimeSpinbox->setToolTip("Set timeline end time (0 = no limit)");
	connect(endTimeSpinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
		&TimelineDock::onEndTimeChanged);

	saveButton = new QPushButton("Save", this);
	saveButton->setToolTip("Export timeline to JSON file");
	connect(saveButton, &QPushButton::clicked, this, &TimelineDock::onSaveClicked);

	loadButton = new QPushButton("Load", this);
	loadButton->setToolTip("Import timeline from JSON file");
	connect(loadButton, &QPushButton::clicked, this, &TimelineDock::onLoadClicked);

	endLabel = new QLabel("End:", this);

	// Initial update
	if (obs_frontend_recording_active()) {
		recordButton->setText("Stop Rec");
		recordButton->setStyleSheet("color: red; font-weight: bold;");
	}

	updateControlsLayout();

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

	// Drag Guide (hidden by default)
	dragGuideItem = scene->addLine(0, 0, 0, view->height(), QPen(QColor(0, 255, 0), 1, Qt::DashLine));
	dragGuideItem->setZValue(110);
	dragGuideItem->setVisible(false);

	// Initial layout
	blog(LOG_INFO, "setupUi: calling updateLayout");
	updateLayout();

	mainLayout->addWidget(view);
}

void TimelineDock::updateControlsLayout()
{
	// Clear layout (but don't delete widgets!)
	QLayoutItem *child;
	while ((child = controlsLayout->takeAt(0)) != 0) {
		if (child->widget()) {
			child->widget()->hide(); // Hide widgets so they don't appear floating
		}
		delete child;
	}

	auto show = [](QWidget *w) {
		w->show();
		return w;
	};

	if (centerControls) {
		controlsLayout->addStretch();
		controlsLayout->addWidget(show(recordButton));
		controlsLayout->addWidget(show(playButton));
		controlsLayout->addWidget(show(stopButton));
		controlsLayout->addWidget(show(addEventButton));
		controlsLayout->addWidget(show(saveButton));
		controlsLayout->addWidget(show(loadButton));
		controlsLayout->addWidget(show(timeLabel));

		controlsLayout->addStretch();

		controlsLayout->addWidget(show(snapCheckbox));
		controlsLayout->addWidget(show(loopCheckbox));
		controlsLayout->addWidget(show(centerControlsCheckbox));
		controlsLayout->addWidget(show(bpmGridCheckbox));
		controlsLayout->addWidget(show(bpmSpinbox));
		controlsLayout->addWidget(show(endLabel));
		controlsLayout->addWidget(show(endTimeSpinbox));
	} else {
		controlsLayout->addWidget(show(playButton));
		controlsLayout->addWidget(show(stopButton));
		controlsLayout->addWidget(show(recordButton));
		controlsLayout->addWidget(show(addEventButton));
		controlsLayout->addWidget(show(saveButton));
		controlsLayout->addWidget(show(loadButton));
		controlsLayout->addWidget(show(timeLabel));

		controlsLayout->addStretch();

		controlsLayout->addWidget(show(snapCheckbox));
		controlsLayout->addWidget(show(loopCheckbox));
		controlsLayout->addWidget(show(centerControlsCheckbox));
		controlsLayout->addWidget(show(bpmGridCheckbox));
		controlsLayout->addWidget(show(bpmSpinbox));
		controlsLayout->addWidget(show(endLabel));
		controlsLayout->addWidget(show(endTimeSpinbox));
	}
}

void TimelineDock::onRecordClicked()
{
	if (obs_frontend_recording_active()) {
		obs_frontend_recording_stop();
	} else {
		obs_frontend_recording_start();
	}
}

void TimelineDock::onBpmGridToggled(bool checked)
{
	gridToBpm = checked;
	bpmSpinbox->setEnabled(checked);
	drawGrid();
}

void TimelineDock::onBpmChanged(double value)
{
	bpm = value;
	if (gridToBpm) {
		drawGrid();
	}
}

void TimelineDock::onCenterControlsToggled(bool checked)
{
	centerControls = checked;
	updateControlsLayout();
}

void TimelineDock::resizeEvent(QResizeEvent *event)
{
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

		// Update drag guide height
		if (dragGuideItem) {
			QLineF line = dragGuideItem->line();
			line.setP2(QPointF(line.x1(), view->viewport()->height()));
			dragGuideItem->setLine(line);
		}
	}
}

// Helper to update event items
void TimelineDock::updateLayout()
{
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

void TimelineDock::clearRuler()
{
	for (QGraphicsItem *item : rulerItems) {
		scene->removeItem(item);
		delete item;
	}
	rulerItems.clear();
}

void TimelineDock::drawRuler()
{
	if (!scene)
		return;

	clearRuler(); // Clean up old ruler

	// Draw ticks every second
	double durationSec = scene->width() / pixelsPerSecond;
	int step = 1;
	if (pixelsPerSecond < 50)
		step = 5;
	if (pixelsPerSecond < 10)
		step = 10;

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
			for (int j = 1; j < 10; j++) {
				double xm = x + (j * pixelsPerSecond / 10.0);
				QGraphicsLineItem *minor = scene->addLine(xm, 10, xm, 20, QPen(Qt::darkGray));
				rulerItems.append(minor);
			}
		}
	}
}

bool TimelineDock::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == view->viewport()) {
		if (event->type() == QEvent::Wheel) {
			QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
			if (wheelEvent->modifiers() & Qt::ControlModifier) {
				double steps = wheelEvent->angleDelta().y() / 120.0;
				double factor = std::pow(1.15, steps); // Slightly faster zoom
				pixelsPerSecond *= factor;

				// Expanded zoom range
				if (pixelsPerSecond < 5.0)
					pixelsPerSecond = 5.0;
				if (pixelsPerSecond > 1000.0)
					pixelsPerSecond = 1000.0;

				updateLayout();
				updateZoomLabel();
				return true;
			}
		}

		// Only start scrubbing on press, not on move (to allow item dragging)
		if (event->type() == QEvent::MouseButtonPress) {
			QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
			if (mouseEvent->button() == Qt::LeftButton) {
				QPoint pos = mouseEvent->pos();
				QPointF scenePos = view->mapToScene(pos);

				// Check if clicking on a movable item - if so, let it handle the drag
				QList<QGraphicsItem *> items = scene->items(scenePos);
				for (QGraphicsItem *item : items) {
					if (item && (item->flags() & QGraphicsItem::ItemIsMovable)) {
						// Found a movable item - don't scrub, let item handle it
						return false;
					}
				}

				// Not on an item - perform scrub
				double x = scenePos.x();
				double timeMs = (x / pixelsPerSecond) * 1000.0;
				if (timeMs < 0)
					timeMs = 0;

				if (scheduler->isPlaying()) {
					scheduler->pause();
					if (executor) {
						executor->cancelAll();
					}
					playButton->setText("Play");
				}

				scheduler->seek(timeMs);
				return true;
			}
		}
	}
	return QWidget::eventFilter(obj, event);
}

void TimelineDock::onPlayClicked()
{
	if (scheduler->isPlaying()) {
		scheduler->pause();
		if (executor) {
			executor->cancelAll();
		}
		playButton->setText("Play");
#if OBS_TIMELINE_HAS_WEBSOCKET
		if (obsWebSocketVendor) {
			obs_data_t *d = obs_data_create();
			obs_data_set_string(d, "event", "paused");
			obs_websocket_vendor_emit_event(obsWebSocketVendor, "PlaybackStatus", d);
			obs_data_release(d);
		}
#endif
	} else {
		scheduler->start();
		playButton->setText("Pause");
#if OBS_TIMELINE_HAS_WEBSOCKET
		if (obsWebSocketVendor) {
			obs_data_t *d = obs_data_create();
			obs_data_set_string(d, "event", "started");
			obs_websocket_vendor_emit_event(obsWebSocketVendor, "PlaybackStatus", d);
			obs_data_release(d);
		}
#endif
	}
}

void TimelineDock::onStopClicked()
{
	scheduler->stop();
	if (executor) {
		executor->cancelAll();
	}
	playButton->setText("Play");

#if OBS_TIMELINE_HAS_WEBSOCKET
	if (obsWebSocketVendor) {
		obs_data_t *d = obs_data_create();
		obs_data_set_string(d, "event", "stopped");
		obs_websocket_vendor_emit_event(obsWebSocketVendor, "PlaybackStatus", d);
		obs_data_release(d);
	}
#endif
}

void TimelineDock::onLoopToggled(bool checked)
{
	scheduler->setLooping(checked);
}

void TimelineDock::onEndTimeChanged(double value)
{
	scheduler->setEndTime(value * 1000.0); // Convert seconds to ms
	updateEndMarker();
}

void TimelineDock::onTimelineFinished()
{
	playButton->setText("Play");
	blog(LOG_INFO, "Timeline playback finished");

#if OBS_TIMELINE_HAS_WEBSOCKET
	if (obsWebSocketVendor) {
		obs_data_t *d = obs_data_create();
		obs_data_set_string(d, "event", "finished");
		obs_websocket_vendor_emit_event(obsWebSocketVendor, "PlaybackStatus", d);
		obs_data_release(d);
	}
#endif
}

void TimelineDock::onTimelineLooped()
{
	blog(LOG_INFO, "Timeline looped");
}

void TimelineDock::onSnapToggled(bool checked)
{
	snapEnabled = checked;
	// Update all existing event items
	for (TimelineEventItem *item : eventItems) {
		if (item) {
			item->setSnapEnabled(snapEnabled);
			// Determine snap interval based on zoom level
			double stepMs = 100.0; // 100ms snap
			item->setSnapInterval(stepMs);
		}
	}
}

void TimelineDock::onSaveClicked()
{
	QString fileName = QFileDialog::getSaveFileName(this, "Save Timeline", "", "JSON Files (*.json)");
	if (fileName.isEmpty())
		return;

	if (!fileName.endsWith(".json", Qt::CaseInsensitive)) {
		fileName += ".json";
	}

	const QJsonObject root = TimelineSerialization::toJsonObject(
		*model, loopCheckbox->isChecked(), endTimeSpinbox->value(), snapCheckbox->isChecked(), bpm,
		gridToBpm, centerControls);
	QString errorMessage;
	if (TimelineSerialization::saveToFile(fileName, root, &errorMessage)) {
		blog(LOG_INFO, "Exported timeline to %s", fileName.toStdString().c_str());
	} else {
		blog(LOG_WARNING, "Failed to export timeline to %s: %s", fileName.toStdString().c_str(),
		     errorMessage.toUtf8().constData());
	}
}

void TimelineDock::onLoadClicked()
{
	QString fileName = QFileDialog::getOpenFileName(this, "Load Timeline", "", "JSON Files (*.json)");
	if (fileName.isEmpty())
		return;

	QJsonDocument doc;
	QString errorMessage;
	if (TimelineSerialization::loadFromFile(fileName, doc, &errorMessage)) {
		TimelineDocument timelineDoc;
		if (!TimelineSerialization::fromJsonDocument(doc, *model, timelineDoc, &errorMessage)) {
			blog(LOG_WARNING, "Failed to import timeline from %s: %s", fileName.toStdString().c_str(),
			     errorMessage.toUtf8().constData());
			return;
		}

		loopCheckbox->setChecked(timelineDoc.loop);
		endTimeSpinbox->setValue(timelineDoc.endTimeSeconds);
		snapCheckbox->setChecked(timelineDoc.snap);
		bpmSpinbox->setValue(timelineDoc.bpm);
		bpmGridCheckbox->setChecked(timelineDoc.gridToBpm);
		centerControlsCheckbox->setChecked(timelineDoc.centerControls);

		onLoopToggled(timelineDoc.loop);
		onEndTimeChanged(timelineDoc.endTimeSeconds);
		onSnapToggled(timelineDoc.snap);
		onBpmChanged(timelineDoc.bpm);
		onBpmGridToggled(timelineDoc.gridToBpm);
		onCenterControlsToggled(timelineDoc.centerControls);

		blog(LOG_INFO, "Imported timeline from %s", fileName.toStdString().c_str());
		updateLayout();
	} else {
		blog(LOG_WARNING, "Failed to load timeline from %s: %s", fileName.toStdString().c_str(),
		     errorMessage.toUtf8().constData());
	}
}

void TimelineDock::updateEndMarker()
{
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
		endMarkerItem =
			scene->addLine(x, 0, x, h, QPen(QColor(255, 165, 0), 2, Qt::DashLine)); // Orange dashed line
		endMarkerItem->setZValue(99);
		endMarkerItem->setToolTip(QString("End time: %1 sec").arg(endTimeMs / 1000.0, 0, 'f', 1));
	}
}

void TimelineDock::keyPressEvent(QKeyEvent *event)
{
	QWidget *focusWidget = QApplication::focusWidget();
	if (focusWidget && (qobject_cast<QLineEdit *>(focusWidget) || qobject_cast<QComboBox *>(focusWidget))) {
		QWidget::keyPressEvent(event);
		return;
	}

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
	case Qt::Key_C:
		if (event->modifiers() & Qt::ControlModifier) {
			copySelectedEvents();
		}
		break;
	case Qt::Key_V:
		if (event->modifiers() & Qt::ControlModifier) {
			pasteEvents();
		}
		break;
	default:
		QWidget::keyPressEvent(event);
		break;
	}
}

void TimelineDock::copySelectedEvents()
{
	clipboard.clear();

	// Find selected items
	QList<TimelineEventItem *> selectedItems;
	for (TimelineEventItem *item : eventItems) {
		if (item && item->isSelected()) {
			selectedItems.append(item);
		}
	}

	if (selectedItems.isEmpty())
		return;

	// Get corresponding events from model
	for (TimelineEventItem *item : selectedItems) {
		const TimelineEvent *evt = model->findEventById(item->getEventId());
		if (evt) {
			clipboard.append(*evt);
		}
	}

	// Sort clipboard by time to maintain relative order
	std::sort(clipboard.begin(), clipboard.end(),
		  [](const TimelineEvent &a, const TimelineEvent &b) { return a.timeMs < b.timeMs; });

	blog(LOG_INFO, "Copied %d events to clipboard", static_cast<int>(clipboard.size()));
}

void TimelineDock::pasteEvents()
{
	if (clipboard.isEmpty())
		return;

	double currentPlayhead = scheduler->getCurrentTime();
	double firstEventTime = clipboard.first().timeMs;
	double offset = currentPlayhead - firstEventTime;

	// Deselect current selection
	for (TimelineEventItem *item : eventItems) {
		if (item)
			item->setSelected(false);
	}

	for (const auto &originalEvt : clipboard) {
		TimelineEvent newEvt = originalEvt;
		newEvt.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		newEvt.timeMs = originalEvt.timeMs + offset;

		if (newEvt.timeMs < 0)
			newEvt.timeMs = 0;

		model->addEvent(newEvt);
	}

	blog(LOG_INFO, "Pasted %d events at %f ms", static_cast<int>(clipboard.size()), currentPlayhead);
}

void TimelineDock::deleteSelectedEvents()
{
	QList<TimelineEventItem *> toDelete;

	for (TimelineEventItem *item : eventItems) {
		if (item && item->isSelected()) {
			toDelete.append(item);
		}
	}

	if (toDelete.isEmpty())
		return;

	for (TimelineEventItem *item : toDelete) {
		model->removeEventById(item->getEventId());
	}
}

void TimelineDock::nudgeSelectedEvents(double deltaMs)
{
	for (TimelineEventItem *item : eventItems) {
		if (item && item->isSelected()) {
			double currentTime = item->getTimeMs();
			double newTime = std::max(0.0, currentTime + deltaMs);

			model->updateEventTimeById(item->getEventId(), newTime);
		}
	}
}

void TimelineDock::drawGrid()
{
	// Clear existing grid
	for (QGraphicsItem *item : gridItems) {
		scene->removeItem(item);
		delete item;
	}
	gridItems.clear();

	if (!scene || !view)
		return;

	double sceneWidth = scene->width();
	double sceneHeight = view->viewport()->height();

	double stepPx;

	if (gridToBpm && bpm > 0.0) {
		// Calculate beat interval in seconds
		double beatIntervalSec = 60.0 / bpm;
		stepPx = beatIntervalSec * pixelsPerSecond;
	} else {
		// Determine grid step based on zoom level (existing logic)
		double stepSeconds = 1.0;
		if (pixelsPerSecond < 20)
			stepSeconds = 10.0;
		else if (pixelsPerSecond < 50)
			stepSeconds = 5.0;
		else if (pixelsPerSecond > 200)
			stepSeconds = 0.5;

		stepPx = stepSeconds * pixelsPerSecond;
	}

	// Draw vertical grid lines
	QPen gridPen(QColor(60, 60, 60), 1, Qt::DotLine);
	for (double x = 0; x < sceneWidth; x += stepPx) {
		// Skip x=0 (optional, but usually 0 is implied)
		if (x < 0.1)
			continue;

		QGraphicsLineItem *line = scene->addLine(x, 20, x, sceneHeight, gridPen);
		line->setZValue(-10); // Behind everything
		gridItems.append(line);
	}
}

void TimelineDock::onTick(double currentTimeMs)
{
	updatePlayhead(currentTimeMs);

#if OBS_TIMELINE_HAS_WEBSOCKET
	if (obsWebSocketVendor) {
		qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (now - lastTickTime > 50) {
			lastTickTime = now;
			obs_data_t *d = obs_data_create();
			obs_data_set_string(d, "event", "tick");
			obs_data_set_double(d, "timeMs", currentTimeMs);
			obs_websocket_vendor_emit_event(obsWebSocketVendor, "PlaybackStatus", d);
			obs_data_release(d);
		}
	}
#endif

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
	QString zoomPart;
	if (currentText.contains("|")) {
		zoomPart = " |" + currentText.split("|").last();
	}

	QString newText = baseText + zoomPart;

	// Optimization: Only update if text actually changed
	if (timeLabel->text() != newText) {
		timeLabel->setText(newText);
	}
}

void TimelineDock::updatePlayhead(double timeMs)
{
	if (!playheadItem || !view)
		return;
	double x = (timeMs / 1000.0) * pixelsPerSecond;
	double h = view->viewport()->height();
	playheadItem->setLine(x, 0, x, h);
	view->ensureVisible(QRectF(x, 0, 10, h), 50, 0);
}

void TimelineDock::onAddEventClicked()
{
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
	typeCombo->addItem("Trigger Timeline", static_cast<int>(TimelineEventType::TriggerTimeline));
	layout->addWidget(typeCombo);

	// Target label (updates based on type)
	QLabel *targetLabel = new QLabel("Target Scene:", &dialog);
	layout->addWidget(targetLabel);

	// Target combo (scenes, sources, or filters)
	// Target combo (scenes, sources, or filters)
	QComboBox *targetCombo = new QComboBox(&dialog);
	layout->addWidget(targetCombo);

	// Timeline File Selector (Hidden by default)
	QWidget *fileWidget = new QWidget(&dialog);
	QHBoxLayout *fileLayout = new QHBoxLayout(fileWidget);
	fileLayout->setContentsMargins(0, 0, 0, 0);
	QLineEdit *fileEdit = new QLineEdit(&dialog);
	QPushButton *browseButton = new QPushButton("Browse...", &dialog);
	fileLayout->addWidget(fileEdit);
	fileLayout->addWidget(browseButton);
	layout->addWidget(fileWidget);
	fileWidget->hide();

	connect(browseButton, &QPushButton::clicked, [&]() {
		QString path = QFileDialog::getOpenFileName(&dialog, "Select Timeline", "", "JSON Files (*.json)");
		if (!path.isEmpty())
			fileEdit->setText(path);
	});

	// Transition Selector (Hidden by default, used for Scene Switch)
	QWidget *transitionWidget = new QWidget(&dialog);
	QHBoxLayout *transitionLayout = new QHBoxLayout(transitionWidget);
	transitionLayout->setContentsMargins(0, 0, 0, 0);
	QLabel *transitionLabel = new QLabel("Transition:", &dialog);
	QComboBox *transitionCombo = new QComboBox(&dialog);
	transitionLayout->addWidget(transitionLabel);
	transitionLayout->addWidget(transitionCombo);
	layout->addWidget(transitionWidget);
	transitionWidget->hide();

	// Helper lambda to populate targets based on event type

	// Helper lambda to populate targets based on event type
	auto populateTargets = [&](int eventType) {
		targetCombo->clear();
		targetCombo->setEnabled(true);

		TimelineEventType evtType = static_cast<TimelineEventType>(eventType);

		if (evtType == TimelineEventType::TriggerTimeline) {
			targetLabel->setText("Timeline File:");
			targetCombo->hide();
			fileWidget->show();
			return;
		} else {
			targetCombo->show();
			fileWidget->hide();
		}

		// Handle Transition Widget Visibility
		if (evtType == TimelineEventType::SceneSwitch) {
			transitionWidget->show();
			transitionCombo->clear();
			transitionCombo->addItem("Current Transition (Default)", "");

			struct obs_frontend_source_list transitions = {};
			obs_frontend_get_transitions(&transitions);
			for (size_t i = 0; i < transitions.sources.num; i++) {
				obs_source_t *source = transitions.sources.array[i];
				const char *name = obs_source_get_name(source);
				transitionCombo->addItem(QString::fromUtf8(name));
			}
			obs_frontend_source_list_free(&transitions);
		} else {
			transitionWidget->hide();
		}

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
		} else if (evtType == TimelineEventType::SourceShow || evtType == TimelineEventType::SourceHide) {
			targetLabel->setText("Target Source:");
			obs_source_t *currentScene = obs_frontend_get_current_scene();
			if (currentScene) {
				obs_scene_t *scene = obs_scene_from_source(currentScene);
				if (scene) {
					obs_scene_enum_items(
						scene,
						[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
							QComboBox *combo = static_cast<QComboBox *>(param);
							obs_source_t *source = obs_sceneitem_get_source(item);
							const char *name = obs_source_get_name(source);
							combo->addItem(QString::fromUtf8(name));
							return true;
						},
						targetCombo);
				}
				obs_source_release(currentScene);
			}

			if (targetCombo->count() == 0) {
				targetCombo->addItem("(No sources in current scene)");
				targetCombo->setEnabled(false);
			}
		} else if (evtType == TimelineEventType::MediaStart || evtType == TimelineEventType::MediaStop) {
			targetLabel->setText("Target Media Source:");
			obs_source_t *currentScene = obs_frontend_get_current_scene();
			if (currentScene) {
				obs_scene_t *scene = obs_scene_from_source(currentScene);
				if (scene) {
					obs_scene_enum_items(
						scene,
						[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
							QComboBox *combo = static_cast<QComboBox *>(param);
							obs_source_t *source = obs_sceneitem_get_source(item);
							const char *id = obs_source_get_id(source);
							if (id && (strcmp(id, "ffmpeg_source") == 0 ||
								   strcmp(id, "vlc_source") == 0)) {
								const char *name = obs_source_get_name(source);
								combo->addItem(QString::fromUtf8(name));
							}
							return true;
						},
						targetCombo);
				}
				obs_source_release(currentScene);
			}

			if (targetCombo->count() == 0) {
				targetCombo->addItem("(No media sources in current scene)");
				targetCombo->setEnabled(false);
			}
		} else if (evtType == TimelineEventType::FilterEnable || evtType == TimelineEventType::FilterDisable) {
			targetLabel->setText("Target Filter (Source::Filter):");
			obs_source_t *currentScene = obs_frontend_get_current_scene();
			if (currentScene) {
				obs_scene_t *scene = obs_scene_from_source(currentScene);
				if (scene) {
					// Enumerate sources and their filters
					obs_scene_enum_items(
						scene,
						[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
							QComboBox *combo = static_cast<QComboBox *>(param);
							obs_source_t *source = obs_sceneitem_get_source(item);
							QString sourceName =
								QString::fromUtf8(obs_source_get_name(source));

							// Allocate once per source, free after enum completes
							auto *filterData =
								new std::pair<QComboBox *, QString>(combo, sourceName);
							obs_source_enum_filters(
								source,
								[](obs_source_t *, obs_source_t *filter, void *data) {
									auto *info = static_cast<
										std::pair<QComboBox *, QString> *>(
										data);
									QString filterName = QString::fromUtf8(
										obs_source_get_name(filter));
									QString displayName = QString("%1::%2")
												      .arg(info->second)
												      .arg(filterName);

									QVariantList eventData;
									eventData << info->second << filterName;
									info->first->addItem(displayName, eventData);
								},
								filterData);
							delete filterData;

							return true;
						},
						targetCombo);
				}
				obs_source_release(currentScene);
			}

			if (targetCombo->count() == 0) {
				targetCombo->addItem("(No filters found in current scene)");
				targetCombo->setEnabled(false);
			}
		} else if (evtType == TimelineEventType::HotkeyTrigger) {
			targetLabel->setText("Target Hotkey:");
			// Enumerate all registered hotkeys
			obs_enum_hotkeys(
				[](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
					Q_UNUSED(id);
					QComboBox *combo = static_cast<QComboBox *>(data);
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
				},
				targetCombo);

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
			colorButton->setStyleSheet(
				QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
		}
	});

	colorLayout->addWidget(colorLabel);
	colorLayout->addWidget(colorButton);

	// Custom Icon Path
	QLabel *iconLabel = new QLabel("Custom Icon:", &dialog);
	QLineEdit *iconPathEdit = new QLineEdit(&dialog);
	QPushButton *browseIconButton = new QPushButton("Browse...", &dialog);

	connect(browseIconButton, &QPushButton::clicked, [&]() {
		QString path = QFileDialog::getOpenFileName(&dialog, "Select Custom Icon", "", "SVG Files (*.svg)");
		if (!path.isEmpty()) {
			iconPathEdit->setText(path);
		}
	});

	QHBoxLayout *iconLayout = new QHBoxLayout();
	iconLayout->addWidget(iconLabel);
	iconLayout->addWidget(iconPathEdit);
	iconLayout->addWidget(browseIconButton);

	colorLayout->addSpacing(20);
	layout->addLayout(iconLayout);

	// Duration Controls
	QHBoxLayout *durationLayout = new QHBoxLayout();
	QCheckBox *durationCheck = new QCheckBox("Has Duration", &dialog);
	QDoubleSpinBox *durationSpin = new QDoubleSpinBox(&dialog);
	durationSpin->setRange(0.0, 3600.0); // Up to 1 hour
	durationSpin->setSuffix(" sec");
	durationSpin->setValue(5.0);
	durationSpin->setEnabled(false);

	connect(durationCheck, &QCheckBox::toggled, durationSpin, &QDoubleSpinBox::setEnabled);

	durationLayout->addWidget(durationCheck);
	durationLayout->addWidget(durationSpin);
	layout->addLayout(durationLayout);

	colorLayout->addStretch();
	layout->addLayout(
		colorLayout); // Add color layout before connecting type change? Or after? Doesn't matter for layout order, just logic.

	// Initial population
	populateTargets(static_cast<int>(initialType));

	// Update targets AND color when type changes
	connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&](int index) {
		TimelineEventType newType = static_cast<TimelineEventType>(typeCombo->itemData(index).toInt());
		populateTargets(static_cast<int>(newType));

		// Update color to default
		selectedColor = ThemeIcons::getDefaultColor(newType);
		colorButton->setStyleSheet(
			QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
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

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() == QDialog::Accepted) {
		TimelineEventType type = static_cast<TimelineEventType>(typeCombo->currentData().toInt());

		// Validate
		if (type == TimelineEventType::TriggerTimeline) {
			if (fileEdit->text().isEmpty()) {
				blog(LOG_WARNING, "Cannot create event: No timeline file selected");
				return;
			}
		} else {
			if (!targetCombo->isEnabled() || targetCombo->currentText().startsWith("(")) {
				blog(LOG_WARNING, "Cannot create event: No valid target selected");
				return;
			}
		}

		TimelineEvent evt;
		evt.timeMs = time;
		evt.type = type;

		// Populate fields based on type
		if (evt.type == TimelineEventType::TriggerTimeline) {
			evt.data = fileEdit->text();
			evt.sourceName = QFileInfo(fileEdit->text()).fileName();
		} else if (evt.type == TimelineEventType::SceneSwitch) {
			evt.sourceName = targetCombo->currentText();
			// Store transition name in data if selected
			if (!transitionCombo->currentData().toString().isEmpty() ||
			    !transitionCombo->currentText().isEmpty()) {
				// Skip if "Current Transition" (empty data/text check)
				if (transitionCombo->currentIndex() > 0) {
					evt.data = transitionCombo->currentText();
				}
			}
		} else if (evt.type == TimelineEventType::HotkeyTrigger) {
			evt.sourceName = targetCombo->currentData().toString(); // Internal hotkey name
		} else if (evt.type == TimelineEventType::FilterEnable ||
			   evt.type == TimelineEventType::FilterDisable) {
			QVariantList data = targetCombo->currentData().toList();
			if (data.size() >= 2) {
				evt.sourceName = data[0].toString();
				evt.filterName = data[1].toString();
			} else {
				// Fallback (shouldn't happen with new populateTargets)
				QStringList parts = targetCombo->currentText().split("::");
				if (parts.size() >= 2) {
					evt.sourceName = parts[0];
					evt.filterName = parts[1];
				}
			}
		} else {
			evt.sourceName = targetCombo->currentText();
		}

		evt.color = selectedColor;
		evt.customIconPath = iconPathEdit->text();
		evt.hasDuration = durationCheck->isChecked();
		evt.durationMs = durationSpin->value() * 1000.0;

		model->addEvent(evt);
	}
}

void TimelineDock::rebuildEventItems()
{
	rebuildScheduled = false;
	clearEventItems();

	const auto &events = model->getEvents();

	// Track stacking - count events at same time (within 50ms tolerance)
	std::map<int, int> timeSlotCount; // time bucket -> count
	const double STACK_TOLERANCE_MS = 50.0;

	for (size_t i = 0; i < events.size(); i++) {
		const auto &evt = events[i];
		int timeBucket = static_cast<int>(evt.timeMs / STACK_TOLERANCE_MS);
		int stackIndex = timeSlotCount[timeBucket]++;

		TimelineEventItem *item =
			new TimelineEventItem(evt.timeMs, pixelsPerSecond, evt.type, evt.color, evt.customIconPath);
		item->setEventId(evt.id);
		item->setStackOffset(stackIndex);
		item->setHasDuration(evt.hasDuration);
		item->setDuration(evt.durationMs);

		// Apply snap settings
		item->setSnapEnabled(snapEnabled);
		double stepMs = 100.0; // 100ms snap
		item->setSnapInterval(stepMs);

		scene->addItem(item);

		// Enhanced tooltip with event details
		QString typeStr;
		switch (evt.type) {
		case TimelineEventType::SceneSwitch:
			typeStr = "Scene Switch";
			break;
		case TimelineEventType::SourceShow:
			typeStr = "Source Show";
			break;
		case TimelineEventType::SourceHide:
			typeStr = "Source Hide";
			break;
		case TimelineEventType::MediaStart:
			typeStr = "Media Start";
			break;
		case TimelineEventType::MediaStop:
			typeStr = "Media Stop";
			break;
		case TimelineEventType::FilterEnable:
			typeStr = "Filter Enable";
			break;
		case TimelineEventType::FilterDisable:
			typeStr = "Filter Disable";
			break;
		case TimelineEventType::HotkeyTrigger:
			typeStr = "Hotkey Trigger";
			break;
		default:
			typeStr = "Unknown";
			break;
		}
		double timeSec = evt.timeMs / 1000.0;
		int minutes = static_cast<int>(timeSec) / 60;
		int seconds = static_cast<int>(timeSec) % 60;
		int ms = static_cast<int>(evt.timeMs) % 1000;
		QString targetDisplay = evt.sourceName;
		if (!evt.filterName.isEmpty()) {
			targetDisplay += "::" + evt.filterName;
		}

		QString tooltip =
			QString("%1\nTarget: %2\nTime: %3:%4.%5\n\nDouble-click to edit | Right-click for options")
				.arg(typeStr)
				.arg(targetDisplay)
				.arg(minutes, 2, 10, QChar('0'))
				.arg(seconds, 2, 10, QChar('0'))
				.arg(ms / 10, 2, 10, QChar('0'));
		item->setToolTip(tooltip);
		item->setEventLabel(
			evt.sourceName); // Just show source name on the bubble usually, or filter name? Source is probably better.
		eventItems.append(item);

		const QString eventId = evt.id;

		// Connect movement
		connect(item, &TimelineEventItem::eventMoved, [this, eventId](double newTime) {
			model->updateEventTimeById(eventId, newTime);
		});

		// Connect double-click
		connect(item, &TimelineEventItem::doubleClicked,
			[this, item, eventId]() { onEventDoubleClicked(item, eventId); });

		// Connect deletion
		connect(item, &TimelineEventItem::deleteRequested, [this, eventId]() {
			model->removeEventById(eventId);
			// Rebuild is handled by modelChanged -> queued rebuildEventItems
		});

		// Connect drag guide support
		connect(item, &TimelineEventItem::dragStarted, [this, item]() {
			if (dragGuideItem) {
				dragGuideItem->setVisible(true);
				// Initial positioning
				double x = (item->getTimeMs() / 1000.0) * pixelsPerSecond;
				dragGuideItem->setLine(x, 0, x, view->viewport()->height());
			}
		});

		connect(item, &TimelineEventItem::dragMoved, [this, item](double timeMs) {
			if (dragGuideItem) {
				double x = (timeMs / 1000.0) * pixelsPerSecond;
				dragGuideItem->setLine(x, 0, x, view->viewport()->height());
			}
		});

		// Ensure guide is hidden when move ends (eventMoved is triggered on release)
		connect(item, &TimelineEventItem::eventMoved, [this, item](double) {
			if (dragGuideItem) {
				dragGuideItem->setVisible(false);
			}
		});

		// Connect color change
		connect(item, &TimelineEventItem::colorChangeRequested, [this, item, eventId]() {
			QColor newColor = QColorDialog::getColor(item->getEventColor(), this, "Select Event Color");
			if (newColor.isValid()) {
				item->setEventColor(newColor);
				const TimelineEvent *original = model->findEventById(eventId);
				if (!original)
					return;
				TimelineEvent updatedEvt = *original;
				updatedEvt.color = newColor;
				model->replaceEventById(eventId, updatedEvt);
			}
		});
	}

	if (events.empty()) {
		if (!emptyHintItem) {
			emptyHintItem = scene->addSimpleText(
				"Double-click timeline or press Add Event to create your first event");
			emptyHintItem->setBrush(QBrush(Qt::gray));
			emptyHintItem->setFont(QFont("Arial", 10));
			emptyHintItem->setZValue(-5);
		}

		const double x = std::max(20.0, scene->sceneRect().width() * 0.1);
		emptyHintItem->setPos(x, 60);
		emptyHintItem->setVisible(true);
	} else if (emptyHintItem) {
		emptyHintItem->setVisible(false);
	}
}

void TimelineDock::clearEventItems()
{
	for (TimelineEventItem *item : eventItems) {
		if (item) {
			scene->removeItem(item);
			item->deleteLater();
		}
	}
	eventItems.clear();
}

void TimelineDock::onEventDoubleClicked(TimelineEventItem *item, const QString &eventId)
{
	const TimelineEvent *original = model->findEventById(eventId);
	if (!original)
		return;

	TimelineEvent evt = *original;
	EventEditor editor(evt, this);

	if (editor.exec() == QDialog::Accepted) {
		editor.getUpdatedEvent(evt);
		model->replaceEventById(eventId, evt);

		// Update visual item for immediate feedback
		if (item) {
			item->setHasDuration(evt.hasDuration);
			item->setDuration(evt.durationMs);
		}

		// Rebuild is handled by modelChanged -> queued rebuildEventItems
		save();
	}
}

void TimelineDock::updateZoomLabel()
{
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
