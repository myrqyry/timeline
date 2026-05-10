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
#include <QMenu>
#include <QWidgetAction>
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

	// Register Hotkeys
	hotkeyIdPlayPause = obs_hotkey_register_frontend("Timeline.PlayPause", "Timeline: Play/Pause",
							 TimelineDock::hotkeyPlayPause, this);
	hotkeyIdStop = obs_hotkey_register_frontend("Timeline.Stop", "Timeline: Stop", TimelineDock::hotkeyStop, this);
	hotkeyIdRestart = obs_hotkey_register_frontend("Timeline.Restart", "Timeline: Restart",
						       TimelineDock::hotkeyRestart, this);

	// Register theme change callback
	obs_frontend_add_event_callback(themeChangedCallback, this);

	// Initial Load
	blog(LOG_INFO, "TimelineDock loading initial data");
	load();

	blog(LOG_INFO, "TimelineDock constructor finished");
}

TimelineDock::~TimelineDock()
{
	obs_frontend_remove_event_callback(themeChangedCallback, this);
	obs_frontend_remove_save_callback(saveCallback, this);

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
	QMetaObject::invokeMethod(dock, "onRestartHotkey", Qt::QueuedConnection);
}

void TimelineDock::themeChangedCallback(enum obs_frontend_event event, void *private_data)
{
	if (event == OBS_FRONTEND_EVENT_THEME_CHANGED) {
		auto *dock = static_cast<TimelineDock *>(private_data);
		QMetaObject::invokeMethod(dock, "updateLayout", Qt::QueuedConnection);
	}
}

void TimelineDock::onRestartHotkey()
{
	scheduler->stop();
	scheduler->start();
	playButton->setText("Pause");
}

void TimelineDock::save()
{
	char *configPath = obs_module_get_config_path(obs_current_module(), "timeline.json");
	if (!configPath)
		return;

	const QJsonObject root = TimelineSerialization::toJsonObject(*model, loopCheckbox->isChecked(),
								     endTimeSpinbox->value(), snapCheckbox->isChecked(),
								     getSnapIntervalMs(), bpm, gridToBpm,
								     noteLengthBeats, centerControls);
	QString errorMessage;
	if (TimelineSerialization::saveToFile(configPath, root, &errorMessage)) {
		blog(LOG_INFO, "Saved timeline to %s", configPath);
	} else {
		blog(LOG_WARNING, "Failed to save timeline to %s: %s", configPath, errorMessage.toUtf8().constData());
	}
	bfree(configPath);
}

void TimelineDock::load()
{
	char *configPath = obs_module_get_config_path(obs_current_module(), "timeline.json");
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
			return;
		}

		loopCheckbox->setChecked(timelineDoc.loop);
		endTimeSpinbox->setValue(timelineDoc.endTimeSeconds);
		snapCheckbox->setChecked(timelineDoc.snap);
		manualSnapIntervalMs = timelineDoc.snapIntervalMs;
		bpmSpinbox->setValue(timelineDoc.bpm);
		gridTypeCombo->setCurrentIndex(timelineDoc.gridToBpm ? 1 : 0);
		noteLengthBeats = timelineDoc.noteLengthBeats;
		int noteIdx = noteLengthCombo->findData(noteLengthBeats);
		if (noteIdx >= 0)
			noteLengthCombo->setCurrentIndex(noteIdx);
		centerControlsCheckbox->setChecked(timelineDoc.centerControls);

		// onXxx() slots are already called via the signal/slot connections above
		// (setChecked/setValue triggers toggled/valueChanged signals)

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
	zoomOutButton = new QPushButton("-", this);
	zoomOutButton->setToolTip("Zoom out (Ctrl+Scroll)");
	zoomInButton = new QPushButton("+", this);
	zoomInButton->setToolTip("Zoom in (Ctrl+Scroll)");
	addEventButton = new QPushButton("Add Event", this);
	addEventButton->setToolTip("Add a new event at the current playhead position");

	connect(playButton, &QPushButton::clicked, this, &TimelineDock::onPlayClicked);
	connect(stopButton, &QPushButton::clicked, this, &TimelineDock::onStopClicked);
	connect(zoomOutButton, &QPushButton::clicked, this, &TimelineDock::onZoomOutClicked);
	connect(zoomInButton, &QPushButton::clicked, this, &TimelineDock::onZoomInClicked);
	connect(addEventButton, &QPushButton::clicked, this, &TimelineDock::onAddEventClicked);

	// Save/Load (will be added in updateControlsLayout)

	// Time Display
	timeLabel = new QLabel("00:00:00", this);
	timeLabel->setToolTip("Current timeline position | Zoom level");

	// Options
	snapCheckbox = new QCheckBox("Snap", this);
	snapCheckbox->setToolTip("Snap events to grid when dragging");
	connect(snapCheckbox, &QCheckBox::toggled, this, &TimelineDock::onSnapToggled);

	snapIntervalLabel = new QLabel("Interval:", this);
	snapIntervalSpinbox = new QDoubleSpinBox(this);
	snapIntervalSpinbox->setRange(0.05, 10.0);
	snapIntervalSpinbox->setValue(0.5);
	snapIntervalSpinbox->setDecimals(2);
	snapIntervalSpinbox->setSingleStep(0.05);
	snapIntervalSpinbox->setToolTip("Snap interval in seconds");
	connect(snapIntervalSpinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
		&TimelineDock::onSnapIntervalChanged);

	loopCheckbox = new QCheckBox("Loop", this);
	loopCheckbox->setToolTip("Loop playback when end time is reached");
	connect(loopCheckbox, &QCheckBox::toggled, this, &TimelineDock::onLoopToggled);

	centerControlsCheckbox = new QCheckBox("Center", this);
	centerControlsCheckbox->setToolTip("Center controls in the interface");
	connect(centerControlsCheckbox, &QCheckBox::toggled, this, &TimelineDock::onCenterControlsToggled);

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

	// Settings menu for advanced options
	settingsButton = new QPushButton("Settings", this);
	settingsButton->setToolTip("Show advanced settings (Center, BPM, End Time)");

	settingsMenu = new QMenu(settingsButton);

	// Embed centerControlsCheckbox in the menu
	QWidgetAction *centerAction = new QWidgetAction(settingsMenu);
	centerAction->setDefaultWidget(centerControlsCheckbox);
	settingsMenu->addAction(centerAction);

	// Grid mode selector (Time vs BPM)
	gridTypeCombo = new QComboBox(this);
	gridTypeCombo->addItem("Time", 0);
	gridTypeCombo->addItem("BPM", 1);
	gridTypeCombo->setToolTip("Grid snapping mode: Time (seconds) or BPM (musical)");
	connect(gridTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimelineDock::onGridModeChanged);

	QWidgetAction *gridTypeAction = new QWidgetAction(settingsMenu);
	gridTypeAction->setDefaultWidget(gridTypeCombo);
	settingsMenu->addAction(gridTypeAction);

	// Note length selector (for BPM mode)
	noteLengthCombo = new QComboBox(this);
	noteLengthCombo->addItem("Whole", 4.0);
	noteLengthCombo->addItem("Half", 2.0);
	noteLengthCombo->addItem("Quarter", 1.0);
	noteLengthCombo->addItem("Eighth", 0.5);
	noteLengthCombo->addItem("Sixteenth", 0.25);
	noteLengthCombo->addItem("32nd", 0.125);
	noteLengthCombo->setToolTip("Note duration for BPM snapping");
	noteLengthCombo->setCurrentIndex(2); // Default: quarter note
	connect(noteLengthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimelineDock::onNoteLengthChanged);

	QWidgetAction *noteLengthAction = new QWidgetAction(settingsMenu);
	noteLengthAction->setDefaultWidget(noteLengthCombo);
	settingsMenu->addAction(noteLengthAction);

	// Initially hidden; shown when BPM mode is active
	noteLengthCombo->setVisible(false);

	// Embed BPM spinbox
	QWidgetAction *bpmSpinAction = new QWidgetAction(settingsMenu);
	bpmSpinAction->setDefaultWidget(bpmSpinbox);
	settingsMenu->addAction(bpmSpinAction);

	settingsMenu->addSeparator();

	// Embed End label and spinbox
	QWidgetAction *endLabelAction = new QWidgetAction(settingsMenu);
	endLabelAction->setDefaultWidget(endLabel);
	settingsMenu->addAction(endLabelAction);

	QWidgetAction *endTimeAction = new QWidgetAction(settingsMenu);
	endTimeAction->setDefaultWidget(endTimeSpinbox);
	settingsMenu->addAction(endTimeAction);

	connect(settingsButton, &QPushButton::clicked, this,
		[this]() { settingsMenu->exec(settingsButton->mapToGlobal(QPoint(0, settingsButton->height()))); });

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
	playheadItem = scene->addLine(0, 0, 0, view->height(), QPen(palette().color(QPalette::Highlight), 2));
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
	setUpdatesEnabled(false); // Prevent flicker from hide/show cycle

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

	if (centerControls)
		controlsLayout->addStretch();

	QList<QWidget *> leftWidgets = {playButton,     stopButton, zoomOutButton, zoomInButton,
					addEventButton, saveButton, loadButton,    timeLabel};
	for (QWidget *w : leftWidgets) {
		controlsLayout->addWidget(show(w));
	}

	controlsLayout->addStretch();

	QList<QWidget *> rightWidgets;
	rightWidgets.append(snapCheckbox);
	if (!gridToBpm) {
		rightWidgets.append(snapIntervalLabel);
		rightWidgets.append(snapIntervalSpinbox);
	}
	rightWidgets.append(loopCheckbox);
	rightWidgets.append(settingsButton);
	for (QWidget *w : rightWidgets) {
		controlsLayout->addWidget(show(w));
	}

	setUpdatesEnabled(true); // Re-enable updates after layout change
}

void TimelineDock::onZoomInClicked()
{
	pixelsPerSecond *= 1.25;
	if (pixelsPerSecond > 1000.0)
		pixelsPerSecond = 1000.0;
	updateLayout();
	updateZoomLabel();
}

void TimelineDock::onZoomOutClicked()
{
	pixelsPerSecond /= 1.25;
	if (pixelsPerSecond < 5.0)
		pixelsPerSecond = 5.0;
	updateLayout();
	updateZoomLabel();
}

void TimelineDock::onSnapIntervalChanged(double value)
{
	if (!gridToBpm) {
		manualSnapIntervalMs = value * 1000.0;
	}
	if (snapEnabled && !gridToBpm) {
		for (TimelineEventItem *item : eventItemMap) {
			if (item) {
				item->setSnapInterval(manualSnapIntervalMs);
			}
		}
	}
}

void TimelineDock::onGridModeChanged(int index)
{
	gridToBpm = (index == 1);

	// Show/hide note length selector in menu
	noteLengthCombo->setVisible(gridToBpm);

	// Update main toolbar layout (shows/hides snap interval controls)
	updateControlsLayout();

	// If switching to Time mode, restore manual interval in spinbox
	if (!gridToBpm) {
		snapIntervalSpinbox->setValue(manualSnapIntervalMs / 1000.0);
	}

	applySnapIntervalToItems();
	drawGrid();
}

void TimelineDock::onBpmChanged(double value)
{
	bpm = value;
	if (gridToBpm) {
		applySnapIntervalToItems();
		drawGrid();
	}
}

void TimelineDock::onNoteLengthChanged(int index)
{
	QVariant data = noteLengthCombo->itemData(index);
	bool ok;
	double beats = data.toDouble(&ok);
	if (ok) {
		noteLengthBeats = beats;
		if (gridToBpm) {
			applySnapIntervalToItems();
			drawGrid();
		}
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

	// 2. Update event item positions and theme colors for zoom/resize
	QColor borderColor = palette().color(QPalette::Text);
	QColor selectionColor = palette().color(QPalette::Highlight);
	QColor bgColor = palette().color(QPalette::Base);
	for (TimelineEventItem *item : eventItemMap) {
		if (item) {
			item->setSuppressDragSignals(true);
			item->setPixelsPerSecond(pixelsPerSecond);
			item->setBorderColor(borderColor);
			item->setSelectionColor(selectionColor);
			item->setBackgroundColor(bgColor);
			item->setSuppressDragSignals(false);
		}
	}

	// 3. Redraw ruler and grid
	drawRuler();
	drawGrid();
	updateEndMarker();

	// 4. Update playhead
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

	clearRuler();

	double durationSec = scene->sceneRect().width() / pixelsPerSecond;

	int step = 1;
	if (pixelsPerSecond < 50)
		step = 5;
	if (pixelsPerSecond < 10)
		step = 10;

	QColor rulerColor = palette().color(QPalette::Mid);
	QColor textColor = palette().color(QPalette::Text);
	QColor minorColor = palette().color(QPalette::Dark);

	for (int i = 0; i <= durationSec; i += step) {
		double x = i * pixelsPerSecond;

		QGraphicsLineItem *line = scene->addLine(x, 0, x, 20, QPen(rulerColor));
		rulerItems.append(line);

		QGraphicsSimpleTextItem *text = scene->addSimpleText(QString::number(i) + "s");
		text->setPos(x + 2, 0);
		text->setBrush(QBrush(textColor));
		text->setFont(QFont("Arial", 8));
		rulerItems.append(text);

		if (pixelsPerSecond > 50) {
			for (int j = 1; j < 10; j++) {
				double xm = x + (j * pixelsPerSecond / 10.0);
				QGraphicsLineItem *minor = scene->addLine(xm, 10, xm, 20, QPen(minorColor));
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

				// Not on an item - clear selection and scrub
				for (TimelineEventItem *item : eventItemMap) {
					if (item)
						item->setSelected(false);
				}

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

double TimelineDock::getSnapIntervalMs() const
{
	if (gridToBpm && bpm > 0.0) {
		return (60.0 / bpm) * noteLengthBeats * 1000.0;
	}
	return snapIntervalSpinbox->value() * 1000.0;
}

void TimelineDock::applySnapIntervalToItems()
{
	double intervalMs = getSnapIntervalMs();
	for (TimelineEventItem *item : eventItemMap) {
		if (item) {
			item->setSnapEnabled(snapEnabled);
			item->setSnapInterval(intervalMs);
		}
	}
}

void TimelineDock::onSnapToggled(bool checked)
{
	snapEnabled = checked;
	applySnapIntervalToItems();
}

void TimelineDock::onSaveClicked()
{
	QString fileName = QFileDialog::getSaveFileName(this, "Save Timeline", "", "JSON Files (*.json)");
	if (fileName.isEmpty())
		return;

	if (!fileName.endsWith(".json", Qt::CaseInsensitive)) {
		fileName += ".json";
	}

	const QJsonObject root = TimelineSerialization::toJsonObject(*model, loopCheckbox->isChecked(),
								     endTimeSpinbox->value(), snapCheckbox->isChecked(),
								     getSnapIntervalMs(), bpm, gridToBpm,
								     noteLengthBeats, centerControls);
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
		manualSnapIntervalMs = timelineDoc.snapIntervalMs;
		bpmSpinbox->setValue(timelineDoc.bpm);
		gridTypeCombo->setCurrentIndex(timelineDoc.gridToBpm ? 1 : 0);
		noteLengthBeats = timelineDoc.noteLengthBeats;
		int noteIdx = noteLengthCombo->findData(noteLengthBeats);
		if (noteIdx >= 0)
			noteLengthCombo->setCurrentIndex(noteIdx);
		centerControlsCheckbox->setChecked(timelineDoc.centerControls);

		// onXxx() slots are already called via the signal/slot connections above
		// (setChecked/setValue triggers toggled/valueChanged signals)

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
		endMarkerItem = scene->addLine(
			x, 0, x, h, QPen(palette().color(QPalette::Link), 2, Qt::DashLine)); // Accent dashed line
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
	// Space, Delete, Home, Copy, Paste handled by QShortcut in setupUi()
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

void TimelineDock::copySelectedEvents()
{
	clipboard.clear();

	// Find selected items
	QList<TimelineEventItem *> selectedItems;
	for (TimelineEventItem *item : eventItemMap) {
		if (item && item->isSelected()) {
			selectedItems.append(item);
		}
	}

	if (selectedItems.isEmpty())
		return;

	// Get corresponding events from model
	for (TimelineEventItem *item : selectedItems) {
		auto evt = model->findEventById(item->getEventId());
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
	for (TimelineEventItem *item : eventItemMap) {
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
	save();
}

void TimelineDock::deleteSelectedEvents()
{
	QList<TimelineEventItem *> toDelete;

	for (TimelineEventItem *item : eventItemMap) {
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
	for (TimelineEventItem *item : eventItemMap) {
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
		// Calculate beat interval in seconds, scaled by note length
		double beatIntervalSec = (60.0 / bpm) * noteLengthBeats;
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
	QColor gridColor = palette().color(QPalette::Mid);
	QPen gridPen(gridColor, 1, Qt::DotLine);
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

	QString newText = baseText + zoomLabelCache;

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
	playheadItem->setPen(QPen(palette().color(QPalette::Highlight), 2));

	// Only auto-scroll when playhead is near an edge
	QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
	if (x < visible.left() + 20.0 || x > visible.right() - 20.0) {
		view->ensureVisible(QRectF(x, 0, 10, h), 50, 0);
	}
}

void TimelineDock::onAddEventClicked()
{
	const double currentTimeMs = scheduler ? scheduler->getCurrentTime() : 0.0;

	EventEditor editor(currentTimeMs, this);
	if (editor.exec() != QDialog::Accepted)
		return;

	TimelineEvent evt;
	evt.timeMs = currentTimeMs;

	editor.getUpdatedEvent(evt);

	// Protect against editor regressions.
	evt.timeMs = currentTimeMs;

	if (evt.color == QColor(0, 200, 255)) {
		evt.color = ThemeIcons::getDefaultColor(evt.type);
	}

	model->addEvent(evt);
}

void TimelineDock::connectItemSignals(TimelineEventItem *item, const QString &eventId)
{
	connect(item, &TimelineEventItem::dragStarted, [this, item]() {
		isDragging = true;
		if (dragGuideItem) {
			dragGuideItem->setVisible(true);
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

	connect(item, &TimelineEventItem::eventMoved, [this, item, eventId](double newTime) {
		isDragging = false;
		if (dragGuideItem) {
			dragGuideItem->setVisible(false);
		}
		model->updateEventTimeById(eventId, newTime);
	});

	connect(item, &TimelineEventItem::doubleClicked,
		[this, item, eventId]() { onEventDoubleClicked(item, eventId); });

	connect(item, &TimelineEventItem::deleteRequested, [this, eventId]() { model->removeEventById(eventId); });

	connect(item, &TimelineEventItem::colorChangeRequested, [this, item, eventId]() {
		QColor newColor = QColorDialog::getColor(item->getEventColor(), this, "Select Event Color");
		if (newColor.isValid()) {
			item->setEventColor(newColor);
			auto original = model->findEventById(eventId);
			if (!original)
				return;
			TimelineEvent updatedEvt = *original;
			updatedEvt.color = newColor;
			model->replaceEventById(eventId, updatedEvt);
		}
	});
}

void TimelineDock::rebuildEventItems()
{
	rebuildScheduled = false;
	if (isDragging) {
		// Re-queue for after drag ends
		rebuildScheduled = true;
		QMetaObject::invokeMethod(this, &TimelineDock::rebuildEventItems, Qt::QueuedConnection);
		return;
	}

	const auto &events = model->getEvents();

	// Build set of current IDs for stale-item removal
	QSet<QString> currentIds;
	currentIds.reserve(static_cast<int>(events.size()));

	// Track stacking - count events at same time (within 50ms tolerance)
	std::map<int, int> timeSlotCount;
	const double STACK_TOLERANCE_MS = 50.0;

	for (size_t i = 0; i < events.size(); i++) {
		const auto &evt = events[i];
		currentIds.insert(evt.id);

		int timeBucket = static_cast<int>(evt.timeMs / STACK_TOLERANCE_MS);
		int stackIndex = timeSlotCount[timeBucket]++;

		QColor borderColor = palette().color(QPalette::Text);
		QColor selectionColor = palette().color(QPalette::Highlight);
		QColor bgColor = palette().color(QPalette::Base);

		TimelineEventItem *item = eventItemMap.value(evt.id, nullptr);
		if (!item) {
			// Create new item
			item = new TimelineEventItem(evt.timeMs, pixelsPerSecond, evt.type, evt.color,
						     evt.customIconPath);
			item->setEventId(evt.id);
			item->setBorderColor(borderColor);
			item->setSelectionColor(selectionColor);
			item->setBackgroundColor(bgColor);
			scene->addItem(item);
			connectItemSignals(item, evt.id);
			eventItemMap[evt.id] = item;
		} else {
			// Update existing item properties
			item->setSuppressDragSignals(true);
			item->setPixelsPerSecond(pixelsPerSecond);
			item->setTimeMs(evt.timeMs);
			item->setStackOffset(stackIndex);
			item->setSuppressDragSignals(false);
			item->setEventType(evt.type);
			item->setEventColor(evt.color);
			item->setCustomIconPath(evt.customIconPath);
			item->setBorderColor(borderColor);
			item->setSelectionColor(selectionColor);
			item->setBackgroundColor(bgColor);
		}
		item->setStackOffset(stackIndex);

		item->setHasDuration(evt.hasDuration);
		item->setDuration(evt.durationMs);
		item->setSnapEnabled(snapEnabled);
		item->setSnapInterval(getSnapIntervalMs());

		// Tooltip and label
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
		case TimelineEventType::TriggerTimeline:
			typeStr = "Trigger Timeline";
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
		item->setEventLabel(evt.sourceName);
	}

	// Remove items no longer in the model
	auto it = eventItemMap.begin();
	while (it != eventItemMap.end()) {
		if (!currentIds.contains(it.key())) {
			if (it.value()) {
				scene->removeItem(it.value());
				delete it.value();
			}
			it = eventItemMap.erase(it);
		} else {
			++it;
		}
	}

	// Empty hint
	if (events.empty()) {
		if (!emptyHintItem) {
			emptyHintItem = scene->addSimpleText(
				"Double-click timeline or press Add Event to create your first event");
			QColor hintColor = palette().color(QPalette::Text);
			hintColor.setAlpha(128);
			emptyHintItem->setBrush(QBrush(hintColor));
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
	for (TimelineEventItem *item : eventItemMap) {
		if (item) {
			scene->removeItem(item);
			delete item;
		}
	}
	eventItemMap.clear();
}

void TimelineDock::onEventDoubleClicked(TimelineEventItem *item, const QString &eventId)
{
	Q_UNUSED(item);
	auto original = model->findEventById(eventId);
	if (!original)
		return;

	TimelineEvent evt = *original;
	EventEditor editor(evt, this);

	if (editor.exec() == QDialog::Accepted) {
		editor.getUpdatedEvent(evt);
		evt.id = eventId;              // Preserve ID
		evt.timeMs = original->timeMs; // Preserve time
		model->replaceEventById(eventId, evt);
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

	zoomLabelCache = " | " + zoomText;

	QString currentText = timeLabel->text();
	if (currentText.contains("|")) {
		currentText = currentText.split("|").first().trimmed();
	}
	timeLabel->setText(currentText + zoomLabelCache);
}
