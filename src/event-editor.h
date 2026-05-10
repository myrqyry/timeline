#pragma once

#include <QDialog>
#include "event-types.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;
class QCheckBox;
class QDoubleSpinBox;

class EventEditor : public QDialog {
    Q_OBJECT

public:
    explicit EventEditor(const TimelineEvent &evt, QWidget *parent = nullptr);
    explicit EventEditor(double timeMs, QWidget *parent = nullptr);

    void getUpdatedEvent(TimelineEvent &evt) const;

private:
    void buildUi();
    void populateTargets(TimelineEventType evtType);
    void preSelectFromEvent(const TimelineEvent &evt);
    void validateAndAccept();
    void onTypeChanged(int index);
    void pickColor();

    QComboBox *typeCombo = nullptr;
    QLabel *targetLabel = nullptr;
    QComboBox *targetCombo = nullptr;
    QWidget *fileWidget = nullptr;
    QLineEdit *fileEdit = nullptr;
    QWidget *transitionWidget = nullptr;
    QComboBox *transitionCombo = nullptr;
    QPushButton *colorButton = nullptr;
    QLineEdit *iconPathEdit = nullptr;
     QCheckBox *durationCheck = nullptr;
     QDoubleSpinBox *durationSpin = nullptr;
     QLabel *timeInfoLabel = nullptr;

     QDoubleSpinBox *timeSpin = nullptr;

    QLineEdit *labelEdit = nullptr;

    QColor selectedColor;
    double eventTimeMs = 0.0;
    bool isNewMode = false;
};
