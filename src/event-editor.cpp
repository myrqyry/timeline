#include "event-editor.h"
#include "theme-helpers.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include <obs-frontend-api.h>
#include <obs-module.h>

EventEditor::EventEditor(const TimelineEvent &evt, QWidget *parent)
    : QDialog(parent), selectedColor(evt.color), eventTimeMs(evt.timeMs), isNewMode(false)
{
    buildUi();
    preSelectFromEvent(evt);
}

EventEditor::EventEditor(double timeMs, QWidget *parent)
    : QDialog(parent),
      selectedColor(ThemeIcons::getDefaultColor(TimelineEventType::SceneSwitch)),
      eventTimeMs(timeMs),
      isNewMode(true)
{
    buildUi();
    populateTargets(TimelineEventType::SceneSwitch);
}

void EventEditor::buildUi()
{
    auto *layout = new QFormLayout(this);
    layout->setSpacing(8);
    setMinimumWidth(400);

    typeCombo = new QComboBox(this);
    typeCombo->addItem(obs_module_text("EventType.SceneSwitch"), static_cast<int>(TimelineEventType::SceneSwitch));
    typeCombo->addItem(obs_module_text("EventType.SourceShow"), static_cast<int>(TimelineEventType::SourceShow));
    typeCombo->addItem(obs_module_text("EventType.SourceHide"), static_cast<int>(TimelineEventType::SourceHide));
    typeCombo->addItem(obs_module_text("EventType.MediaStart"), static_cast<int>(TimelineEventType::MediaStart));
    typeCombo->addItem(obs_module_text("EventType.MediaStop"), static_cast<int>(TimelineEventType::MediaStop));
    typeCombo->addItem(obs_module_text("EventType.FilterEnable"), static_cast<int>(TimelineEventType::FilterEnable));
    typeCombo->addItem(obs_module_text("EventType.FilterDisable"), static_cast<int>(TimelineEventType::FilterDisable));
    typeCombo->addItem(obs_module_text("EventType.HotkeyTrigger"), static_cast<int>(TimelineEventType::HotkeyTrigger));
    typeCombo->addItem(obs_module_text("EventType.TriggerTimeline"), static_cast<int>(TimelineEventType::TriggerTimeline));
    layout->addRow(obs_module_text("EventEditor.EventType"), typeCombo);

    targetLabel = new QLabel(obs_module_text("EventEditor.TargetScene"), this);
    targetCombo = new QComboBox(this);
    layout->addRow(targetLabel, targetCombo);

    // Timeline file selector (for TriggerTimeline)
    fileWidget = new QWidget(this);
    auto *fileLayout = new QHBoxLayout(fileWidget);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileEdit = new QLineEdit(this);
    auto *browseFileBtn = new QPushButton(obs_module_text("EventEditor.Browse"), this);
    fileLayout->addWidget(fileEdit);
    fileLayout->addWidget(browseFileBtn);
    layout->addRow(obs_module_text("EventEditor.TimelineFile"), fileWidget);
    fileWidget->hide();

    connect(browseFileBtn, &QPushButton::clicked, this, [this]() {
        QString path =
            QFileDialog::getOpenFileName(this, obs_module_text("EventEditor.SelectTimeline"), "", "JSON Files (*.json)");
        if (!path.isEmpty())
            fileEdit->setText(path);
    });

    // Transition selector (for SceneSwitch)
    transitionWidget = new QWidget(this);
    auto *transitionLayout = new QHBoxLayout(transitionWidget);
    transitionLayout->setContentsMargins(0, 0, 0, 0);
    auto *transitionLabel = new QLabel(obs_module_text("EventEditor.Transition"), this);
    transitionCombo = new QComboBox(this);
    transitionLayout->addWidget(transitionLabel);
    transitionLayout->addWidget(transitionCombo);
    layout->addRow(transitionWidget);
    transitionWidget->hide();

    // Color picker + custom icon
    colorButton = new QPushButton(this);
    colorButton->setFixedSize(60, 24);
    colorButton->setStyleSheet(
        QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
    colorButton->setToolTip(obs_module_text("EventEditor.ClickColor"));
    connect(colorButton, &QPushButton::clicked, this, &EventEditor::pickColor);

    iconPathEdit = new QLineEdit(this);
	auto *browseIconBtn = new QPushButton(obs_module_text("EventEditor.Browse"), this);
    connect(browseIconBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, obs_module_text("EventEditor.SelectIcon"), "",
                                                    "SVG Files (*.svg)");
        if (!path.isEmpty())
            iconPathEdit->setText(path);
    });

    auto *colorIconLayout = new QHBoxLayout();
    colorIconLayout->addWidget(new QLabel(obs_module_text("EventEditor.Color"), this));
    colorIconLayout->addWidget(colorButton);
    colorIconLayout->addSpacing(20);
    colorIconLayout->addWidget(new QLabel(obs_module_text("EventEditor.CustomIcon"), this));
    colorIconLayout->addWidget(iconPathEdit);
    colorIconLayout->addWidget(browseIconBtn);
    colorIconLayout->addStretch();
    layout->addRow(colorIconLayout);

    // Duration controls
    durationCheck = new QCheckBox(obs_module_text("EventEditor.HasDuration"), this);
    durationSpin = new QDoubleSpinBox(this);
    durationSpin->setRange(0.0, 3600.0);
    durationSpin->setSuffix(" sec");
    durationSpin->setDecimals(2);
    durationSpin->setSingleStep(0.5);
    durationSpin->setValue(5.0);
    durationSpin->setEnabled(false);
    connect(durationCheck, &QCheckBox::toggled, durationSpin, &QDoubleSpinBox::setEnabled);

     auto *durationLayout = new QHBoxLayout();
     durationLayout->addWidget(durationCheck);
     durationLayout->addWidget(durationSpin);
     layout->addRow(durationLayout);

     // Fine-tune exact time (seconds with millisecond precision)
     timeSpin = new QDoubleSpinBox(this);
     timeSpin->setRange(0.0, 36000.0); // Up to 10 hours
     timeSpin->setDecimals(3);
     timeSpin->setSuffix(" sec");
     timeSpin->setSingleStep(0.1);
    timeSpin->setValue(eventTimeMs / 1000.0);
    layout->addRow(obs_module_text("EventEditor.ExactTime"), timeSpin);

     // Time info label (updates as user changes timeSpin)
     int minutes = static_cast<int>(eventTimeMs / 1000.0) / 60;
     int seconds = static_cast<int>(eventTimeMs / 1000.0) % 60;
     int ms = static_cast<int>(eventTimeMs) % 1000;
     QString timeStr = QString("Event %1 at: %2:%3.%4")
                   .arg(isNewMode ? "will be placed" : "time")
                   .arg(minutes, 2, 10, QChar('0'))
                   .arg(seconds, 2, 10, QChar('0'))
                   .arg(ms / 10, 2, 10, QChar('0'));
     timeInfoLabel = new QLabel(timeStr, this);
     timeInfoLabel->setStyleSheet("color: gray; font-style: italic;");
     layout->addRow(timeInfoLabel);

     // Connect live updates
     connect(timeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double val) {
         int totalMs = static_cast<int>(val * 1000);
         int minutes = totalMs / 60000;
         int seconds = (totalMs / 1000) % 60;
         int ms = totalMs % 1000;
         timeInfoLabel->setText(QString("Event time: %1:%2.%3")
                                     .arg(minutes, 2, 10, QChar('0'))
                                     .arg(seconds, 2, 10, QChar('0'))
                                     .arg(ms / 10, 2, 10, QChar('0')));
     });

     // Time controls are display-only when creating a new event (position determined by context)
     if (isNewMode) {
         timeSpin->setEnabled(false);
     }

     // Optional display label
     showLabelCheck = new QCheckBox(obs_module_text("EventEditor.ShowLabel"), this);
     labelEdit = new QLineEdit(this);
    labelEdit->setPlaceholderText("Optional display label (max 12 chars)");
    labelEdit->setMaxLength(12);
    labelEdit->setEnabled(false);
    connect(showLabelCheck, &QCheckBox::toggled, labelEdit, &QLineEdit::setEnabled);
    auto *labelRow = new QHBoxLayout();
    labelRow->addWidget(showLabelCheck);
    labelRow->addWidget(labelEdit);
    layout->addRow(obs_module_text("EventEditor.TimelineLabel"), labelRow);

     // Buttons
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &EventEditor::validateAndAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttonBox);

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &EventEditor::onTypeChanged);
}

void EventEditor::populateTargets(TimelineEventType evtType)
{
    targetCombo->clear();
    targetCombo->setEnabled(true);

    if (evtType == TimelineEventType::TriggerTimeline) {
        targetLabel->setText(obs_module_text("EventEditor.TimelineFile"));
        targetCombo->hide();
        fileWidget->show();
        transitionWidget->hide();
        return;
    } else {
        targetCombo->show();
        fileWidget->hide();
    }

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
        targetLabel->setText(obs_module_text("EventEditor.TargetScene"));
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
    } else if (evtType == TimelineEventType::SourceShow ||
               evtType == TimelineEventType::SourceHide) {
        targetLabel->setText(obs_module_text("EventEditor.TargetSource"));
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
    } else if (evtType == TimelineEventType::MediaStart ||
               evtType == TimelineEventType::MediaStop) {
        targetLabel->setText(obs_module_text("EventEditor.TargetMedia"));
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
                        if (id && (strcmp(id, "ffmpeg_source") == 0 || strcmp(id, "vlc_source") == 0)) {
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
    } else if (evtType == TimelineEventType::FilterEnable ||
               evtType == TimelineEventType::FilterDisable) {
        targetLabel->setText(obs_module_text("EventEditor.TargetFilter"));
        obs_source_t *currentScene = obs_frontend_get_current_scene();
        if (currentScene) {
            obs_scene_t *scene = obs_scene_from_source(currentScene);
            if (scene) {
                obs_scene_enum_items(
                    scene,
                    [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
                        QComboBox *combo = static_cast<QComboBox *>(param);
                        obs_source_t *source = obs_sceneitem_get_source(item);
                        QString sourceName = QString::fromUtf8(obs_source_get_name(source));

                        auto *filterData = new std::pair<QComboBox *, QString>(combo, sourceName);
                        obs_source_enum_filters(
                            source,
                            [](obs_source_t *, obs_source_t *filter, void *data) {
                                auto *info = static_cast<std::pair<QComboBox *, QString> *>(data);
                                QString filterName =
                                    QString::fromUtf8(obs_source_get_name(filter));
                                QString displayName =
                                    QString("%1::%2").arg(info->second).arg(filterName);

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
        targetLabel->setText(obs_module_text("EventEditor.TargetHotkey"));
        obs_enum_hotkeys(
            [](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
                Q_UNUSED(id);
                QComboBox *combo = static_cast<QComboBox *>(data);
                const char *name = obs_hotkey_get_name(hotkey);
                if (name && strlen(name) > 0) {
                    const char *desc = obs_hotkey_get_description(hotkey);
                    QString displayName = desc && strlen(desc) > 0 ? QString::fromUtf8(desc)
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
}

void EventEditor::preSelectFromEvent(const TimelineEvent &evt)
{
    typeCombo->blockSignals(true);
    int idx = typeCombo->findData(static_cast<int>(evt.type));
    if (idx != -1)
        typeCombo->setCurrentIndex(idx);
    typeCombo->blockSignals(false);

    populateTargets(evt.type);

    if (evt.type == TimelineEventType::TriggerTimeline) {
        fileEdit->setText(evt.data.toString());
    } else if (evt.type == TimelineEventType::SceneSwitch) {
        targetCombo->setCurrentText(evt.sourceName);
        if (!evt.data.isNull() && !evt.data.toString().isEmpty()) {
            int tIdx = transitionCombo->findText(evt.data.toString());
            if (tIdx != -1)
                transitionCombo->setCurrentIndex(tIdx);
        }
    } else if (evt.type == TimelineEventType::HotkeyTrigger) {
        for (int i = 0; i < targetCombo->count(); i++) {
            if (targetCombo->itemData(i).toString() == evt.sourceName) {
                targetCombo->setCurrentIndex(i);
                break;
            }
        }
    } else if (evt.type == TimelineEventType::FilterEnable ||
               evt.type == TimelineEventType::FilterDisable) {
        for (int i = 0; i < targetCombo->count(); i++) {
            QVariantList data = targetCombo->itemData(i).toList();
            if (data.size() >= 2 && data[0].toString() == evt.sourceName &&
                data[1].toString() == evt.filterName) {
                targetCombo->setCurrentIndex(i);
                break;
            }
        }
    } else {
        targetCombo->setCurrentText(evt.sourceName);
    }

     selectedColor = evt.color;
     colorButton->setStyleSheet(
         QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
     iconPathEdit->setText(evt.customIconPath);
     showLabelCheck->setChecked(evt.showLabel);
     labelEdit->setText(evt.label);
     labelEdit->setEnabled(evt.showLabel);
     durationCheck->setChecked(evt.hasDuration);
     durationSpin->setValue(evt.durationMs / 1000.0);
     timeSpin->setValue(evt.timeMs / 1000.0);
 }

void EventEditor::validateAndAccept()
{
    TimelineEventType type = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
    const QString sourceText = targetCombo->currentText().trimmed();

    auto warn = [this](const QString &message) {
        QMessageBox::warning(this, obs_module_text("EventEditor.InvalidEvent"), message);
    };

    if (type == TimelineEventType::TriggerTimeline) {
        if (fileEdit->text().isEmpty()) {
            warn(obs_module_text("EventEditor.WarnSelectFile"));
            return;
        }
    } else if (type == TimelineEventType::SceneSwitch || type == TimelineEventType::SourceShow ||
               type == TimelineEventType::SourceHide || type == TimelineEventType::MediaStart ||
               type == TimelineEventType::MediaStop) {
        if (sourceText.isEmpty() || sourceText.startsWith("(")) {
            warn(obs_module_text("EventEditor.WarnSelectTarget"));
            return;
        }
    } else if (type == TimelineEventType::FilterEnable ||
               type == TimelineEventType::FilterDisable) {
        if (sourceText.isEmpty() || sourceText.startsWith("(")) {
            warn(obs_module_text("EventEditor.WarnFilterSource"));
            return;
        }
        QVariantList data = targetCombo->currentData().toList();
        if (data.size() < 2 || data[0].toString().isEmpty() || data[1].toString().isEmpty()) {
            warn(obs_module_text("EventEditor.WarnFilterNames"));
            return;
        }
    } else if (type == TimelineEventType::HotkeyTrigger) {
        if (targetCombo->currentData().toString().isEmpty() && sourceText.isEmpty()) {
            warn(obs_module_text("EventEditor.WarnHotkeyName"));
            return;
        }
    }

    if (durationCheck->isChecked() && durationSpin->value() < 0.0) {
        warn(obs_module_text("EventEditor.WarnDuration"));
        return;
    }

    accept();
}

void EventEditor::onTypeChanged(int index)
{
    Q_UNUSED(index);
    TimelineEventType newType = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
    populateTargets(newType);
    selectedColor = ThemeIcons::getDefaultColor(newType);
    colorButton->setStyleSheet(
        QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
}

void EventEditor::pickColor()
{
    QColor newColor = QColorDialog::getColor(selectedColor, this, "Select Event Color");
    if (newColor.isValid()) {
        selectedColor = newColor;
        colorButton->setStyleSheet(
            QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
    }
}

 void EventEditor::getUpdatedEvent(TimelineEvent &evt) const
 {
     if (!isNewMode) {
         evt.timeMs = timeSpin->value() * 1000.0;
     }

     evt.type = static_cast<TimelineEventType>(typeCombo->currentData().toInt());

    if (evt.type == TimelineEventType::TriggerTimeline) {
        evt.data = fileEdit->text();
        evt.sourceName = QFileInfo(fileEdit->text()).fileName();
    } else if (evt.type == TimelineEventType::SceneSwitch) {
        evt.sourceName = targetCombo->currentText();
        if (transitionCombo->currentIndex() > 0) {
            evt.data = transitionCombo->currentText();
        } else {
            evt.data = QVariant();
        }
    } else if (evt.type == TimelineEventType::HotkeyTrigger) {
        evt.sourceName = targetCombo->currentData().toString();
        if (evt.sourceName.isEmpty())
            evt.sourceName = targetCombo->currentText();
    } else if (evt.type == TimelineEventType::FilterEnable ||
               evt.type == TimelineEventType::FilterDisable) {
        QVariantList data = targetCombo->currentData().toList();
        if (data.size() >= 2) {
            evt.sourceName = data[0].toString();
            evt.filterName = data[1].toString();
        } else {
            QStringList parts = targetCombo->currentText().split("::");
            if (parts.size() >= 2) {
                evt.sourceName = parts[0];
                evt.filterName = parts[1];
            } else {
                evt.sourceName = targetCombo->currentText();
            }
        }
    } else {
        evt.sourceName = targetCombo->currentText();
    }

     evt.color = selectedColor;
     evt.customIconPath = iconPathEdit->text();
     evt.showLabel = showLabelCheck->isChecked();
     evt.label = labelEdit->text().trimmed();
     evt.hasDuration = durationCheck->isChecked();
     evt.durationMs = durationSpin->value() * 1000.0;
 }
