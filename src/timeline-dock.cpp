#include "timeline-dock.h"
#include <obs-module.h>

#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QResizeEvent>
#include <obs-frontend-api.h>
#if OBS_TIMELINE_HAS_WEBSOCKET
#include <obs-websocket-api.h>
#endif
#include <QDateTime>

#include <QDir>
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
#include <algorithm>
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
#include <QScrollBar>
#include <QMenu>
#include <QWidgetAction>
#include "event-editor.h"
#include "timeline-serialization.h"

// Stepped zoom levels for clean, professional increments
static const std::vector<double> ZOOM_LEVELS = {5.0,   10.0,  15.0,  20.0,  30.0,  50.0,  75.0,
						100.0, 150.0, 200.0, 300.0, 500.0, 750.0, 1000.0};

TimelineDock::TimelineDock(QWidget *parent) : QWidget(parent)
{
	blog(LOG_INFO, "[Timeline] Dock constructor started");
	model = new TimelineModel(this);
	scheduler = new TimelineScheduler(model, this);
	executor = new ObsExecutor(this);

	setupUi();

	connect(scheduler, &TimelineScheduler::tick, this, &TimelineDock::onTick);
	connect(scheduler, &TimelineScheduler::eventStarted, executor, &ObsExecutor::executeEventStart);
	connect(scheduler, &TimelineScheduler::eventEnded, executor, &ObsExecutor::executeEventEnd);
	connect(scheduler, &TimelineScheduler::finished, this, &TimelineDock::onTimelineFinished);
	connect(scheduler, &TimelineScheduler::looped, this, &TimelineDock::onTimelineLooped);
	debounceSaveTimer = new QTimer(this);
	debounceSaveTimer->setSingleShot(true);
	debounceSaveTimer->setInterval(250);
	connect(debounceSaveTimer, &QTimer::timeout, this, &TimelineDock::save);
	connect(model, &TimelineModel::modelChanged, this, [this]() {
		if (!rebuildScheduled) {
			rebuildScheduled = true;
			QMetaObject::invokeMethod(this, &TimelineDock::rebuildEventItems, Qt::QueuedConnection);
		}
		if (!loadingTimeline && !savePending) {
			savePending = true;
			QMetaObject::invokeMethod(
				this,
				[this]() {
					savePending = false;
				save();
				},
				Qt::QueuedConnection);
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

	// Register frontend event callback
	obs_frontend_add_event_callback(frontendEventCallback, this);

	// Initial Load
	blog(LOG_INFO, "[Timeline] ├─ Loading data");
	load();

	blog(LOG_INFO, "[Timeline] └─ Dock ready");
}

TimelineDock::~TimelineDock()
{
	blog(LOG_INFO, "[Timeline] Cleaning up dock");
	save();
	obs_frontend_remove_event_callback(frontendEventCallback, this);
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

void TimelineDock::frontendEventCallback(enum obs_frontend_event event, void *private_data)
{
	auto *dock = static_cast<TimelineDock *>(private_data);

	switch (event) {
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		QMetaObject::invokeMethod(
			dock,
			[dock]() {
				dock->playheadColor = dock->palette().color(QPalette::Highlight).lighter(130);
				dock->updateToolbarIcons();
				dock->updateLayout();
			},
			Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		QMetaObject::invokeMethod(
			dock,
			[dock]() {
				dock->save();
				obs_frontend_save();
			},
			Qt::QueuedConnection);
		break;
	default:
		break;
	}
}

void TimelineDock::onRestartHotkey()
{
	scheduler->stop();
	scheduler->start();
	updatePlayButtonIcon();
}

void TimelineDock::save()
{
	char *configPath = obs_module_get_config_path(obs_current_module(), "timeline.json");
	if (!configPath)
		return;
	if (loadingTimeline) {
		bfree(configPath);
		return;
	}

	const QJsonObject root = TimelineSerialization::toJsonObject(*model, loopCheckbox->isChecked(),
								     endTimeSpinbox->value(), snapCheckbox->isChecked(),
								     getSnapIntervalMs(), bpm, gridToBpm,
								     noteLengthBeats, centerControls, alternatingBg,
								     scheduler->getCurrentTime(), pixelsPerSecond);
	QString errorMessage;
	if (TimelineSerialization::saveToFile(configPath, root, &errorMessage)) {
		blog(LOG_INFO, "[Timeline] Saved → %s", configPath);
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Save failed");
		blog(LOG_WARNING, "[Timeline] ├─ Path   : %s", configPath);
		blog(LOG_WARNING, "[Timeline] └─ Detail : %s", errorMessage.toUtf8().constData());
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
	const QFileInfo configInfo(configPath);
	if (TimelineSerialization::loadFromFile(configPath, doc, &errorMessage)) {
		blog(LOG_INFO, "[Timeline] Loading config: %s", configPath);
		TimelineDocument timelineDoc;
		loadingTimeline = true;
		model->blockSignals(true);
		if (!TimelineSerialization::fromJsonDocument(doc, *model, timelineDoc, &errorMessage)) {
			model->blockSignals(false);
			loadingTimeline = false;
			blog(LOG_WARNING, "[Timeline] ┌─ Config parse failed");
			blog(LOG_WARNING, "[Timeline] ├─ Path   : %s", configPath);
			blog(LOG_WARNING, "[Timeline] └─ Detail : %s", errorMessage.toUtf8().constData());
			bfree(configPath);
			return;
		}

		loopCheckbox->setChecked(timelineDoc.loop);
		endTimeSpinbox->setValue(timelineDoc.endTimeSeconds);
		snapCheckbox->setChecked(timelineDoc.snap);
		manualSnapIntervalMs = timelineDoc.snapIntervalMs;
		for (int i = 0; i < snapIntervalCombo->count(); i++) {
			if (snapIntervalCombo->itemData(i).toDouble() == manualSnapIntervalMs) {
				snapIntervalCombo->setCurrentIndex(i);
				break;
			}
		}
		bpmSpinbox->setValue(timelineDoc.bpm);
		gridTypeCombo->setCurrentIndex(timelineDoc.gridToBpm ? 1 : 0);
		noteLengthBeats = timelineDoc.noteLengthBeats;
		int noteIdx = noteLengthCombo->findData(noteLengthBeats);
		if (noteIdx >= 0)
			noteLengthCombo->setCurrentIndex(noteIdx);
		centerControlsCheckbox->setChecked(timelineDoc.centerControls);
		alternatingBg = timelineDoc.alternatingBg;
		alternatingBgCheckbox->blockSignals(true);
		alternatingBgCheckbox->setChecked(alternatingBg);
		alternatingBgCheckbox->blockSignals(false);
		pixelsPerSecond = timelineDoc.zoomPixelsPerSecond;
		scheduler->seek(timelineDoc.currentTimeMs);
		model->blockSignals(false);
		loadingTimeline = false;

		// onXxx() slots are already called via the signal/slot connections above
		// (setChecked/setValue triggers toggled/valueChanged signals)

		updateLayout();
		blog(LOG_INFO, "[Timeline] Loaded → %s", configPath);
	} else if (!configInfo.exists()) {
		QDir().mkpath(configInfo.absolutePath());
		const QJsonObject root = TimelineSerialization::toJsonObject(
			*model, loopCheckbox->isChecked(), endTimeSpinbox->value(), snapCheckbox->isChecked(),
			getSnapIntervalMs(), bpm, gridToBpm, noteLengthBeats, centerControls, alternatingBg,
			scheduler->getCurrentTime(), pixelsPerSecond);
		if (TimelineSerialization::saveToFile(configPath, root, &errorMessage)) {
			blog(LOG_INFO, "[Timeline] Default config created: %s", configPath);
		} else {
			blog(LOG_WARNING, "[Timeline] ┌─ Default config creation failed");
			blog(LOG_WARNING, "[Timeline] ├─ Path   : %s", configPath);
			blog(LOG_WARNING, "[Timeline] └─ Detail : %s", errorMessage.toUtf8().constData());
		}
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Config load failed");
		blog(LOG_WARNING, "[Timeline] ├─ Path   : %s", configPath);
		blog(LOG_WARNING, "[Timeline] └─ Detail : %s", errorMessage.toUtf8().constData());
	}
	bfree(configPath);
}

void TimelineDock::setupUi()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	// Compute playhead color (lighter version of highlight)
	playheadColor = palette().color(QPalette::Highlight).lighter(130);

	// Unified Controls Row
	controlsLayout = new QHBoxLayout();

	settingsButton = new QPushButton(this);
	settingsButton->setToolTip(obs_module_text("TimelineDock.Settings"));
	auto createSeparator = [this]() {
		QFrame *line = new QFrame(this);
		line->setFrameShape(QFrame::VLine);
		line->setFrameShadow(QFrame::Sunken);
		return line;
	};
	settingsSeparator = createSeparator();
	playbackSeparator = createSeparator();
	timeSeparator = createSeparator();
	zoomSeparator = createSeparator();
	addSeparator = createSeparator();
	modeSeparator = createSeparator();
	snapSeparator = createSeparator();

	// Playback Controls
	playButton = new QPushButton(obs_module_text("TimelineDock.Play"), this);
	playButton->setToolTip(obs_module_text("TimelineDock.StartPlayback"));
	stopButton = new QPushButton(obs_module_text("TimelineDock.Stop"), this);
	stopButton->setToolTip(obs_module_text("TimelineDock.StopPlayback"));
	zoomOutButton = new QPushButton("-", this);
	zoomOutButton->setToolTip(obs_module_text("TimelineDock.ZoomOut"));
	zoomInButton = new QPushButton("+", this);
	addEventButton = new QPushButton(obs_module_text("TimelineDock.AddEvent"), this);
	addEventButton->setToolTip(obs_module_text("TimelineDock.AddEventTooltip"));

	connect(playButton, &QPushButton::clicked, this, &TimelineDock::onPlayClicked);
	connect(stopButton, &QPushButton::clicked, this, &TimelineDock::onStopClicked);
	connect(zoomOutButton, &QPushButton::clicked, this, &TimelineDock::onZoomOutClicked);
	connect(zoomInButton, &QPushButton::clicked, this, &TimelineDock::onZoomInClicked);
	connect(addEventButton, &QPushButton::clicked, this, &TimelineDock::onAddEventClicked);

	// Time Display
	timeLabel = new QLabel("00:00:00", this);
	timeLabel->setToolTip("Current timeline position | Zoom level");
	QFont timeFont = timeLabel->font();
	timeFont.setPointSize(28);
	timeFont.setBold(true);
	timeLabel->setFont(timeFont);

	// Zoom label (overlay on viewport)
	zoomLabel = new QLabel(this);
	zoomLabel->setToolTip("Current zoom level");
	zoomLabel->setStyleSheet(
		"QLabel { background-color: palette(window); color: palette(text); padding: 2px 6px; border-radius: 3px; }");

	// Hover time label (overlay on viewport, shows time at cursor)
	hoverTimeLabel = new QLabel(this);
	hoverTimeLabel->setStyleSheet(
		"QLabel { background-color: palette(window); color: palette(text); padding: 2px 6px; border-radius: 3px; }");
	QFont hoverFont = hoverTimeLabel->font();
	hoverFont.setPointSize(11);
	hoverFont.setBold(true);
	hoverTimeLabel->setFont(hoverFont);
	hoverTimeLabel->hide();

	// Options
	snapCheckbox = new QPushButton(this);
	snapCheckbox->setCheckable(true);
	snapCheckbox->setToolTip(obs_module_text("TimelineDock.SnapTooltip"));
	connect(snapCheckbox, &QPushButton::toggled, this, &TimelineDock::onSnapToggled);

	snapIntervalCombo = new QComboBox(this);
	snapIntervalCombo->addItem("100 ms", 100);
	snapIntervalCombo->addItem("250 ms", 250);
	snapIntervalCombo->addItem("500 ms", 500);
	snapIntervalCombo->setCurrentIndex(2); // default: 500 ms
	snapIntervalCombo->setToolTip("Snap interval");
	connect(snapIntervalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimelineDock::onSnapIntervalChanged);

	loopCheckbox = new QPushButton(this);
	loopCheckbox->setCheckable(true);
	loopCheckbox->setToolTip(obs_module_text("TimelineDock.LoopTooltip"));
	connect(loopCheckbox, &QPushButton::toggled, this, &TimelineDock::onLoopToggled);

	QString iconToggleStyle = "QPushButton { padding: 4px; }"
				  "QPushButton:checked { background-color: palette(highlight); "
				  "color: palette(highlighted-text); }";
	snapCheckbox->setStyleSheet(iconToggleStyle);
	loopCheckbox->setStyleSheet(iconToggleStyle);

	centerControlsCheckbox = new QCheckBox(obs_module_text("TimelineDock.CenterControls"), this);
	centerControlsCheckbox->setToolTip("Center controls in the interface");
	connect(centerControlsCheckbox, &QCheckBox::toggled, this, &TimelineDock::onCenterControlsToggled);

	bpmSpinbox = new QDoubleSpinBox(this);
	bpmSpinbox->setRange(10, 300);
	bpmSpinbox->setValue(120);
	bpmSpinbox->setSuffix(" bpm");
	bpmSpinbox->setToolTip("Beats Per Minute");
	connect(bpmSpinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TimelineDock::onBpmChanged);

	endTimeSpinbox = new QDoubleSpinBox(this);
	endTimeSpinbox->setRange(0, 3600); // Up to 1 hour
	endTimeSpinbox->setValue(0);
	endTimeSpinbox->setDecimals(1);
	endTimeSpinbox->setSingleStep(1.0);
	endTimeSpinbox->setSuffix(" sec");
	endTimeSpinbox->setToolTip(obs_module_text("TimelineDock.EndTimeTooltip"));
	connect(endTimeSpinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
		&TimelineDock::onEndTimeChanged);

	saveButton = new QPushButton(obs_module_text("TimelineDock.Save"), this);
	saveButton->setToolTip(obs_module_text("TimelineDock.SaveTooltip"));
	saveButton->hide();
	connect(saveButton, &QPushButton::clicked, this, &TimelineDock::onSaveClicked);

	loadButton = new QPushButton(obs_module_text("TimelineDock.Load"), this);
	loadButton->setToolTip(obs_module_text("TimelineDock.LoadTooltip"));
	loadButton->hide();
	connect(loadButton, &QPushButton::clicked, this, &TimelineDock::onLoadClicked);

	endLabel = new QLabel(obs_module_text("TimelineDock.EndTime"), this);

	settingsMenu = new QMenu(settingsButton);
	settingsMenu->addAction(obs_module_text("TimelineDock.SaveTimeline"), this, &TimelineDock::onSaveClicked);
	settingsMenu->addAction(obs_module_text("TimelineDock.LoadTimeline"), this, &TimelineDock::onLoadClicked);
	settingsMenu->addSeparator();

	// Embed centerControlsCheckbox in the menu
	QWidgetAction *centerAction = new QWidgetAction(settingsMenu);
	centerAction->setDefaultWidget(centerControlsCheckbox);
	settingsMenu->addAction(centerAction);

	// Alternating background checkbox
	alternatingBgCheckbox = new QCheckBox(obs_module_text("TimelineDock.AltBackground"), this);
	alternatingBgCheckbox->setToolTip(obs_module_text("TimelineDock.AltBackgroundTooltip"));
	connect(alternatingBgCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
		alternatingBg = checked;
		drawGrid();
		debounceSaveTimer->start();
	});
	QWidgetAction *altBgAction = new QWidgetAction(settingsMenu);
	altBgAction->setDefaultWidget(alternatingBgCheckbox);
	settingsMenu->addAction(altBgAction);

	// Grid mode selector (Time vs BPM)
	gridTypeCombo = new QComboBox(this);
	gridTypeCombo->addItem(obs_module_text("TimelineDock.GridTime"), 0);
	gridTypeCombo->addItem(obs_module_text("TimelineDock.GridBPM"), 1);
	gridTypeCombo->setToolTip(obs_module_text("TimelineDock.GridTooltip"));
	connect(gridTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimelineDock::onGridModeChanged);

	QWidgetAction *gridTypeAction = new QWidgetAction(settingsMenu);
	gridTypeAction->setDefaultWidget(gridTypeCombo);
	settingsMenu->addAction(gridTypeAction);

	// Note length selector (for BPM mode)
	noteLengthCombo = new QComboBox(this);
	noteLengthCombo->addItem(obs_module_text("TimelineDock.NoteWhole"), 4.0);
	noteLengthCombo->addItem(obs_module_text("TimelineDock.NoteHalf"), 2.0);
	noteLengthCombo->addItem(obs_module_text("TimelineDock.NoteQuarter"), 1.0);
	noteLengthCombo->addItem(obs_module_text("TimelineDock.NoteEighth"), 0.5);
	noteLengthCombo->addItem(obs_module_text("TimelineDock.NoteSixteenth"), 0.25);
	noteLengthCombo->addItem(obs_module_text("TimelineDock.Note32nd"), 0.125);
	noteLengthCombo->setToolTip(obs_module_text("TimelineDock.NoteTooltip"));
	noteLengthCombo->setCurrentIndex(2); // Default: quarter note
	connect(noteLengthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimelineDock::onNoteLengthChanged);

	QWidgetAction *noteLengthAction = new QWidgetAction(settingsMenu);
	noteLengthAction->setDefaultWidget(noteLengthCombo);
	settingsMenu->addAction(noteLengthAction);

	// Initially hidden; shown when BPM mode is active
	noteLengthCombo->setVisible(false);

	connect(settingsButton, &QPushButton::clicked, this,
		[this]() { settingsMenu->exec(settingsButton->mapToGlobal(QPoint(0, settingsButton->height()))); });

	// Create mode toggle (Time/BPM) buttons
	setupModeToggle();

	zoomToggleContainer = new QWidget(this);
	QHBoxLayout *zoomLayout = new QHBoxLayout(zoomToggleContainer);
	zoomLayout->setSpacing(0);
	zoomLayout->setContentsMargins(0, 0, 0, 0);
	QString zoomLeftStyle = "QPushButton { border-top-right-radius: 0px; border-bottom-right-radius: 0px; "
				"border-right: none; padding: 1px; }";
	QString zoomRightStyle = "QPushButton { border-top-left-radius: 0px; border-bottom-left-radius: 0px; "
				 "border-left: none; padding: 1px; }";
	zoomOutButton->setStyleSheet(zoomLeftStyle);
	zoomInButton->setStyleSheet(zoomRightStyle);
	zoomLayout->addWidget(zoomOutButton);
	zoomLayout->addWidget(zoomInButton);

	updateToolbarIcons();
	zoomOutButton->setFixedSize(20, 20);
	zoomInButton->setFixedSize(20, 20);
	updateZoomLabel();

	updateControlsLayout();

	mainLayout->addLayout(controlsLayout);

	// Timeline View
	view = new QGraphicsView(this);
	scene = new QGraphicsScene(this);
	view->setScene(scene);
	zoomLabel->setParent(view->viewport());
	zoomLabel->raise();
	hoverTimeLabel->setParent(view->viewport());
	hoverTimeLabel->raise();
	view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	// Initial scene rect - height will be updated in resizeEvent
	scene->setSceneRect(0, 0, 5000, view->height());

	// Install event filter for scrubbing handling on viewport
	view->viewport()->installEventFilter(this);
	view->viewport()->setMouseTracking(true);
	connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() { updateZoomLabelPosition(); });

	// Playhead
	playheadItem = scene->addLine(0, 0, 0, view->height(), QPen(playheadColor, 2));
	playheadItem->setZValue(100);

	// Drag Guide (hidden by default)
	dragGuideItem = scene->addLine(0, 0, 0, view->height(), QPen(QColor(0, 255, 0), 1, Qt::DashLine));
	dragGuideItem->setZValue(110);
	dragGuideItem->setVisible(false);

	// Playhead handle (triangle glyph) for easier grabbing
	playheadHandle = scene->addSimpleText(QString::fromUtf8("\xE2\x96\xB2")); // ▲ triangle
	playheadHandle->setBrush(QBrush(playheadColor));
	QFont handleFont = playheadHandle->font();
	handleFont.setPointSize(10);
	playheadHandle->setFont(handleFont);
	playheadHandle->setZValue(101);
	playheadHandle->setFlag(QGraphicsItem::ItemIgnoresTransformations);
	playheadHandle->setAcceptedMouseButtons(Qt::NoButton);

	// Initial layout
	blog(LOG_INFO, "[Timeline] ├─ UI layout initialized");
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

	bpmSpinbox->hide();
	noteLengthCombo->hide();
	snapIntervalCombo->hide();

	controlsLayout->addWidget(show(settingsButton));
	controlsLayout->addWidget(show(settingsSeparator));
	controlsLayout->addWidget(show(addEventButton));
	controlsLayout->addWidget(show(addSeparator));
	if (centerControls) {
		controlsLayout->addStretch();
	}
	controlsLayout->addWidget(show(playButton));
	controlsLayout->addWidget(show(stopButton));
	controlsLayout->addWidget(show(playbackSeparator));
	controlsLayout->addWidget(show(timeLabel));
	controlsLayout->addWidget(show(timeSeparator));
	controlsLayout->addWidget(show(zoomToggleContainer));
	controlsLayout->addWidget(show(zoomSeparator));
	controlsLayout->addStretch();
	controlsLayout->addWidget(show(modeToggleContainer));
	if (gridToBpm) {
		controlsLayout->addWidget(show(bpmSpinbox));
	}
	controlsLayout->addWidget(show(modeSeparator));
	controlsLayout->addWidget(show(snapCheckbox));
	if (gridToBpm) {
		controlsLayout->addWidget(show(noteLengthCombo));
	} else {
		controlsLayout->addWidget(show(snapIntervalCombo));
	}
	controlsLayout->addWidget(show(snapSeparator));
	controlsLayout->addWidget(show(endLabel));
	controlsLayout->addWidget(show(endTimeSpinbox));
	controlsLayout->addWidget(show(loopCheckbox));
	updateDependentControlStates();
	zoomLabel->show();
	updateZoomLabelPosition();

	setUpdatesEnabled(true); // Re-enable updates after layout change
}

void TimelineDock::updateDependentControlStates()
{
	const bool snapSettingsEnabled = snapCheckbox->isChecked();
	snapIntervalCombo->setEnabled(snapSettingsEnabled);
	noteLengthCombo->setEnabled(snapSettingsEnabled);

	const bool hasEndTime = endTimeSpinbox->value() > 0.0;
	if (!hasEndTime && loopCheckbox->isChecked()) {
		loopCheckbox->setChecked(false);
	}
	loopCheckbox->setEnabled(hasEndTime);
}

void TimelineDock::updateZoomLabelPosition()
{
	if (!zoomLabel || !view || !view->viewport())
		return;

	zoomLabel->adjustSize();
	const int margin = 8;
	const QSize size = zoomLabel->sizeHint();
	const QRect viewportRect = view->viewport()->rect();
	zoomLabel->setGeometry(viewportRect.right() - size.width() - margin,
			       viewportRect.bottom() - size.height() - margin, size.width(), size.height());
	zoomLabel->raise();
}

void TimelineDock::seekToSceneX(double x)
{
	if (!scheduler)
		return;

	double timeMs = (x / pixelsPerSecond) * 1000.0;
	if (timeMs < 0.0)
		timeMs = 0.0;
	scheduler->seek(timeMs);
}

void TimelineDock::onZoomInClicked()
{
	// Find the first level strictly greater than current
	auto it = std::upper_bound(ZOOM_LEVELS.begin(), ZOOM_LEVELS.end(), pixelsPerSecond + 0.01);
	if (it != ZOOM_LEVELS.end()) {
		pixelsPerSecond = *it;
		updateLayout();
		updateZoomLabel();
		debounceSaveTimer->start();
	}
}

void TimelineDock::onZoomOutClicked()
{
	// Find the first level greater or equal, then go back one
	auto it = std::lower_bound(ZOOM_LEVELS.begin(), ZOOM_LEVELS.end(), pixelsPerSecond - 0.01);
	if (it != ZOOM_LEVELS.begin()) {
		--it;
		pixelsPerSecond = *it;
		updateLayout();
		updateZoomLabel();
		debounceSaveTimer->start();
	}
}

void TimelineDock::onSnapIntervalChanged(int index)
{
	Q_UNUSED(index);
	if (!gridToBpm) {
		manualSnapIntervalMs = snapIntervalCombo->currentData().toDouble();
		drawGrid();
	}
	if (snapEnabled && !gridToBpm) {
		for (TimelineEventItem *item : eventItemMap) {
			if (item) {
				item->setSnapInterval(manualSnapIntervalMs);
			}
		}
	}
	debounceSaveTimer->start();
}

void TimelineDock::onGridModeChanged(int index)
{
	gridToBpm = (index == 1);

	// Show/hide note length selector in menu
	noteLengthCombo->setVisible(gridToBpm);

	// Update main toolbar layout (shows/hides snap interval controls)
	updateControlsLayout();

	// If switching to Time mode, restore manual interval in combo
	if (!gridToBpm) {
		for (int i = 0; i < snapIntervalCombo->count(); i++) {
			if (snapIntervalCombo->itemData(i).toDouble() == manualSnapIntervalMs) {
				snapIntervalCombo->setCurrentIndex(i);
				break;
			}
		}
	}

	applySnapIntervalToItems();
	drawGrid();

	// Sync toggle buttons to match new state
	if (timeModeBtn && bpmModeBtn) {
		timeModeBtn->blockSignals(true);
		bpmModeBtn->blockSignals(true);
		timeModeBtn->setChecked(!gridToBpm);
		bpmModeBtn->setChecked(gridToBpm);
		timeModeBtn->blockSignals(false);
		bpmModeBtn->blockSignals(false);
	}
	debounceSaveTimer->start();
}

void TimelineDock::onBpmChanged(double value)
{
	bpm = value;
	if (gridToBpm) {
		applySnapIntervalToItems();
		drawGrid();
	}
	debounceSaveTimer->start();
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
		debounceSaveTimer->start();
	}
}

void TimelineDock::onCenterControlsToggled(bool checked)
{
	centerControls = checked;
	updateControlsLayout();
	debounceSaveTimer->start();
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

		drawGrid();
		updateZoomLabelPosition();
	}
}

// Helper to update event items
void TimelineDock::updateLayout()
{
	if (!view || !view->viewport() || !scene) {
		blog(LOG_WARNING, "[Timeline] └─ updateLayout called before UI init");
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
		item->deleteLater();
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
		QFont markerFont = text->font();
		markerFont.setPointSize(9);
		markerFont.setBold(false);
		text->setFont(markerFont);
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
		// Right-click on empty area to add event
		if (event->type() == QEvent::ContextMenu) {
			QContextMenuEvent *ctxEvent = static_cast<QContextMenuEvent *>(event);
			QPointF scenePos = view->mapToScene(ctxEvent->pos());

			// Check if clicking on an event item
			QList<QGraphicsItem *> itemsAt = scene->items(scenePos);
			bool onItem = false;
			for (QGraphicsItem *it : itemsAt) {
				if (it && (it->flags() & QGraphicsItem::ItemIsMovable)) {
					onItem = true;
					break;
				}
			}

			if (!onItem) {
				double clickedTimeMs = (scenePos.x() / pixelsPerSecond) * 1000.0;
				if (clickedTimeMs < 0)
					clickedTimeMs = 0;

				QMenu menu(this);
				QAction *addAction = menu.addAction("Add Event Here");
				connect(addAction, &QAction::triggered,
					[this, clickedTimeMs]() { addEventAtTime(clickedTimeMs); });
				menu.exec(ctxEvent->globalPos());
				return true;
			}
		}

		if (event->type() == QEvent::Wheel) {
			QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
			if (wheelEvent->modifiers() & Qt::ControlModifier) {
				if (wheelEvent->angleDelta().y() > 0) {
					onZoomInClicked();
				} else {
					onZoomOutClicked();
					return true;
				}
			} else {
				// Horizontal scrolling with standard wheel
				// Use Y delta for horizontal scroll if X delta is zero
				int delta = wheelEvent->angleDelta().x();
				if (delta == 0) {
					delta = wheelEvent->angleDelta().y();
				}

				QScrollBar *sb = view->horizontalScrollBar();
				if (sb) {
					sb->setValue(sb->value() - delta);
				}
				return true;
			}
		}

		// Only start scrubbing on press, not on move (to allow item dragging)
		if (event->type() == QEvent::MouseButtonPress) {
			QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
			if (mouseEvent->button() == Qt::LeftButton) {
				QPoint pos = mouseEvent->pos();
				QPointF scenePos = view->mapToScene(pos);
				const double playheadX = (scheduler->getCurrentTime() / 1000.0) * pixelsPerSecond;
				const bool onPlayhead = std::abs(scenePos.x() - playheadX) <= 8.0;

				// Check if clicking on a movable item - if so, let it handle the drag
				QList<QGraphicsItem *> items = scene->items(scenePos);
				for (QGraphicsItem *item : items) {
					if (item && (item->flags() & QGraphicsItem::ItemIsMovable)) {
						// Found a movable item - don't scrub, let item handle it
						return false;
					}
				}

				if (onPlayhead) {
					draggingPlayhead = true;
					view->viewport()->setCursor(Qt::SizeHorCursor);
				}

				// Not on an item - clear selection and scrub
				for (TimelineEventItem *item : eventItemMap) {
					if (item)
						item->setSelected(false);
				}

				if (scheduler->isPlaying()) {
				}

				seekToSceneX(scenePos.x());
				debounceSaveTimer->start();
				return true;
			}
		}

		if (event->type() == QEvent::MouseMove) {
			if (draggingPlayhead) {
				QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
				seekToSceneX(view->mapToScene(mouseEvent->pos()).x());
				return true;
			}
			QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
			QPointF scenePos = view->mapToScene(mouseEvent->pos());
			double timeMs = (scenePos.x() / pixelsPerSecond) * 1000.0;
			if (timeMs < 0)
				timeMs = 0;
			int totalSec = static_cast<int>(timeMs / 1000.0);
			int hrs = totalSec / 3600;
			int mins = (totalSec % 3600) / 60;
			int secs = totalSec % 60;
			int msec = static_cast<int>(timeMs) % 1000;
			hoverTimeLabel->setText(QString("%1:%2:%3.%4")
							.arg(hrs, 2, 10, QChar('0'))
							.arg(mins, 2, 10, QChar('0'))
							.arg(secs, 2, 10, QChar('0'))
							.arg(msec, 3, 10, QChar('0')));
			hoverTimeLabel->adjustSize();
			QSize size = hoverTimeLabel->sizeHint();
			int labelX = mouseEvent->pos().x() + 12;
			int labelY = mouseEvent->pos().y() - size.height() - 8;
			if (labelY < 0)
				labelY = 0;
			if (labelX + size.width() > view->viewport()->width())
				labelX = view->viewport()->width() - size.width() - 4;
			hoverTimeLabel->setGeometry(labelX, labelY, size.width(), size.height());
			hoverTimeLabel->raise();
			return false;
		}

		if (event->type() == QEvent::Enter) {
			hoverTimeLabel->show();
			return false;
		}

		if (event->type() == QEvent::Leave) {
			hoverTimeLabel->hide();
			return false;
		}

		if (event->type() == QEvent::MouseButtonRelease && draggingPlayhead) {
			QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
			if (mouseEvent->button() == Qt::LeftButton) {
				draggingPlayhead = false;
				view->viewport()->unsetCursor();
				seekToSceneX(view->mapToScene(mouseEvent->pos()).x());
				debounceSaveTimer->start();
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
		updatePlayButtonIcon();
		debounceSaveTimer->start();
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
		updatePlayButtonIcon();
		debounceSaveTimer->start();
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
	scheduler->restart();
	if (executor) {
		executor->cancelAll();
	}
	updatePlayButtonIcon();
	debounceSaveTimer->start();

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
	debounceSaveTimer->start();
}

void TimelineDock::onEndTimeChanged(double value)
{
	scheduler->setEndTime(value * 1000.0); // Convert seconds to ms
	updateDependentControlStates();
	updateEndMarker();
	debounceSaveTimer->start();
}

void TimelineDock::onTimelineFinished()
{
	updatePlayButtonIcon();
	blog(LOG_INFO, "[Timeline] Playback finished");
	debounceSaveTimer->start();

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
	blog(LOG_INFO, "[Timeline] Looped");
	debounceSaveTimer->start();
}

double TimelineDock::getSnapIntervalMs() const
{
	if (gridToBpm && bpm > 0.0) {
		return (60.0 / bpm) * noteLengthBeats * 1000.0;
	}
	return snapIntervalCombo->currentData().toDouble();
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
	updateDependentControlStates();
	applySnapIntervalToItems();
	debounceSaveTimer->start();
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
								     noteLengthBeats, centerControls, alternatingBg,
								     scheduler->getCurrentTime(), pixelsPerSecond);
	QString errorMessage;
	if (TimelineSerialization::saveToFile(fileName, root, &errorMessage)) {
		blog(LOG_INFO, "[Timeline] Exported → %s", fileName.toStdString().c_str());
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Export failed");
		blog(LOG_WARNING, "[Timeline] ├─ Path   : %s", fileName.toStdString().c_str());
		blog(LOG_WARNING, "[Timeline] └─ Detail : %s", errorMessage.toUtf8().constData());
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
		loadingTimeline = true;
		model->blockSignals(true);
		if (!TimelineSerialization::fromJsonDocument(doc, *model, timelineDoc, &errorMessage)) {
			model->blockSignals(false);
			loadingTimeline = false;
			blog(LOG_WARNING, "[Timeline] ┌─ Import failed");
			blog(LOG_WARNING, "[Timeline] ├─ Path   : %s", fileName.toStdString().c_str());
			blog(LOG_WARNING, "[Timeline] └─ Detail : %s", errorMessage.toUtf8().constData());
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
		alternatingBg = timelineDoc.alternatingBg;
		alternatingBgCheckbox->blockSignals(true);
		alternatingBgCheckbox->setChecked(alternatingBg);
		alternatingBgCheckbox->blockSignals(false);
		pixelsPerSecond = timelineDoc.zoomPixelsPerSecond;
		scheduler->seek(timelineDoc.currentTimeMs);
		model->blockSignals(false);
		loadingTimeline = false;

		blog(LOG_INFO, "[Timeline] Imported → %s", fileName.toStdString().c_str());
		updateLayout();
		debounceSaveTimer->start();
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Load failed");
		blog(LOG_WARNING, "[Timeline] ├─ Path   : %s", fileName.toStdString().c_str());
		blog(LOG_WARNING, "[Timeline] └─ Detail : %s", errorMessage.toUtf8().constData());
	}
}

void TimelineDock::updateEndMarker()
{
	double endTimeMs = scheduler->getEndTime();

	// Remove existing marker if any
	if (endMarkerItem) {
		scene->removeItem(endMarkerItem);
		endMarkerItem->deleteLater();
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

	blog(LOG_INFO, "[Timeline] Copied %d events", static_cast<int>(clipboard.size()));
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

	blog(LOG_INFO, "[Timeline] Pasted %d events at %.0f ms", static_cast<int>(clipboard.size()), currentPlayhead);
	debounceSaveTimer->start();
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
		item->deleteLater();
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
		double stepSec = getSnapIntervalMs() / 1000.0;
		stepPx = stepSec * pixelsPerSecond;
	}

	// Alternating background sections
	if (alternatingBg) {
		QColor altColor = palette().color(QPalette::Mid);
		altColor.setAlpha(100);
		double altStepPx;
		if (gridToBpm && bpm > 0.0) {
			// BPM mode: one measure = 4 beats
			double measureSec = (60.0 / bpm) * 4.0;
			altStepPx = measureSec * pixelsPerSecond;
		} else {
			// Time mode: 1 second intervals
			altStepPx = pixelsPerSecond;
		}
		for (double x = 0; x < sceneWidth; x += altStepPx * 2.0) {
			double rectW = altStepPx;
			if (x + rectW > sceneWidth)
				rectW = sceneWidth - x;
			if (rectW < 0.1)
				break;
			QGraphicsRectItem *rect =
				scene->addRect(x, 20, rectW, sceneHeight - 20, QPen(Qt::NoPen), QBrush(altColor));
			rect->setZValue(-2);
			gridItems.append(rect);
		}
	}

	// Draw visible vertical grid lines above the scene background but below event items.
	QColor gridColor = palette().color(QPalette::Text);
	gridColor.setAlpha(85);
	QPen gridPen(gridColor, 1, Qt::DashLine);
	for (double x = 0; x < sceneWidth; x += stepPx) {
		// Skip x=0 (optional, but usually 0 is implied)
		if (x < 0.1)
			continue;

		QGraphicsLineItem *line = scene->addLine(x, 20, x, sceneHeight, gridPen);
		line->setZValue(-1);
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

	QString newText = baseText;

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
	playheadItem->setPen(QPen(playheadColor, 2));

	if (playheadHandle) {
		// Center handle on playhead and position above the line
		QRectF br = playheadHandle->boundingRect();
		playheadHandle->setPos(x - br.width() / 2.0, -br.height());
	}

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

void TimelineDock::addEventAtTime(double timeMs)
{
	EventEditor editor(timeMs, this);
	if (editor.exec() != QDialog::Accepted)
		return;

	TimelineEvent evt;
	evt.timeMs = timeMs;

	editor.getUpdatedEvent(evt);

	// Ensure the event time matches the requested position
	evt.timeMs = timeMs;

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
		rebuildScheduled = true;
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
		item->setEventLabel(evt.label.isEmpty() ? evt.sourceName : evt.label);
	}

	// Remove items no longer in the model
	auto it = eventItemMap.begin();
	while (it != eventItemMap.end()) {
		if (!currentIds.contains(it.key())) {
			TimelineEventItem *item = it.value();
			if (item) {
				scene->removeItem(item);
				item->deleteLater();   // Safer Qt ownership - respects event loop
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
			QFont hintFont = QApplication::font();
			hintFont.setPointSize(10);
			emptyHintItem->setFont(hintFont);
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
			item->deleteLater();   // Safer - respects Qt event loop
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
		debounceSaveTimer->start();
	}
}

void TimelineDock::updateZoomLabel()
{
	QString zoomText = QString("x%1").arg(pixelsPerSecond / 100.0, 0, 'f', 1);

	zoomLabelCache = zoomText;
	if (zoomLabel) {
		zoomLabel->setText(zoomLabelCache);
		updateZoomLabelPosition();
	}
}

void TimelineDock::updatePlayButtonIcon()
{
	updateButtonIcon(playButton, scheduler->isPlaying() ? ThemeIcons::PATH_PAUSE : ThemeIcons::PATH_PLAY);
}

void TimelineDock::updateToolbarIcons()
{
	updateButtonIcon(settingsButton, ThemeIcons::PATH_MENU);
	updatePlayButtonIcon();
	updateButtonIcon(stopButton, ThemeIcons::PATH_STOP);
	updateButtonIcon(zoomOutButton, ThemeIcons::PATH_MINUS);
	updateButtonIcon(zoomInButton, ThemeIcons::PATH_PLUS);
	updateButtonIcon(snapCheckbox, ThemeIcons::PATH_MAGNET);
	updateButtonIcon(loopCheckbox, ThemeIcons::PATH_REPEAT);
	updateButtonIcon(timeModeBtn, ThemeIcons::PATH_CLOCK);
	updateButtonIcon(bpmModeBtn, ThemeIcons::PATH_MUSIC_NOTE);
	QColor color = palette().color(QPalette::Text);
	QByteArray svgData = ThemeIcons::wrapSvg(ThemeIcons::PATH_ADD_EVENT, color.name());
	addEventButton->setIcon(QIcon(QPixmap::fromImage(QImage::fromData(svgData))));
	addEventButton->setText("Add Event");
	addEventButton->setFixedHeight(24);
}

void TimelineDock::updateButtonIcon(QPushButton *btn, const char *path)
{
	QColor color = palette().color(QPalette::Text);
	QByteArray svgData = ThemeIcons::wrapSvg(path, color.name());
	QPixmap pixmap = QPixmap::fromImage(QImage::fromData(svgData));
	if (!pixmap.isNull()) {
		btn->setIcon(QIcon(pixmap));
	}
	btn->setText("");
	btn->setFixedSize(24, 24);
}

void TimelineDock::setupModeToggle()
{
	timeModeBtn = new QPushButton(this);
	bpmModeBtn = new QPushButton(this);

	timeModeBtn->setCheckable(true);
	bpmModeBtn->setCheckable(true);
	timeModeBtn->setAutoExclusive(true);
	bpmModeBtn->setAutoExclusive(true);

	// Set initial state
	if (gridToBpm) {
		bpmModeBtn->setChecked(true);
	} else {
		timeModeBtn->setChecked(true);
	}

	updateButtonIcon(timeModeBtn, ThemeIcons::PATH_CLOCK);
	updateButtonIcon(bpmModeBtn, ThemeIcons::PATH_MUSIC_NOTE);

	// Styling to join the buttons
	QString leftStyle = "QPushButton { border-top-right-radius: 0px; border-bottom-right-radius: 0px; "
			    "border-right: none; padding: 4px; }"
			    "QPushButton:checked { background-color: palette(highlight); }";
	QString rightStyle = "QPushButton { border-top-left-radius: 0px; border-bottom-left-radius: 0px; "
			     "border-left: none; padding: 4px; }"
			     "QPushButton:checked { background-color: palette(highlight); }";

	timeModeBtn->setStyleSheet(leftStyle);
	bpmModeBtn->setStyleSheet(rightStyle);

	connect(timeModeBtn, &QPushButton::toggled, [this](bool checked) {
		if (checked && gridToBpm) {
			// Switching to Time mode
			gridTypeCombo->blockSignals(true);
			gridTypeCombo->setCurrentIndex(0);
			gridTypeCombo->blockSignals(false);
			onGridModeChanged(0);
		}
	});
	connect(bpmModeBtn, &QPushButton::toggled, [this](bool checked) {
		if (checked && !gridToBpm) {
			// Switching to BPM mode
			gridTypeCombo->blockSignals(true);
			gridTypeCombo->setCurrentIndex(1);
			gridTypeCombo->blockSignals(false);
			onGridModeChanged(1);
		}
	});

	modeToggleContainer = new QWidget(this);
	QHBoxLayout *toggleLayout = new QHBoxLayout(modeToggleContainer);
	toggleLayout->setSpacing(0);
	toggleLayout->setContentsMargins(0, 0, 0, 0);
	toggleLayout->addWidget(timeModeBtn);
	toggleLayout->addWidget(bpmModeBtn);
}
