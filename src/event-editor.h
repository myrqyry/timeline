#pragma once

#include <QDialog>
#include "event-types.h"
#include "hotkey-catalog.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

class EventEditor : public QDialog {
	Q_OBJECT

public:
	explicit EventEditor(const TimelineEvent &evt, QWidget *parent = nullptr, bool allowFreeze = true);
	explicit EventEditor(double timeMs, QWidget *parent = nullptr, bool allowFreeze = true);

	void getUpdatedEvent(TimelineEvent &evt) const;

private:
	void buildUi();
	void populateTargets(TimelineEventType evtType);
	void populateHotkeyPicker();
	void filterHotkeys(const QString &query);
	void populateActionChoices(TimelineEventType category, TimelineEventType selected);
	TimelineEventType selectedActionType() const;
	void preSelectFromEvent(const TimelineEvent &evt);
	void validateAndAccept();
	void onTypeChanged(int index);
	void onActionChanged(int index);
	void pickColor();
	void chooseDefinedParameter();
	void updateDefinedParameterLabel();
	void updateRampControls(bool resetEnd = false);
	bool canRamp() const;
	void updateEndingStateChoices();
	void updateEndingStateVisibility();

	QComboBox *typeCombo = nullptr;
	QLabel *actionLabel = nullptr;
	QComboBox *actionCombo = nullptr;
	QWidget *parameterWidget = nullptr;
	QPushButton *parameterButton = nullptr;
	QLabel *parameterSummary = nullptr;
	QString parameterPayload;
	QWidget *rampWidget = nullptr;
	QDoubleSpinBox *rampEnd = nullptr;
	QComboBox *rampEasing = nullptr;
	QString parameterTarget;
	QWidget *conditionWidget = nullptr;
	QComboBox *conditionSource = nullptr;
	QComboBox *conditionOp = nullptr;
	QDoubleSpinBox *conditionThreshold = nullptr;
	QSpinBox *conditionAge = nullptr;
	QWidget *freezeWidget = nullptr;
	QDoubleSpinBox *freezeDurationSpin = nullptr;
	QWidget *mediaSeekWidget = nullptr;
	QDoubleSpinBox *mediaSeekSpin = nullptr;
	QLabel *targetLabel = nullptr;
	QComboBox *targetCombo = nullptr;
	QLineEdit *hotkeySearchEdit = nullptr;
	QList<HotkeyChoice> hotkeyChoices;
	QString preferredHotkeyId;
	QWidget *fileWidget = nullptr;
	QLineEdit *fileEdit = nullptr;
	QWidget *transitionWidget = nullptr;
	QComboBox *transitionCombo = nullptr;
	QWidget *dskWidget = nullptr;
	QLineEdit *dskNameEdit = nullptr;
	QLineEdit *dskSceneEdit = nullptr;
	QLineEdit *dskViewEdit = nullptr;
	QPushButton *colorButton = nullptr;
	QLineEdit *iconPathEdit = nullptr;
	QCheckBox *durationCheck = nullptr;
	QDoubleSpinBox *durationSpin = nullptr;
	QWidget *endingWidget = nullptr;
	QComboBox *endingCombo = nullptr;
	QLineEdit *endingScene = nullptr;
	QLabel *timeInfoLabel = nullptr;

	QDoubleSpinBox *timeSpin = nullptr;

	QLineEdit *labelEdit = nullptr;
	QCheckBox *showLabelCheck = nullptr;

	QColor selectedColor;
	double eventTimeMs = 0.0;
	double initialDisplayedTimeSeconds = 0.0;
	bool isNewMode = false;
	bool allowFreeze = true;
};
