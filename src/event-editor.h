#pragma once
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include "event-types.h"

class EventEditor : public QDialog {
    Q_OBJECT
public:
    EventEditor(const TimelineEvent &evt, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Edit Event");
        setMinimumWidth(350);
        auto *layout = new QFormLayout(this);

        typeCombo = new QComboBox();
        typeCombo->addItem("Scene Switch", (int)TimelineEventType::SceneSwitch);
        typeCombo->addItem("Source Visibility", (int)TimelineEventType::SourceShow);
        typeCombo->addItem("Media Control", (int)TimelineEventType::MediaStart);
        typeCombo->addItem("Filter Control", (int)TimelineEventType::FilterEnable);
        typeCombo->addItem("Hotkey Trigger", (int)TimelineEventType::HotkeyTrigger);
        
        // Find current type in combo (handle both Show/Hide and Enable/Disable variants)
        int idx = typeCombo->findData((int)evt.type);
        if (idx != -1) {
            typeCombo->setCurrentIndex(idx);
        } else if (evt.type == TimelineEventType::SourceHide) {
            typeCombo->setCurrentIndex(1); // Source Visibility
        } else if (evt.type == TimelineEventType::MediaStop) {
            typeCombo->setCurrentIndex(2); // Media Control
        } else if (evt.type == TimelineEventType::FilterDisable) {
            typeCombo->setCurrentIndex(3); // Filter Control
        }

        sourceEdit = new QLineEdit(evt.sourceName);
        filterEdit = new QLineEdit(evt.filterName);
        
        durationCheck = new QCheckBox("Auto-Stop after duration");
        durationCheck->setChecked(evt.hasDuration);
        durationCheck->setToolTip("When enabled, the action will be automatically reversed after the specified duration.");
        
        durationSpin = new QDoubleSpinBox();
        durationSpin->setRange(100, 600000); // 100ms to 10 minutes
        durationSpin->setSuffix(" ms");
        durationSpin->setValue(evt.durationMs > 0 ? evt.durationMs : 2000);
        durationSpin->setEnabled(evt.hasDuration);

        connect(durationCheck, &QCheckBox::toggled, durationSpin, &QWidget::setEnabled);

        layout->addRow("Type:", typeCombo);
        layout->addRow("Target Source/Scene:", sourceEdit);
        layout->addRow("Filter Name:", filterEdit);
        layout->addRow(durationCheck);
        layout->addRow("Duration:", durationSpin);

        // Buttons
        auto *buttonLayout = new QHBoxLayout();
        auto *saveBtn = new QPushButton("Save Changes");
        auto *cancelBtn = new QPushButton("Cancel");
        buttonLayout->addWidget(saveBtn);
        buttonLayout->addWidget(cancelBtn);
        layout->addRow(buttonLayout);

        connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

    void getUpdatedEvent(TimelineEvent &evt) {
        evt.type = (TimelineEventType)typeCombo->currentData().toInt();
        evt.sourceName = sourceEdit->text();
        evt.filterName = filterEdit->text();
        evt.hasDuration = durationCheck->isChecked();
        evt.durationMs = durationSpin->value();
    }

private:
    QComboBox *typeCombo;
    QLineEdit *sourceEdit;
    QLineEdit *filterEdit;
    QCheckBox *durationCheck;
    QDoubleSpinBox *durationSpin;
};
