#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QHash>
#include <QSet>
#include <QFrame>
#include <QPixmap>
#include <map>
#include "timeline-model.h"
#include "timeline-scheduler.h"
#include "obs-executor.h"

class TimelineEventItem;
class QGraphicsLineItem;
class QGraphicsSimpleTextItem;
class TimecodeDisplay;

class TimelineDock : public QWidget {
	Q_OBJECT

public:
	TimelineDock(QWidget *parent = nullptr);
	~TimelineDock();

	// Persistence
	void save();
	void load();

	void setObsWebSocketVendor(void *vendor);

	static void saveCallback(obs_data_t *save_data, bool saving, void *private_data);

	// Hotkey callbacks
	static void hotkeyPlayPause(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void hotkeyStop(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void hotkeyRestart(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	// Frontend event callback
	static void frontendEventCallback(enum obs_frontend_event event, void *private_data);

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void changeEvent(QEvent *event) override;

private slots:
	void onPlayClicked();
	void onStopClicked();
	void onTick(double currentTimeMs);
	void onAddEventClicked();
	void addEventAtTime(double timeMs);
	void onEventDoubleClicked(TimelineEventItem *item, const QString &eventId);
	void onLoopToggled(bool checked);
	void onEndTimeChanged(double value);
	void onSnapToggled(bool checked);
	void onSaveClicked();
	void onLoadClicked();
	void onTimelineFinished();
	void onTimelineLooped();
	void onZoomInClicked();
	void onZoomOutClicked();
	void onSnapIntervalChanged(int index);
	void onGridModeChanged(int index);
	void onNoteLengthChanged(int index);
	void onBpmChanged(double value);
	void onCenterControlsToggled(bool checked);
	void onRestartHotkey();

private:
	void setupUi();
	void setupModeToggle();
	void updatePlayhead(double timeMs);
	void updateLayout();
	void drawRuler();
	void drawGrid();
	void clearRuler();
	void rebuildEventItems();
	void clearEventItems();
	void updateZoomLabel();
	void updateEndMarker();
	void updatePlayButtonIcon();
	void updateToolbarIcons();
	void deleteSelectedEvents();
	void copySelectedEvents();
	void pasteEvents();
	void nudgeSelectedEvents(double deltaMs);
	void updateControlsLayout();
	double getSnapIntervalMs() const;
	void applySnapIntervalToItems();
	void connectItemSignals(TimelineEventItem *item, const QString &eventId);
	void updateMoonButtonIcon(QAbstractButton *btn, const char *inner, int size = 24,
				  const QString &label = QString(), int iconSizeOffset = 6);
	void updateDependentControlStates();
	void updateZoomLabelPosition();
	void seekToSceneX(double x);
	void applyThemeColorsToItems();
	QColor computePlayheadColor() const;
	void refreshPlayheadHandlePixmap();
	void refreshEndMarkerHandlePixmap(const QColor &color);

	QList<TimelineEvent> clipboard;

	TimelineModel *model = nullptr;
	TimelineScheduler *scheduler = nullptr;
	ObsExecutor *executor = nullptr;

	// UI Elements
	QGraphicsView *view = nullptr;
	QGraphicsScene *scene = nullptr;

	// Layout
	QHBoxLayout *controlsLayout = nullptr;

	// Settings Menu
	QMenu *settingsMenu = nullptr;

	// Buttons & Controls
	QPushButton *playButton = nullptr;
	QPushButton *stopButton = nullptr;
	QPushButton *zoomInButton = nullptr;
	QPushButton *zoomOutButton = nullptr;
	QPushButton *addEventButton = nullptr;
	QPushButton *saveButton = nullptr;
	QPushButton *loadButton = nullptr;
	QPushButton *settingsButton = nullptr;
	QWidget *zoomToggleContainer = nullptr;
	QFrame *settingsSeparator = nullptr;
	QFrame *playbackSeparator = nullptr;
	QFrame *timeSeparator = nullptr;
	QFrame *zoomSeparator = nullptr;
	QFrame *addSeparator = nullptr;
	QFrame *modeSeparator = nullptr;
	QFrame *snapSeparator = nullptr;

	// Mode toggle buttons
	QPushButton *timeModeBtn = nullptr;
	QPushButton *bpmModeBtn = nullptr;
	QWidget *modeToggleContainer = nullptr;

	// Options
	QPushButton *loopCheckbox = nullptr;
	QPushButton *snapCheckbox = nullptr;
	QCheckBox *centerControlsCheckbox = nullptr;
	QCheckBox *alternatingBgCheckbox = nullptr;
	QCheckBox *compactModeCheckbox = nullptr;
	QCheckBox *showEndTimeCheckbox = nullptr;
	QDoubleSpinBox *endTimeSpinbox = nullptr;
	QDoubleSpinBox *bpmSpinbox = nullptr;
	QComboBox *snapIntervalCombo = nullptr;
	QComboBox *gridTypeCombo = nullptr;
	QComboBox *noteLengthCombo = nullptr;
	QLabel *timeLabel = nullptr;
	QLabel *zoomLabel = nullptr;
	QLabel *hoverTimeLabel = nullptr;
	QLabel *endLabel = nullptr;
	TimecodeDisplay *timecodeDisplay = nullptr;

	// Settings
	bool centerControls = false;
	bool alternatingBg = false;
	bool compactMode = false;
	bool showEndTimeOnDock = true;
	double bpm = 120.0;
	bool gridToBpm = false;
	double noteLengthBeats = 1.0; // Quarter note (1 beat)

	QGraphicsLineItem *playheadItem = nullptr;
	QGraphicsSimpleTextItem *playheadHandle = nullptr;
	QGraphicsPixmapItem *playheadGrabHandle = nullptr;

	QGraphicsLineItem *endMarkerItem = nullptr;
	QGraphicsPixmapItem *endMarkerGrabHandle = nullptr;
	QGraphicsRectItem *postEndOverlay = nullptr;
	QGraphicsLineItem *dragGuideItem = nullptr;
	QGraphicsSimpleTextItem *emptyHintItem = nullptr;

	QColor playheadColor;
	QPixmap playheadHandlePixmap;
	QPixmap endMarkerHandlePixmap;
	QColor cachedPlayheadHandleColor;
	QColor cachedEndMarkerHandleColor;

	QList<QGraphicsItem *> rulerItems;
	QList<QGraphicsItem *> gridItems;

	// Event tracking
	QHash<QString, TimelineEventItem *> eventItemMap;

	double pixelsPerSecond = 99.0;
	bool snapEnabled = false;
	bool draggingPlayhead = false;
	bool draggingEndMarker = false;
	double manualSnapIntervalMs = 500.0;
	bool loadingTimeline = false;

	// Hotkeys
	obs_hotkey_id hotkeyIdPlayPause = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyIdStop = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyIdRestart = OBS_INVALID_HOTKEY_ID;

	void *obsWebSocketVendor = nullptr;
	qint64 lastTickTime = 0;
	bool rebuildScheduled = false;
	QTimer *debounceSaveTimer = nullptr;
	bool isDragging = false;
	QString zoomLabelCache;
};
