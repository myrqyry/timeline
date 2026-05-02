#pragma once
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMessageBox>
#include <obs-module.h>
#include <obs-frontend-api.h>
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
        typeCombo->addItem("Source Show", (int)TimelineEventType::SourceShow);
        typeCombo->addItem("Source Hide", (int)TimelineEventType::SourceHide);
        typeCombo->addItem("Media Start", (int)TimelineEventType::MediaStart);
        typeCombo->addItem("Media Stop", (int)TimelineEventType::MediaStop);
        typeCombo->addItem("Filter Enable", (int)TimelineEventType::FilterEnable);
        typeCombo->addItem("Filter Disable", (int)TimelineEventType::FilterDisable);
        typeCombo->addItem("Hotkey Trigger", (int)TimelineEventType::HotkeyTrigger);
        typeCombo->addItem("Trigger Timeline", (int)TimelineEventType::TriggerTimeline);

        int idx = typeCombo->findData((int)evt.type);
        if (idx != -1)
            typeCombo->setCurrentIndex(idx);

        sourceCombo = new QComboBox();
        sourceCombo->setEditable(true);

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
        layout->addRow("Target Source/Scene:", sourceCombo);
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

        connect(saveBtn, &QPushButton::clicked, this, &EventEditor::validateAndAccept);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

        // Populate based on initial type and pre-select current values
        populateSourceCombo(evt.type);
        sourceCombo->setCurrentText(evt.sourceName);

        if (evt.type == TimelineEventType::FilterEnable || evt.type == TimelineEventType::FilterDisable) {
            for (int i = 0; i < sourceCombo->count(); i++) {
                QVariantList data = sourceCombo->itemData(i).toList();
                if (data.size() >= 2 && data[0].toString() == evt.sourceName && data[1].toString() == evt.filterName) {
                    sourceCombo->setCurrentIndex(i);
                    break;
                }
            }
        } else if (evt.type == TimelineEventType::HotkeyTrigger) {
            for (int i = 0; i < sourceCombo->count(); i++) {
                if (sourceCombo->itemData(i).toString() == evt.sourceName) {
                    sourceCombo->setCurrentIndex(i);
                    break;
                }
            }
        }

        connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&](int index) {
            Q_UNUSED(index);
            TimelineEventType newType = (TimelineEventType)typeCombo->currentData().toInt();
            QString oldText = sourceCombo->currentText();
            populateSourceCombo(newType);
            int foundIdx = sourceCombo->findText(oldText);
            if (foundIdx != -1)
                sourceCombo->setCurrentIndex(foundIdx);
            else
                sourceCombo->setCurrentText(oldText);
        });
    }

    void getUpdatedEvent(TimelineEvent &evt) {
        evt.type = (TimelineEventType)typeCombo->currentData().toInt();

        if (evt.type == TimelineEventType::FilterEnable || evt.type == TimelineEventType::FilterDisable) {
            QVariantList data = sourceCombo->currentData().toList();
            if (data.size() >= 2) {
                evt.sourceName = data[0].toString();
                evt.filterName = data[1].toString();
            } else {
                QStringList parts = sourceCombo->currentText().split("::");
                if (parts.size() >= 2) {
                    evt.sourceName = parts[0];
                    evt.filterName = parts[1];
                } else {
                    evt.sourceName = sourceCombo->currentText();
                    evt.filterName = filterEdit->text();
                }
            }
        } else if (evt.type == TimelineEventType::HotkeyTrigger) {
            evt.sourceName = sourceCombo->currentData().toString();
            if (evt.sourceName.isEmpty())
                evt.sourceName = sourceCombo->currentText();
            evt.filterName = filterEdit->text();
        } else {
            evt.sourceName = sourceCombo->currentText();
            evt.filterName = filterEdit->text();
        }

        evt.hasDuration = durationCheck->isChecked();
        evt.durationMs = durationSpin->value();
    }

private:
    void validateAndAccept() {
        TimelineEventType type = (TimelineEventType)typeCombo->currentData().toInt();
        const QString sourceText = sourceCombo->currentText().trimmed();
        const QString filterText = filterEdit->text().trimmed();

        auto warn = [this](const QString &message) {
            QMessageBox::warning(this, "Invalid Event", message);
        };

        if (type == TimelineEventType::SceneSwitch ||
            type == TimelineEventType::SourceShow ||
            type == TimelineEventType::SourceHide ||
            type == TimelineEventType::MediaStart ||
            type == TimelineEventType::MediaStop) {
            if (sourceText.isEmpty() || sourceText.startsWith("(")) {
                warn("Please select a valid target source/scene.");
                return;
            }
        }

        if (type == TimelineEventType::FilterEnable || type == TimelineEventType::FilterDisable) {
            if (sourceText.isEmpty() || sourceText.startsWith("(")) {
                warn("Filter events require a valid source and filter.");
                return;
            }
            QVariantList data = sourceCombo->currentData().toList();
            if ((data.size() < 2 || data[0].toString().isEmpty() || data[1].toString().isEmpty()) &&
                filterText.isEmpty()) {
                warn("Filter events require both source and filter names.");
                return;
            }
        }

        if (type == TimelineEventType::HotkeyTrigger) {
            if (sourceCombo->currentData().toString().isEmpty() && sourceText.isEmpty()) {
                warn("Hotkey events require a hotkey name.");
                return;
            }
        }

        if (type == TimelineEventType::TriggerTimeline && sourceText.isEmpty()) {
            warn("Trigger Timeline events require a timeline file path.");
            return;
        }

        if (durationCheck->isChecked() && durationSpin->value() < 0.0) {
            warn("Duration must be zero or greater.");
            return;
        }

        accept();
    }

    void populateSourceCombo(TimelineEventType evtType) {
        sourceCombo->clear();
        sourceCombo->setEnabled(true);

        switch (evtType) {
        case TimelineEventType::SceneSwitch: {
            struct obs_frontend_source_list scenes = {};
            obs_frontend_get_scenes(&scenes);
            for (size_t i = 0; i < scenes.sources.num; i++) {
                obs_source_t *source = scenes.sources.array[i];
                const char *name = obs_source_get_name(source);
                sourceCombo->addItem(QString::fromUtf8(name));
            }
            obs_frontend_source_list_free(&scenes);
            if (sourceCombo->count() == 0) {
                sourceCombo->addItem("(No scenes available)");
                sourceCombo->setEnabled(false);
            }
            break;
        }
        case TimelineEventType::SourceShow:
        case TimelineEventType::SourceHide: {
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
                        sourceCombo);
                }
                obs_source_release(currentScene);
            }
            if (sourceCombo->count() == 0) {
                sourceCombo->addItem("(No sources in current scene)");
                sourceCombo->setEnabled(false);
            }
            break;
        }
        case TimelineEventType::MediaStart:
        case TimelineEventType::MediaStop: {
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
                            if (id && (strcmp(id, "ffmpeg_source") == 0 ||
                                       strcmp(id, "vlc_source") == 0)) {
                                const char *name = obs_source_get_name(source);
                                combo->addItem(QString::fromUtf8(name));
                            }
                            return true;
                        },
                        sourceCombo);
                }
                obs_source_release(currentScene);
            }
            if (sourceCombo->count() == 0) {
                sourceCombo->addItem("(No media sources in current scene)");
                sourceCombo->setEnabled(false);
            }
            break;
        }
        case TimelineEventType::FilterEnable:
        case TimelineEventType::FilterDisable: {
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
                                    QString filterName = QString::fromUtf8(obs_source_get_name(filter));
                                    QString displayName = QString("%1::%2")
                                                              .arg(info->second)
                                                              .arg(filterName);

                                    QVariantList eventData;
                                    eventData << info->second << filterName;
                                    info->first->addItem(displayName, eventData);
                                },
                                filterData);
                            delete filterData;

                            return true;
                        },
                        sourceCombo);
                }
                obs_source_release(currentScene);
            }
            if (sourceCombo->count() == 0) {
                sourceCombo->addItem("(No filters found in current scene)");
                sourceCombo->setEnabled(false);
            }
            break;
        }
        case TimelineEventType::HotkeyTrigger: {
            obs_enum_hotkeys(
                [](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
                    Q_UNUSED(id);
                    QComboBox *combo = static_cast<QComboBox *>(data);
                    const char *name = obs_hotkey_get_name(hotkey);
                    if (name && strlen(name) > 0) {
                        const char *desc = obs_hotkey_get_description(hotkey);
                        QString displayName = desc && strlen(desc) > 0
                                                  ? QString::fromUtf8(desc)
                                                  : QString::fromUtf8(name);
                        combo->addItem(displayName, QString::fromUtf8(name));
                    }
                    return true;
                },
                sourceCombo);
            if (sourceCombo->count() == 0) {
                sourceCombo->addItem("(No hotkeys registered)");
                sourceCombo->setEnabled(false);
            }
            break;
        }
        case TimelineEventType::TriggerTimeline:
        default:
            // Free text for timeline file path
            break;
        }
    }

    QComboBox *typeCombo;
    QComboBox *sourceCombo;
    QLineEdit *filterEdit;
    QCheckBox *durationCheck;
    QDoubleSpinBox *durationSpin;
};
