#include "macro-parameter-widget.h"

#include <QAbstractSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace {
class ParameterWidget final : public QWidget {
public:
	ParameterWidget(const MacroButton &button, QWidget *parent)
		: QWidget(parent), binding(button.binding)
	{
		setObjectName("macro-parameter-control");
		setContextMenuPolicy(Qt::CustomContextMenu);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(3, 1, 3, 1);
		layout->setSpacing(0);
		setFixedHeight(44);
		label = new QLabel(this);
		label->setAlignment(Qt::AlignCenter);
		label->setStyleSheet("font-size: 10px;");
		title = button.label.isEmpty() ? binding.key : button.label;
		label->setText(title);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		layout->addWidget(label);
		if (button.kind == MacroControlKind::Toggle) {
			toggle = new QCheckBox(this);
			toggle->setObjectName("macro-parameter-toggle");
			toggle->setAccessibleName(title);
			layout->addWidget(toggle, 0, Qt::AlignHCenter);
			connect(toggle, &QCheckBox::toggled, this, [this](bool value) {
				queueValue(value ? 1 : 0);
				flush();
			});
		} else {
			if (button.kind == MacroControlKind::Knob) {
				auto *dial = new QDial(this);
				dial->setObjectName("macro-parameter-knob");
				dial->setWrapping(false);
				dial->setNotchesVisible(false);
				dial->setFixedSize(28, 28);
				slider = dial;
			} else {
				auto *fader = new QSlider(Qt::Horizontal, this);
				fader->setObjectName("macro-parameter-slider");
				fader->setFixedWidth(100);
				slider = fader;
			}
			slider->setAccessibleName(title);
			slider->setSingleStep(1);
			layout->addWidget(slider, 0, Qt::AlignHCenter);
			connect(slider, &QAbstractSlider::valueChanged, this, [this](int index) {
				if (spec)
					queueValue(spec->valueAt(index));
			});
			connect(slider, &QAbstractSlider::sliderReleased, this, [this] {
				if (pending)
					flush();
				refresh(false);
			});
		}
		// Right-click on either the wrapper or its native control opens the
		// same strip context menu (edit/reorder/delete).
		QWidget *input = slider ? static_cast<QWidget *>(slider) : static_cast<QWidget *>(toggle);
		input->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(input, &QWidget::customContextMenuRequested, this, [this, input](const QPoint &p) {
			emit customContextMenuRequested(input->mapTo(this, p));
		});
		writeTimer.setSingleShot(true);
		writeTimer.setInterval(80);
		connect(&writeTimer, &QTimer::timeout, this, [this] { flush(); });
		readTimer.setInterval(250);
		connect(&readTimer, &QTimer::timeout, this, [this] {
			if (isVisible() && !pending && !(slider && slider->isSliderDown()))
				refresh(++pollCount % 8 == 0);
		});
		refresh(true);
		readTimer.start();
	}
private:
	void queueValue(double value)
	{
		if (!spec)
			return;
		pending = value;
		updateLabel(value);
		// Coalesce continuous adjustments; never update the source at the
		// mouse-event frequency or persist a duplicate macro value.
		if (!writeTimer.isActive())
			writeTimer.start();
	}
	void flush()
	{
		writeTimer.stop();
		const auto value = pending;
		pending.reset();
		if (value && isVisible())
			refresh(!MacroParameter::write(binding, *value));
	}
	void updateLabel(double value)
	{
		if (!spec)
			return;
		const QString display = spec->type == MacroParameterType::Boolean ?
			(value != 0 ? tr("On") : tr("Off")) : QString::number(value, 'g', 7) + spec->suffix;
		label->setText(title + " · " + display);
	}
	void refresh(bool schema)
	{
		if (schema)
			spec = MacroParameter::describe(binding);
		auto value = spec ? MacroParameter::read(binding) : std::nullopt;
		const bool valid = spec.has_value() && value.has_value();
		if (slider) {
			const QSignalBlocker block(slider);
			slider->setEnabled(valid);
			if (valid) {
				slider->setRange(0, spec->steps());
				slider->setValue(spec->indexOf(*value));
			}
		}
		if (toggle) {
			const QSignalBlocker block(toggle);
			toggle->setEnabled(valid);
			if (valid)
				toggle->setChecked(*value != 0);
		}
		if (valid) {
			updateLabel(*value);
			setToolTip(spec->label + " (" + binding.key + ")");
		} else {
			label->setText(title + " · " + tr("Unavailable"));
			setToolTip(tr("Bound source, filter or property is unavailable. Right-click to edit."));
		}
	}
	MacroParameterBinding binding;
	std::optional<MacroParameter::Spec> spec;
	std::optional<double> pending;
	QAbstractSlider *slider = nullptr;
	QCheckBox *toggle = nullptr;
	QLabel *label = nullptr;
	QString title;
	QTimer readTimer;
	QTimer writeTimer;
	int pollCount = 0;
};
}

QWidget *createMacroParameterWidget(const MacroButton &button, QWidget *parent)
{
	return new ParameterWidget(button, parent);
}

std::optional<MacroButton> editMacroParameter(QWidget *parent, const MacroButton &initial)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("Bind source or filter parameter"));
	dialog.setMinimumWidth(440);
	auto *form = new QFormLayout(&dialog);
	auto *targetCombo = new QComboBox(&dialog);
	targetCombo->setObjectName("macro-parameter-target");
	auto *propertyCombo = new QComboBox(&dialog);
	propertyCombo->setObjectName("macro-parameter-property");
	propertyCombo->setPlaceholderText(QObject::tr("Choose a parameter"));
	auto *kindCombo = new QComboBox(&dialog);
	kindCombo->setObjectName("macro-parameter-kind");
	auto *label = new QLineEdit(initial.label, &dialog);
	label->setPlaceholderText(QObject::tr("Use parameter name"));
	label->setObjectName("macro-parameter-label");
	auto *info = new QLabel(&dialog);
	info->setWordWrap(true);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	form->addRow(QObject::tr("Source / filter"), targetCombo);
	form->addRow(QObject::tr("Parameter"), propertyCombo);
	form->addRow(QObject::tr("Control"), kindCombo);
	form->addRow(QObject::tr("Label"), label);
	form->addRow(info);
	form->addRow(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	const auto targets = MacroParameter::targets();
	QList<MacroParameter::Spec> specs;
	targetCombo->addItem(QObject::tr("Choose a source or filter"), -1);
	int savedTarget = 0;
	for (int i = 0; i < targets.size(); ++i) {
		const auto &t = targets[i];
		targetCombo->addItem(t.label, i);
		if (t.sourceUuid == initial.binding.sourceUuid && t.filterUuid == initial.binding.filterUuid)
			savedTarget = i + 1;
	}
	MacroControlKind preferred = initial.kind;
	auto updateKind = [&] {
		const int index = propertyCombo->currentIndex();
		kindCombo->clear();
		const bool valid = index >= 0 && index < specs.size();
		buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
		if (!valid) {
			info->setText(QObject::tr("Choose an exposed numeric or boolean property."));
			return;
		}
		const auto &spec = specs[index];
		if (spec.type == MacroParameterType::Boolean)
			kindCombo->addItem(QObject::tr("Toggle"), static_cast<int>(MacroControlKind::Toggle));
		else {
			kindCombo->addItem(QObject::tr("Slider"), static_cast<int>(MacroControlKind::Slider));
			kindCombo->addItem(QObject::tr("Knob"), static_cast<int>(MacroControlKind::Knob));
		}
		const int kindIndex = kindCombo->findData(static_cast<int>(preferred));
		if (kindIndex >= 0)
			kindCombo->setCurrentIndex(kindIndex);
		info->setText(spec.type == MacroParameterType::Boolean ? spec.key :
			QObject::tr("%1 · %2 to %3 · step %4").arg(spec.key)
				.arg(spec.min).arg(spec.max).arg(spec.step));
	};
	auto updateProperties = [&] {
		const QSignalBlocker block(propertyCombo);
		propertyCombo->clear();
		specs.clear();
		const int targetIndex = targetCombo->currentData().toInt();
		if (targetIndex >= 0 && targetIndex < targets.size()) {
			const auto &target = targets[targetIndex];
			specs = MacroParameter::properties({target.sourceUuid, target.filterUuid, {}, MacroParameterType::Float});
			for (const auto &spec : specs)
				propertyCombo->addItem(spec.label);
			// If a saved key disappeared, never silently substitute the first
			// available parameter. Require an explicit new selection.
			propertyCombo->setCurrentIndex(-1);
			if (target.sourceUuid == initial.binding.sourceUuid && target.filterUuid == initial.binding.filterUuid) {
				for (int i = 0; i < specs.size(); ++i)
					if (specs[i].key == initial.binding.key && specs[i].type == initial.binding.type)
						propertyCombo->setCurrentIndex(i);
			}
		}
		updateKind();
	};
	QObject::connect(targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { updateProperties(); });
	QObject::connect(propertyCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { updateKind(); });
	QObject::connect(kindCombo, qOverload<int>(&QComboBox::activated), &dialog, [&](int) {
		preferred = static_cast<MacroControlKind>(kindCombo->currentData().toInt());
	});
	targetCombo->setCurrentIndex(savedTarget);
	updateProperties();
	if (dialog.exec() != QDialog::Accepted)
		return std::nullopt;
	const int targetIndex = targetCombo->currentData().toInt();
	const int propertyIndex = propertyCombo->currentIndex();
	if (targetIndex < 0 || targetIndex >= targets.size() || propertyIndex < 0 || propertyIndex >= specs.size())
		return std::nullopt;
	const auto &target = targets[targetIndex];
	const auto &spec = specs[propertyIndex];
	MacroButton result = initial;
	result.kind = static_cast<MacroControlKind>(kindCombo->currentData().toInt());
	result.binding = {target.sourceUuid, target.filterUuid, spec.key, spec.type};
	result.sourceName = target.label;
	result.label = label->text().trimmed().isEmpty() ? spec.label.section(" / ", -1) : label->text().trimmed();
	if (!MacroParameter::describe(result.binding))
		return std::nullopt;
	return result;
}
