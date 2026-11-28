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
#include <QComboBox>
#include <QHash>
#include <QSet>
#include <QFrame>
#include <QPixmap>
#include "timeline-model.h"
#include "timeline-scheduler.h"
#include "obs-executor.h"
#include "macro-automation-recorder.h"
#include <QElapsedTimer>

class TimelineEventItem;
struct TimelineDocument;
class QGraphicsLineItem;
class QGraphicsSimpleTextItem;
class TimecodeDisplay;
class QAction;
class QRubberBand;

class TimelineDock : public QWidget {
	Q_OBJECT

public:
	TimelineDock(QWidget *parent = nullptr, const QString &storageFilePath = {});
	~TimelineDock();

	// Persistence
	void save();
	void load();

	void setObsWebSocketVendor(void *vendor);

	// Public accessor so the MacroStripDock can share this executor and fire actions
	// through the same backend without duplicating OBS API usage.
	ObsExecutor *obsExecutor() const { return executor; }
	// Receives only successful Macro Strip user writes on the GUI thread.
	void recordParameterChange(const MacroParameterBinding &binding, double value);

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
	void onRecordAutomationClicked();
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
	void onZoomInClicked();
	void onZoomOutClicked();
	void onZoomFitClicked();
	void onSnapIntervalChanged(int index);
	void onGridModeChanged(int index);
	void onNoteLengthChanged(int index);
	void onBpmChanged(double value);
	void onCenterControlsToggled(bool checked);
	void onRestartHotkey();

private:
	QString configFilePath() const;
	void setupUi();
	void setupModeToggle();
	void applyTimelineDocument(const TimelineDocument &doc);
	void updatePlayhead(double timeMs);
	void updateVisualTime(double timeMs);
	void updateLayout();
	void updateViewportGeometry();
	double eventContentHeight = 0.0;
	double eventContentRight = 0.0;
	void drawRuler();
	void paintRuler(QPainter *p, double left, double right) const;
	void paintPinnedRuler(QPainter *p) const;
	void drawGrid();
	void paintTimelineBackground(QPainter *painter, const QRectF &exposed) const;
	void rebuildEventItems();
	void clearEventItems();
	void updateZoomLabel();
	void updateEndMarker();
	void updatePlayButtonIcon();
	void updateToolbarIcons();
	void refreshTheme();
	void refreshVisibleCueHealth();
	void updateHistoryControls();
	void undoEdit();
	void redoEdit();
	void deleteSelectedEvents();
	void copySelectedEvents();
	void pasteEvents();
	void nudgeSelectedEvents(double deltaMs);
	void updateControlsLayout();
	double getSnapIntervalMs() const;
	double snapTimeMs(double timeMs) const;
	void applySnapIntervalToItems();
	void connectItemSignals(TimelineEventItem *item, const QString &eventId);
	bool cueMenuAllowed(const QString &id, bool mute) const;
	void updateMoonButtonIcon(QAbstractButton *btn, const char *inner, int size = 24,
				  const QString &label = QString(), int iconSizeOffset = 6);
	void updateDependentControlStates();
	void updateZoomLabelPosition();
	void seekToSceneX(double x);
	void previewDurationResize(double sceneX);
	void finishDurationResize(bool commit);
	void beginMarquee(const QPoint &point, Qt::KeyboardModifiers modifiers);
	void previewSelectionGesture(const QPoint &point);
	void finishSelectionGesture(bool commit);
	bool beginGroupDrag(TimelineEventItem *anchor, const QPoint &point);
	void applyThemeColorsToItems();
	QColor computePlayheadColor() const;
	void refreshPlayheadHandlePixmap();
	void refreshEndMarkerHandlePixmap(const QColor &color);
	void placeNewEvent(double timeMs);
	void finishAutomationRecording(bool commit = true);

	QList<TimelineEvent> clipboard;
	QString storageFilePath;

	TimelineModel *model = nullptr;
	TimelineScheduler *scheduler = nullptr;
	MacroAutomationRecorder automationRecorder;
	QElapsedTimer automationTimer;
	QTimer *automationStopTimer = nullptr;

	ObsExecutor *executor = nullptr;

	// UI Elements
	QGraphicsView *view = nullptr;
	QWidget *pinnedRuler = nullptr;
	QGraphicsScene *scene = nullptr;

	// Layout
	QHBoxLayout *controlsLayout = nullptr;

	// Settings Menu
	QMenu *settingsMenu = nullptr;

	// Buttons & Controls
	QPushButton *playButton = nullptr;
	QPushButton *stopButton = nullptr;
	QPushButton *recordAutomationButton = nullptr;
	QPushButton *zoomInButton = nullptr;
	QPushButton *zoomOutButton = nullptr;
	QPushButton *addEventButton = nullptr;
	QAction *undoAction = nullptr;
	QAction *redoAction = nullptr;
	QPushButton *saveButton = nullptr;
	QPushButton *loadButton = nullptr;
	QPushButton *settingsButton = nullptr;
	QWidget *zoomToggleContainer = nullptr;
	QFrame *settingsSeparator = nullptr;
	QFrame *playbackSeparator = nullptr;
	QFrame *zoomSeparator = nullptr;
	QFrame *clockZoomSeparator = nullptr;
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
	bool centerControls = true;
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

	// Event tracking
	QHash<QString, TimelineEventItem *> eventItemMap;

	double pixelsPerSecond = 99.0;
	bool snapEnabled = false;
	bool draggingPlayhead = false;
	bool draggingEndMarker = false;
	QString resizingEventId;
	double resizeStartMs = 0;
	double resizeInitialMs = 0;
	double resizePreviewMs = 0;
	double resizePressX = 0;
	enum class SelectionGesture { None, Marquee, Group };
	SelectionGesture selectionGesture = SelectionGesture::None;
	QRubberBand *marqueeBand = nullptr;
	QPoint gesturePress;
	double gestureSceneX = 0;
	bool gestureMoved = false;
	Qt::KeyboardModifiers marqueeModifiers = Qt::NoModifier;
	QSet<QString> originalSelection;
	QHash<QString, double> groupOriginalTimes;
	double groupAnchorTime = 0;
	double groupDelta = 0;
	double manualSnapIntervalMs = 500.0;
	bool loadingTimeline = false;
	bool configLoadFailed = false;

	// Hotkeys
	obs_hotkey_id hotkeyIdPlayPause = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyIdStop = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyIdRestart = OBS_INVALID_HOTKEY_ID;

	void *obsWebSocketVendor = nullptr;
	qint64 lastTickTime = 0;
	QElapsedTimer visualTickTimer;
	double lastVisualTimeMs = 0;
	bool lastVisualFrozen = false;
	bool rebuildScheduled = false;
	QTimer *debounceSaveTimer = nullptr;
	int cueHealthCursor = 0;
	bool isDragging = false;
	QString zoomLabelCache;
};
