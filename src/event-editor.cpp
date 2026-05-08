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
    typeCombo->addItem("Scene Switch", static_cast<int>(TimelineEventType::SceneSwitch));
    typeCombo->addItem("Source Show", static_cast<int>(TimelineEventType::SourceShow));
    typeCombo->addItem("Source Hide", static_cast<int>(TimelineEventType::SourceHide));
    typeCombo->addItem("Media Start", static_cast<int>(TimelineEventType::MediaStart));
    typeCombo->addItem("Media Stop", static_cast<int>(TimelineEventType::MediaStop));
    typeCombo->addItem("Filter Enable", static_cast<int>(TimelineEventType::FilterEnable));
    typeCombo->addItem("Filter Disable", static_cast<int>(TimelineEventType::FilterDisable));
    typeCombo->addItem("Trigger Hotkey", static_cast<int>(TimelineEventType::HotkeyTrigger));
    typeCombo->addItem("Trigger Timeline", static_cast<int>(TimelineEventType::TriggerTimeline));
    layout->addRow("Event Type:", typeCombo);

    targetLabel = new QLabel("Target Scene:", this);
    targetCombo = new QComboBox(this);
    layout->addRow(targetLabel, targetCombo);

    // Timeline file selector (for TriggerTimeline)
    fileWidget = new QWidget(this);
    auto *fileLayout = new QHBoxLayout(fileWidget);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileEdit = new QLineEdit(this);
    auto *browseFileBtn = new QPushButton("Browse...", this);
    fileLayout->addWidget(fileEdit);
    fileLayout->addWidget(browseFileBtn);
    layout->addRow("Timeline File:", fileWidget);
    fileWidget->hide();

    connect(browseFileBtn, &QPushButton::clicked, this, [this]() {
        QString path =
            QFileDialog::getOpenFileName(this, "Select Timeline", "", "JSON Files (*.json)");
        if (!path.isEmpty())
            fileEdit->setText(path);
    });

    // Transition selector (for SceneSwitch)
    transitionWidget = new QWidget(this);
    auto *transitionLayout = new QHBoxLayout(transitionWidget);
    transitionLayout->setContentsMargins(0, 0, 0, 0);
    auto *transitionLabel = new QLabel("Transition:", this);
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
    colorButton->setToolTip("Click to change event color");
    connect(colorButton, &QPushButton::clicked, this, &EventEditor::pickColor);

    iconPathEdit = new QLineEdit(this);
    auto *browseIconBtn = new QPushButton("Browse...", this);
    connect(browseIconBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Select Custom Icon", "",
                                                    "SVG Files (*.svg)");
        if (!path.isEmpty())
            iconPathEdit->setText(path);
    });

    auto *colorIconLayout = new QHBoxLayout();
    colorIconLayout->addWidget(new QLabel("Color:", this));
    colorIconLayout->addWidget(colorButton);
    colorIconLayout->addSpacing(20);
    colorIconLayout->addWidget(new QLabel("Custom Icon:", this));
    colorIconLayout->addWidget(iconPathEdit);
    colorIconLayout->addWidget(browseIconBtn);
    colorIconLayout->addStretch();
    layout->addRow(colorIconLayout);

    // Duration controls
    durationCheck = new QCheckBox("Has Duration", this);
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

    // Time info label
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
        targetLabel->setText("Timeline File:");
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
    } else if (evtType == TimelineEventType::SourceShow ||
               evtType == TimelineEventType::SourceHide) {
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
    } else if (evtType == TimelineEventType::MediaStart ||
               evtType == TimelineEventType::MediaStop) {
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
        targetLabel->setText("Target Filter (Source::Filter):");
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
        targetLabel->setText("Target Hotkey:");
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
    durationCheck->setChecked(evt.hasDuration);
    durationSpin->setValue(evt.durationMs / 1000.0);
}

void EventEditor::validateAndAccept()
{
    TimelineEventType type = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
    const QString sourceText = targetCombo->currentText().trimmed();

    auto warn = [this](const QString &message) {
        QMessageBox::warning(this, "Invalid Event", message);
    };

    if (type == TimelineEventType::TriggerTimeline) {
        if (fileEdit->text().isEmpty()) {
            warn("Please select a timeline file.");
            return;
        }
    } else if (type == TimelineEventType::SceneSwitch || type == TimelineEventType::SourceShow ||
               type == TimelineEventType::SourceHide || type == TimelineEventType::MediaStart ||
               type == TimelineEventType::MediaStop) {
        if (sourceText.isEmpty() || sourceText.startsWith("(")) {
            warn("Please select a valid target source/scene.");
            return;
        }
    } else if (type == TimelineEventType::FilterEnable ||
               type == TimelineEventType::FilterDisable) {
        if (sourceText.isEmpty() || sourceText.startsWith("(")) {
            warn("Filter events require a valid source and filter.");
            return;
        }
        QVariantList data = targetCombo->currentData().toList();
        if (data.size() < 2 || data[0].toString().isEmpty() || data[1].toString().isEmpty()) {
            warn("Filter events require both source and filter names.");
            return;
        }
    } else if (type == TimelineEventType::HotkeyTrigger) {
        if (targetCombo->currentData().toString().isEmpty() && sourceText.isEmpty()) {
            warn("Hotkey events require a hotkey name.");
            return;
        }
    }

    if (durationCheck->isChecked() && durationSpin->value() < 0.0) {
        warn("Duration must be zero or greater.");
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
    evt.timeMs = eventTimeMs;

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
    evt.hasDuration = durationCheck->isChecked();
    evt.durationMs = durationSpin->value() * 1000.0;
}
