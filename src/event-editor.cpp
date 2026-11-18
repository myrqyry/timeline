#include "event-editor.h"
#include "theme-helpers.h"
#include "plugin-integrations.h"
#include "scene-item-target.h"
#include "media-target.h"
#include "macro-parameter-widget.h"
#include <QtMath>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>

#include <obs-frontend-api.h>
#include <obs-module.h>

// Keep historic numeric action IDs intact; only the picker groups paired actions.
static TimelineEventType actionCategory(TimelineEventType type)
{
	switch (type) {
	case TimelineEventType::SourceHide:
		return TimelineEventType::SourceShow;
	case TimelineEventType::MediaStop:
	case TimelineEventType::MediaPause:
	case TimelineEventType::MediaResume:
	case TimelineEventType::MediaSeekTo:
		return TimelineEventType::MediaStart;
	case TimelineEventType::FilterDisable:
		return TimelineEventType::FilterEnable;
	default:
		return type;
	}
}

EventEditor::EventEditor(const TimelineEvent &evt, QWidget *parent)
	: QDialog(parent),
	  selectedColor(evt.color),
	  eventTimeMs(evt.timeMs),
	  isNewMode(false)
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
	typeCombo->setObjectName("eventTypeCombo");
	typeCombo->addItem(obs_module_text("EventType.SceneSwitch"), static_cast<int>(TimelineEventType::SceneSwitch));
	typeCombo->addItem(obs_module_text("EventType.SourceVisibility"),
			   static_cast<int>(TimelineEventType::SourceShow));
	typeCombo->addItem(obs_module_text("EventType.MediaPlayback"), static_cast<int>(TimelineEventType::MediaStart));
	typeCombo->addItem(obs_module_text("EventType.FilterVisibility"),
			   static_cast<int>(TimelineEventType::FilterEnable));
	typeCombo->addItem(obs_module_text("EventType.HotkeyTrigger"),
			   static_cast<int>(TimelineEventType::HotkeyTrigger));
	typeCombo->addItem(obs_module_text("EventType.TriggerTimeline"),
			   static_cast<int>(TimelineEventType::TriggerTimeline));
	if (PluginIntegrations::instance().actionAvailable(TimelineEventType::DownstreamKeyerScene))
		typeCombo->addItem(obs_module_text("EventType.DownstreamKeyerScene"),
				   static_cast<int>(TimelineEventType::DownstreamKeyerScene));
	typeCombo->addItem(obs_module_text("EventType.SetParameterValue"),
			   static_cast<int>(TimelineEventType::SetParameterValue));
	layout->addRow(obs_module_text("EventEditor.EventType"), typeCombo);

	actionLabel = new QLabel(obs_module_text("EventEditor.Action"), this);
	actionCombo = new QComboBox(this);
	actionCombo->setObjectName("eventActionCombo");
	layout->addRow(actionLabel, actionCombo);
	populateActionChoices(TimelineEventType::SceneSwitch, TimelineEventType::SceneSwitch);

	targetLabel = new QLabel(obs_module_text("EventEditor.TargetScene"), this);
	QWidget *targetContainer = new QWidget(this);
	QVBoxLayout *targetLayout = new QVBoxLayout(targetContainer);
	targetLayout->setContentsMargins(0, 0, 0, 0);
	targetLayout->setSpacing(4);
	hotkeySearchEdit = new QLineEdit(targetContainer);
	hotkeySearchEdit->setObjectName("hotkeySearchEdit");
	hotkeySearchEdit->setPlaceholderText(obs_module_text("EventEditor.SearchHotkeys"));
	hotkeySearchEdit->setClearButtonEnabled(true);
	hotkeySearchEdit->hide();
	targetCombo = new QComboBox(targetContainer);
	targetCombo->setObjectName("hotkeyTargetCombo");
	targetLayout->addWidget(hotkeySearchEdit);
	targetLayout->addWidget(targetCombo);
	layout->addRow(targetLabel, targetContainer);
	connect(hotkeySearchEdit, &QLineEdit::textChanged, this, &EventEditor::filterHotkeys);
	connect(targetCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		if (static_cast<TimelineEventType>(typeCombo->currentData().toInt()) ==
		    TimelineEventType::HotkeyTrigger)
			preferredHotkeyId = targetCombo->itemData(index).toString();
	});

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
		QString path = QFileDialog::getOpenFileName(this, obs_module_text("EventEditor.SelectTimeline"), "",
							    "JSON Files (*.json)");
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

	// Separate downstream-keyer channel/scene controls; do not reuse main-scene switching.
	dskWidget = new QWidget(this);
	auto *dskLayout = new QFormLayout(dskWidget);
	dskLayout->setContentsMargins(0, 0, 0, 0);
	dskNameEdit = new QLineEdit(dskWidget);
	dskNameEdit->setObjectName("dskNameEdit");
	dskSceneEdit = new QLineEdit(dskWidget);
	dskSceneEdit->setObjectName("dskSceneEdit");
	dskViewEdit = new QLineEdit(dskWidget);
	dskViewEdit->setObjectName("dskViewEdit");
	dskLayout->addRow(obs_module_text("EventEditor.DskName"), dskNameEdit);
	dskLayout->addRow(obs_module_text("EventEditor.DskScene"), dskSceneEdit);
	dskLayout->addRow(obs_module_text("EventEditor.DskView"), dskViewEdit);
	layout->addRow(dskWidget);
	dskWidget->hide();

	mediaSeekWidget = new QWidget(this);
	auto *mediaSeekLayout = new QHBoxLayout(mediaSeekWidget);
	mediaSeekLayout->setContentsMargins(0, 0, 0, 0);
	mediaSeekLayout->addWidget(new QLabel(obs_module_text("EventEditor.MediaSeekPosition"), mediaSeekWidget));
	mediaSeekSpin = new QDoubleSpinBox(mediaSeekWidget);
	mediaSeekSpin->setObjectName("mediaSeekPosition");
	mediaSeekSpin->setRange(0.0, 86400.0);
	mediaSeekSpin->setDecimals(3);
	mediaSeekSpin->setSuffix(" sec");
	mediaSeekLayout->addWidget(mediaSeekSpin);
	layout->addRow(mediaSeekWidget);
	mediaSeekWidget->hide();

	parameterWidget = new QWidget(this);
	auto *parameterLayout = new QVBoxLayout(parameterWidget);
	parameterLayout->setContentsMargins(0, 0, 0, 0);
	parameterButton = new QPushButton(obs_module_text("EventEditor.DefineParameter"), parameterWidget);
	parameterButton->setObjectName("eventDefineParameter");
	parameterSummary = new QLabel(parameterWidget);
	parameterSummary->setWordWrap(true);
	parameterSummary->setTextFormat(Qt::PlainText);
	parameterLayout->addWidget(parameterButton);
	parameterLayout->addWidget(parameterSummary);
	layout->addRow(parameterWidget);
	parameterWidget->hide();
	connect(parameterButton, &QPushButton::clicked, this, &EventEditor::chooseDefinedParameter);
	updateDefinedParameterLabel();

	// Color picker + custom icon
	colorButton = new QPushButton(this);
	colorButton->setFixedSize(60, 24);
	colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
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
	timeSpin->setObjectName("eventTimeSpin");
	timeSpin->setRange(0.0, 36000.0); // Up to 10 hours
	timeSpin->setDecimals(3);
	timeSpin->setSuffix(" sec");
	timeSpin->setSingleStep(0.1);
	timeSpin->setValue(eventTimeMs / 1000.0);
	initialDisplayedTimeSeconds = timeSpin->value();
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

	connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EventEditor::onTypeChanged);
	connect(actionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EventEditor::onActionChanged);
}

void EventEditor::populateActionChoices(TimelineEventType category, TimelineEventType selected)
{
	const QSignalBlocker blocker(actionCombo);
	actionCombo->clear();
	auto add = [this](const char *key, TimelineEventType type) {
		actionCombo->addItem(obs_module_text(key), static_cast<int>(type));
	};

	switch (category) {
	case TimelineEventType::SourceShow:
		add("EventEditor.ActionShow", TimelineEventType::SourceShow);
		add("EventEditor.ActionHide", TimelineEventType::SourceHide);
		break;
	case TimelineEventType::MediaStart:
		add("EventEditor.ActionRestart", TimelineEventType::MediaStart);
		add("EventEditor.ActionStop", TimelineEventType::MediaStop);
		add("EventEditor.ActionPause", TimelineEventType::MediaPause);
		add("EventEditor.ActionResume", TimelineEventType::MediaResume);
		add("EventEditor.ActionSeek", TimelineEventType::MediaSeekTo);
		break;
	case TimelineEventType::FilterEnable:
		add("EventEditor.ActionEnable", TimelineEventType::FilterEnable);
		add("EventEditor.ActionDisable", TimelineEventType::FilterDisable);
		break;
	default:
		break;
	}
	const bool hasOptions = actionCombo->count() > 0;
	actionLabel->setVisible(hasOptions);
	actionCombo->setVisible(hasOptions);
	if (hasOptions) {
		const int idx = actionCombo->findData(static_cast<int>(selected));
		actionCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	}
}

TimelineEventType EventEditor::selectedActionType() const
{
	if (actionCombo->count() > 0)
		return static_cast<TimelineEventType>(actionCombo->currentData().toInt());
	return static_cast<TimelineEventType>(typeCombo->currentData().toInt());
}

void EventEditor::populateTargets(TimelineEventType evtType)
{
	hotkeySearchEdit->setVisible(evtType == TimelineEventType::HotkeyTrigger);
	if (evtType != TimelineEventType::HotkeyTrigger) {
		hotkeySearchEdit->clear();
		hotkeyChoices.clear();
		preferredHotkeyId.clear();
	}
	targetCombo->clear();
	targetCombo->setEnabled(true);
	dskWidget->setVisible(evtType == TimelineEventType::DownstreamKeyerScene);
	parameterWidget->setVisible(evtType == TimelineEventType::SetParameterValue);
	if (evtType == TimelineEventType::SetParameterValue) {
		targetLabel->hide();
		targetCombo->hide();
		fileWidget->hide();
		transitionWidget->hide();
		return;
	}

	if (evtType == TimelineEventType::DownstreamKeyerScene) {
		targetLabel->hide();
		targetCombo->hide();
		fileWidget->hide();
		transitionWidget->hide();
		return;
	}
	targetLabel->show();

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
	} else if (evtType == TimelineEventType::SourceShow || evtType == TimelineEventType::SourceHide) {
		targetLabel->setText(obs_module_text("EventEditor.TargetSource"));
		obs_source_t *currentScene = obs_frontend_get_current_scene();
		if (currentScene) {
			obs_scene_t *scene = obs_scene_from_source(currentScene);
			if (scene) {
				SceneItemTarget::visit(
					scene, QString::fromUtf8(obs_source_get_name(currentScene)),
					[this](obs_scene_t *owner, obs_sceneitem_t *item, const QString &name,
					       const QString &path) {
						const auto key = SceneItemTarget::key(owner, item);
						targetCombo->addItem(
							path + QStringLiteral(" [%1]").arg(obs_sceneitem_get_id(item)),
							QVariantList{name, key});
					});
			}
			obs_source_release(currentScene);
		}
		if (targetCombo->count() == 0) {
			targetCombo->addItem("(No sources in current scene)");
			targetCombo->setEnabled(false);
		}
	} else if (isMediaAction(evtType)) {
		targetLabel->setText(obs_module_text("EventEditor.TargetMedia"));
		QSet<QString> seen;
		auto add = [&](obs_source_t *source, const QString &path) {
			if (!source || obs_source_removed(source) ||
			    !(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA))
				return;
			const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
			if (uuid.isEmpty() || seen.contains(uuid))
				return;
			seen.insert(uuid);
			targetCombo->addItem(path, QVariantList{QString::fromUtf8(obs_source_get_name(source)), uuid});
		};
		auto visit = [&](obs_source_t *root) {
			SceneItemTarget::visit(obs_scene_from_source(root),
					       QString::fromUtf8(obs_source_get_name(root)),
					       [&](obs_scene_t *, obs_sceneitem_t *item, const QString &,
						   const QString &path) { add(obs_sceneitem_get_source(item), path); });
		};
		if (auto *current = obs_frontend_get_current_scene()) {
			visit(current);
			obs_source_release(current);
		}
		// Also allow cueing media in another scene before switching to it.
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; ++i)
			visit(scenes.sources.array[i]);
		obs_frontend_source_list_free(&scenes);
		if (targetCombo->count() == 0) {
			targetCombo->addItem(obs_module_text("EventEditor.NoMediaSources"));
			targetCombo->setEnabled(false);
		}
	} else if (evtType == TimelineEventType::FilterEnable || evtType == TimelineEventType::FilterDisable) {
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

						auto *filterData =
							new std::pair<QComboBox *, QString>(combo, sourceName);
						obs_source_enum_filters(
							source,
							[](obs_source_t *, obs_source_t *filter, void *data) {
								auto *info =
									static_cast<std::pair<QComboBox *, QString> *>(
										data);
								QString filterName =
									QString::fromUtf8(obs_source_get_name(filter));
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
		hotkeyChoices = HotkeyCatalog::collect();
		preferredHotkeyId.clear();
		hotkeySearchEdit->clear();
		populateHotkeyPicker();
	}
}

void EventEditor::filterHotkeys(const QString &)
{
	if (static_cast<TimelineEventType>(typeCombo->currentData().toInt()) == TimelineEventType::HotkeyTrigger)
		populateHotkeyPicker();
}

void EventEditor::populateHotkeyPicker()
{
	const QString query = hotkeySearchEdit->text().trimmed();
	const QSignalBlocker blocker(targetCombo);
	targetCombo->clear();

	QString previousGroup;
	int firstChoice = -1;
	int preferredChoice = -1;
	for (const auto &choice : hotkeyChoices) {
		if (!query.isEmpty() && !choice.id.contains(query, Qt::CaseInsensitive) &&
		    !choice.label.contains(query, Qt::CaseInsensitive) &&
		    !choice.group.contains(query, Qt::CaseInsensitive))
			continue;

		if (choice.group != previousGroup) {
			targetCombo->addItem(choice.group);
			const int groupIndex = targetCombo->count() - 1;
			if (auto *model = qobject_cast<QStandardItemModel *>(targetCombo->model()))
				model->item(groupIndex)->setEnabled(false);
			previousGroup = choice.group;
		}
		targetCombo->addItem(choice.label, choice.id);
		const int row = targetCombo->count() - 1;
		targetCombo->setItemData(row, choice.id, Qt::ToolTipRole);
		if (firstChoice < 0)
			firstChoice = row;
		if (choice.id == preferredHotkeyId)
			preferredChoice = row;
	}

	const bool found = firstChoice >= 0;
	if (!found) {
		targetCombo->addItem(obs_module_text(hotkeyChoices.isEmpty() ? "EventEditor.NoHotkeys"
									     : "EventEditor.NoHotkeyMatches"));
		if (auto *model = qobject_cast<QStandardItemModel *>(targetCombo->model()))
			model->item(0)->setEnabled(false);
		targetCombo->setEnabled(false);
		return;
	}
	targetCombo->setEnabled(true);
	targetCombo->setCurrentIndex(preferredChoice >= 0 ? preferredChoice : firstChoice);
}

void EventEditor::preSelectFromEvent(const TimelineEvent &evt)
{
	typeCombo->blockSignals(true);
	const TimelineEventType category = actionCategory(evt.type);
	int idx = typeCombo->findData(static_cast<int>(category));
	if (idx < 0 && evt.type == TimelineEventType::DownstreamKeyerScene) {
		// Preserve editing of imported/saved plugin actions even when the plugin is absent or hidden.
		typeCombo->addItem(QString::fromUtf8(obs_module_text("EventType.DownstreamKeyerScene")) +
					   QString::fromUtf8(obs_module_text("EventType.UnavailableSuffix")),
				   static_cast<int>(evt.type));
		idx = typeCombo->count() - 1;
		if (auto *items = qobject_cast<QStandardItemModel *>(typeCombo->model()))
			items->item(idx)->setEnabled(false);
		typeCombo->setItemData(idx, obs_module_text("EventType.UnavailableTooltip"), Qt::ToolTipRole);
	}
	if (idx != -1)
		typeCombo->setCurrentIndex(idx);
	typeCombo->blockSignals(false);

	populateActionChoices(category, evt.type);
	populateTargets(category);

	if (evt.type == TimelineEventType::SetParameterValue) {
		parameterPayload = evt.data.toString();
		parameterTarget = evt.sourceName;
		updateDefinedParameterLabel();
	} else if (evt.type == TimelineEventType::DownstreamKeyerScene) {
		dskNameEdit->setText(evt.sourceName);
		dskSceneEdit->setText(evt.filterName);
		dskViewEdit->setText(evt.data.toString());
	} else if (evt.type == TimelineEventType::TriggerTimeline) {
		fileEdit->setText(evt.data.toString());
	} else if (evt.type == TimelineEventType::SceneSwitch) {
		targetCombo->setCurrentText(evt.sourceName);
		if (!evt.data.isNull() && !evt.data.toString().isEmpty()) {
			int tIdx = transitionCombo->findText(evt.data.toString());
			if (tIdx != -1)
				transitionCombo->setCurrentIndex(tIdx);
		}
	} else if (evt.type == TimelineEventType::HotkeyTrigger) {
		preferredHotkeyId = evt.sourceName;
		bool available = false;
		for (const auto &choice : hotkeyChoices) {
			if (choice.id == evt.sourceName) {
				available = true;
				break;
			}
		}
		if (!available && !evt.sourceName.isEmpty()) {
			// An imported hotkey may be registered only after its plugin loads again.
			hotkeyChoices.prepend({evt.sourceName, evt.sourceName,
					       QString::fromUtf8(obs_module_text("EventEditor.MissingHotkeys"))});
		}
		populateHotkeyPicker();
	} else if (evt.type == TimelineEventType::SourceShow || evt.type == TimelineEventType::SourceHide) {
		const auto key = evt.data.toString();
		const bool explicitTarget = SceneItemTarget::isExplicit(key);
		int index = -1;
		if (explicitTarget) {
			for (int i = 0; i < targetCombo->count(); ++i) {
				if (targetCombo->itemData(i).toList().value(1).toString() == key) {
					index = i;
					break;
				}
			}
		}
		if (index < 0 && !evt.sourceName.isEmpty()) {
			// Preserve historical name-based targets and explicit targets outside this scene.
			const auto suffix = QString::fromUtf8(obs_module_text(
				explicitTarget ? "EventEditor.SavedSceneItem" : "EventEditor.LegacySourceTarget"));
			targetCombo->addItem(evt.sourceName + QStringLiteral(" — ") + suffix,
					     QVariantList{evt.sourceName, explicitTarget ? key : QString()});
			index = targetCombo->count() - 1;
			targetCombo->setEnabled(true);
		}
		if (index >= 0)
			targetCombo->setCurrentIndex(index);
	} else if (isMediaAction(evt.type)) {
		const auto selection = MediaTarget::decode(evt.data.toString());
		int index = -1;
		if (selection.valid && !selection.uuid.isEmpty()) {
			for (int i = 0; i < targetCombo->count(); ++i) {
				if (targetCombo->itemData(i).toList().value(1).toString() == selection.uuid) {
					index = i;
					break;
				}
			}
		}
		if (index < 0 && !evt.sourceName.isEmpty()) {
			targetCombo->addItem(evt.sourceName + QStringLiteral(" — ") +
						     QString::fromUtf8(obs_module_text("EventEditor.SavedMediaTarget")),
					     QVariantList{evt.sourceName, selection.uuid, evt.data.toString()});
			index = targetCombo->count() - 1;
			targetCombo->setEnabled(true);
		}
		if (index >= 0)
			targetCombo->setCurrentIndex(index);
		mediaSeekSpin->setValue(selection.positionMs / 1000.0);
	} else if (evt.type == TimelineEventType::FilterEnable || evt.type == TimelineEventType::FilterDisable) {
		bool found = false;
		for (int i = 0; i < targetCombo->count(); i++) {
			QVariantList data = targetCombo->itemData(i).toList();
			if (data.size() >= 2 && data[0].toString() == evt.sourceName &&
			    data[1].toString() == evt.filterName) {
				targetCombo->setCurrentIndex(i);
				found = true;
				break;
			}
		}
		if (!found && !evt.sourceName.isEmpty() && !evt.filterName.isEmpty()) {
			QVariantList data{evt.sourceName, evt.filterName};
			targetCombo->addItem(evt.sourceName + "::" + evt.filterName, data);
			targetCombo->setCurrentIndex(targetCombo->count() - 1);
		}
	} else {
		targetCombo->setCurrentText(evt.sourceName);
		if (!evt.sourceName.isEmpty() && targetCombo->currentText() != evt.sourceName) {
			targetCombo->addItem(evt.sourceName);
			targetCombo->setCurrentIndex(targetCombo->count() - 1);
		}
	}

	selectedColor = evt.color;
	colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
	iconPathEdit->setText(evt.customIconPath);
	showLabelCheck->setChecked(evt.showLabel);
	labelEdit->setText(evt.label);
	labelEdit->setEnabled(evt.showLabel);
	durationCheck->setChecked(evt.hasDuration);
	durationSpin->setValue(evt.durationMs / 1000.0);
	timeSpin->setValue(evt.timeMs / 1000.0);
	onActionChanged(actionCombo->currentIndex());
	selectedColor = evt.color;
	colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
}

void EventEditor::validateAndAccept()
{
	TimelineEventType type = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
	const QString sourceText = targetCombo->currentText().trimmed();

	auto warn = [this](const QString &message) {
		QMessageBox::warning(this, obs_module_text("EventEditor.InvalidEvent"), message);
	};

	if (type == TimelineEventType::SetParameterValue) {
		// Allow an existing missing binding to be preserved, but require a
		// well-formed explicit payload. The executor revalidates at playback.
		if (!MacroParameter::decodeValue(parameterPayload)) {
			warn(obs_module_text("EventEditor.WarnDefineParameter"));
			return;
		}
	} else if (type == TimelineEventType::DownstreamKeyerScene) {
		if (dskNameEdit->text().trimmed().isEmpty()) {
			warn(obs_module_text("EventEditor.WarnDskName"));
			return;
		}
	} else if (type == TimelineEventType::TriggerTimeline) {
		if (fileEdit->text().isEmpty()) {
			warn(obs_module_text("EventEditor.WarnSelectFile"));
			return;
		}
	} else if (type == TimelineEventType::SourceShow || type == TimelineEventType::SourceHide) {
		if (targetCombo->currentData().toList().value(0).toString().isEmpty()) {
			warn(obs_module_text("EventEditor.WarnSelectTarget"));
			return;
		}
	} else if (isMediaAction(type)) {
		if (targetCombo->currentData().toList().value(0).toString().isEmpty() ||
		    !MediaTarget::decode(targetCombo->currentData().toList().value(2).toString()).valid) {
			warn(obs_module_text("EventEditor.WarnSelectTarget"));
			return;
		}
	} else if (type == TimelineEventType::SceneSwitch) {
		if (sourceText.isEmpty() || sourceText.startsWith("(")) {
			warn(obs_module_text("EventEditor.WarnSelectTarget"));
			return;
		}
	} else if (type == TimelineEventType::FilterEnable || type == TimelineEventType::FilterDisable) {
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
		if (targetCombo->currentData().toString().isEmpty()) {
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
	TimelineEventType category = static_cast<TimelineEventType>(typeCombo->currentData().toInt());
	populateActionChoices(category, category);
	populateTargets(category);
	onActionChanged(actionCombo->currentIndex());
}

void EventEditor::onActionChanged(int index)
{
	Q_UNUSED(index);
	const bool seek = selectedActionType() == TimelineEventType::MediaSeekTo;
	const bool instantaneous = seek || selectedActionType() == TimelineEventType::SetParameterValue;
	mediaSeekWidget->setVisible(seek);
	durationCheck->setText(obs_module_text(
		selectedActionType() == TimelineEventType::MediaPause    ? "EventEditor.ResumeAfterDuration"
		: selectedActionType() == TimelineEventType::MediaResume ? "EventEditor.PauseAfterDuration"
									 : "EventEditor.HasDuration"));
	durationCheck->setEnabled(!instantaneous);
	durationSpin->setEnabled(!instantaneous && durationCheck->isChecked());
	selectedColor = ThemeIcons::getDefaultColor(selectedActionType());
	colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid black;").arg(selectedColor.name()));
}

void EventEditor::chooseDefinedParameter()
{
	MacroButton initial;
	std::optional<double> value;
	if (const auto saved = MacroParameter::decodeValue(parameterPayload)) {
		initial.binding = saved->binding;
		value = saved->value;
	}
	if (const auto chosen = editDefinedParameter(this, initial, value); chosen && value) {
		parameterPayload = MacroParameter::encodeValue({chosen->binding, *value});
		parameterTarget = chosen->sourceName;
		updateDefinedParameterLabel();
	}
}

void EventEditor::updateDefinedParameterLabel()
{
	if (const auto saved = MacroParameter::decodeValue(parameterPayload)) {
		parameterSummary->setText(parameterTarget + "\n" + MacroParameter::displayValue(*saved));
		if (!MacroParameter::describe(saved->binding))
			parameterSummary->setText(parameterSummary->text() + "\n" +
						  obs_module_text("EventEditor.ParameterUnavailable"));
	} else {
		parameterSummary->setText(obs_module_text("EventEditor.WarnDefineParameter"));
	}
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
		// Preserve sub-millisecond BPM positions if the user did not edit
		// the millisecond-resolution time field.
		evt.timeMs = timeSpin->value() == initialDisplayedTimeSeconds ? eventTimeMs
									      : timeSpin->value() * 1000.0;
	}

	if ((evt.type == TimelineEventType::SourceShow || evt.type == TimelineEventType::SourceHide) &&
	    SceneItemTarget::isExplicit(evt.data.toString()))
		evt.data = QVariant();
	if (isMediaAction(evt.type) || evt.type == TimelineEventType::SetParameterValue)
		evt.data = QVariant();
	evt.type = selectedActionType();

	if (evt.type == TimelineEventType::SetParameterValue) {
		evt.sourceName = parameterTarget;
		evt.filterName.clear();
		evt.data = parameterPayload;
	} else if (evt.type == TimelineEventType::DownstreamKeyerScene) {
		evt.sourceName = dskNameEdit->text().trimmed();
		evt.filterName = dskSceneEdit->text().trimmed(); // Blank hides overlay
		evt.data = dskViewEdit->text().trimmed();        // Blank targets main canvas
	} else if (evt.type == TimelineEventType::TriggerTimeline) {
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
	} else if (evt.type == TimelineEventType::SourceShow || evt.type == TimelineEventType::SourceHide) {
		const auto target = targetCombo->currentData().toList();
		evt.sourceName = target.value(0).toString();
		evt.data = target.value(1);
	} else if (isMediaAction(evt.type)) {
		const auto target = targetCombo->currentData().toList();
		evt.sourceName = target.value(0).toString();
		evt.filterName.clear();
		const auto oldData = target.value(2).toString();
		if (!MediaTarget::decode(oldData).valid) {
			evt.data = oldData;
		} else if (!target.value(1).toString().isEmpty() || evt.type == TimelineEventType::MediaSeekTo) {
			evt.data = MediaTarget::encode(target.value(1).toString(),
						       evt.type == TimelineEventType::MediaSeekTo
							       ? qRound64(mediaSeekSpin->value() * 1000.0)
							       : 0);
		} else {
			evt.data = QVariant();
		}
	} else if (evt.type == TimelineEventType::FilterEnable || evt.type == TimelineEventType::FilterDisable) {
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
	evt.hasDuration = evt.type != TimelineEventType::MediaSeekTo &&
			  evt.type != TimelineEventType::SetParameterValue && durationCheck->isChecked();
	evt.durationMs = durationSpin->value() * 1000.0;
}
