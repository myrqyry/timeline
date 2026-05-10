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
#include <map>
#include "timeline-model.h"
#include "timeline-scheduler.h"
#include "obs-executor.h"

class TimelineEventItem;
class QGraphicsLineItem;
class QGraphicsSimpleTextItem;

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

	// Theme change callback
	static void themeChangedCallback(enum obs_frontend_event event, void *private_data);

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

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
	void onSnapIntervalChanged(double value);
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
 	void deleteSelectedEvents();
 	void copySelectedEvents();
 	void pasteEvents();
 	void nudgeSelectedEvents(double deltaMs);
 	void updateControlsLayout();
 	double getSnapIntervalMs() const;
 	void applySnapIntervalToItems();
 	void connectItemSignals(TimelineEventItem *item, const QString &eventId);
 	void updateButtonIcon(QPushButton *btn, const char *path);

	QList<TimelineEvent> clipboard;

	TimelineModel *model;
	TimelineScheduler *scheduler;
	ObsExecutor *executor;

	// UI Elements
	QGraphicsView *view;
	QGraphicsScene *scene;

	// Layout
	QHBoxLayout *controlsLayout;

	// Settings Menu
	QMenu *settingsMenu;

	// Buttons & Controls
	QPushButton *playButton;
	QPushButton *stopButton;
	QPushButton *zoomInButton;
	QPushButton *zoomOutButton;
	QPushButton *addEventButton;
	QPushButton *saveButton;
	QPushButton *loadButton;
	QPushButton *settingsButton;

	// Mode toggle buttons
	QPushButton *timeModeBtn;
	QPushButton *bpmModeBtn;
	QWidget *modeToggleContainer;

	// Options
	QCheckBox *loopCheckbox;
	QCheckBox *snapCheckbox;
	QCheckBox *centerControlsCheckbox;
	QDoubleSpinBox *endTimeSpinbox;
	QDoubleSpinBox *bpmSpinbox;
	QDoubleSpinBox *snapIntervalSpinbox;
	QComboBox *gridTypeCombo;
	QComboBox *noteLengthCombo;
	QLabel *timeLabel;
	QLabel *endLabel;
	QLabel *snapIntervalLabel;

	// Settings
	bool centerControls = false;
	double bpm = 120.0;
	bool gridToBpm = false;
	double noteLengthBeats = 1.0; // Quarter note (1 beat)

	QGraphicsLineItem *playheadItem;

	QGraphicsLineItem *endMarkerItem = nullptr;
	QGraphicsLineItem *dragGuideItem = nullptr;
	QGraphicsSimpleTextItem *emptyHintItem = nullptr;

	QList<QGraphicsItem *> rulerItems;
	QList<QGraphicsItem *> gridItems;

	// Event tracking
	QHash<QString, TimelineEventItem *> eventItemMap;

	double pixelsPerSecond = 100.0;
	bool snapEnabled = false;
	double manualSnapIntervalMs = 500.0;

	// Hotkeys
	obs_hotkey_id hotkeyIdPlayPause = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyIdStop = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyIdRestart = OBS_INVALID_HOTKEY_ID;

	void *obsWebSocketVendor = nullptr;
	qint64 lastTickTime = 0;
	bool rebuildScheduled = false;
	bool isDragging = false;
	QString zoomLabelCache;
};
