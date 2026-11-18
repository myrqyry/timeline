#include "macro-parameter-widget.h"
#include "macro-button-widget.h"
#include <QDoubleSpinBox>
#include <QFontMetrics>

#include <QAbstractSlider>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <algorithm>
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
// Keep the native Qt appearance and keyboard/accessibility behavior, but
// use relative movement rather than QDial's absolute mouse-angle mapping.
class RelativeDial final : public QDial {
public:
	explicit RelativeDial(QWidget *parent) : QDial(parent)
	{
		setFocusPolicy(Qt::StrongFocus);
		setCursor(Qt::SizeVerCursor);
	}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton || !isEnabled()) {
			event->ignore();
			return;
		}
		setFocus(Qt::MouseFocusReason);
		lastPosition = event->globalPosition();
		dragValue = value();
		setSliderDown(true);
		event->accept();
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (!isSliderDown() || !(event->buttons() & Qt::LeftButton)) {
			event->ignore();
			return;
		}
		const auto position = event->globalPosition();
		const double delta = lastPosition.y() - position.y();
		lastPosition = position;
		const double span = static_cast<double>(maximum()) - minimum();
		// Wide ranges cover their full travel in 160 logical pixels.
		// Small discrete ranges need only eight pixels per step.
		const double pixels = std::max(1.0, std::min(160.0, span * 8.0));
		const double fine = event->modifiers().testFlag(Qt::ShiftModifier) ? 0.1 : 1.0;
		dragValue = std::clamp(dragValue + delta * span / pixels * fine, static_cast<double>(minimum()),
				       static_cast<double>(maximum()));
		setValue(static_cast<int>(std::round(dragValue)));
		event->accept();
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton && isSliderDown()) {
			setSliderDown(false);
			event->accept();
		} else {
			event->ignore();
		}
	}

	bool event(QEvent *event) override
	{
		if ((event->type() == QEvent::UngrabMouse ||
		     (event->type() == QEvent::EnabledChange && !isEnabled())) &&
		    isSliderDown())
			setSliderDown(false);
		return QDial::event(event);
	}

	void keyPressEvent(QKeyEvent *event) override
	{
		if (isSliderDown()) {
			event->ignore();
			return;
		}
		switch (event->key()) {
		case Qt::Key_Left:
		case Qt::Key_Down:
			setValue(std::max(minimum(), value() - singleStep()));
			break;
		case Qt::Key_Right:
		case Qt::Key_Up:
			setValue(std::min(maximum(), value() + singleStep()));
			break;
		default:
			QDial::keyPressEvent(event);
			return;
		}
		event->accept();
	}

	void wheelEvent(QWheelEvent *event) override
	{
		// Scrolling the strip must not accidentally adjust an unfocused knob.
		if (!hasFocus() || isSliderDown() || !isEnabled() || event->angleDelta().y() == 0) {
			event->ignore();
			return;
		}
		wheelRemainder += event->angleDelta().y();
		const int steps = wheelRemainder / 120;
		wheelRemainder %= 120;
		const auto next = static_cast<qint64>(value()) + static_cast<qint64>(steps) * singleStep();
		setValue(static_cast<int>(
			std::clamp(next, static_cast<qint64>(minimum()), static_cast<qint64>(maximum()))));
		event->accept();
	}

private:
	QPointF lastPosition;
	double dragValue = 0;
	int wheelRemainder = 0;
};

class ParameterWidget final : public QWidget {
public:
	ParameterWidget(const MacroButton &button, QWidget *parent) : QWidget(parent), binding(button.binding)
	{
		setObjectName("macro-parameter-control");
		setContextMenuPolicy(Qt::CustomContextMenu);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(3, 0, 3, 0);
		layout->setSpacing(0);
		setFixedHeight(MacroButtonWidget::kWidgetHeight);
		label = new QLabel(this);
		label->setAlignment(Qt::AlignCenter);
		label->setTextFormat(Qt::PlainText);
		label->setObjectName("macro-parameter-caption");
		QFont captionFont = font();
		captionFont.setPointSize(8);
		captionFont.setBold(true);
		label->setFont(captionFont);
		label->setFixedHeight(MacroButtonWidget::kLabelHeight);
		title = button.label.isEmpty() ? binding.key : button.label;
		label->setText(title);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		layout->addWidget(label);
		controlRow = new QWidget(this);
		controlRow->setFixedHeight(MacroButtonWidget::kControlHeight);
		auto *controlLayout = new QVBoxLayout(controlRow);
		controlLayout->setContentsMargins(0, 0, 0, 0);
		controlLayout->setSpacing(0);
		layout->addWidget(controlRow);
		if (button.kind == MacroControlKind::Toggle) {
			toggle = new QCheckBox(this);
			toggle->setObjectName("macro-parameter-toggle");
			toggle->setAccessibleName(title);
			controlLayout->addWidget(toggle, 0, Qt::AlignCenter);
			connect(toggle, &QCheckBox::toggled, this, [this](bool value) {
				queueValue(value ? 1 : 0);
				flush();
			});
		} else {
			if (button.kind == MacroControlKind::Knob) {
				auto *dial = new RelativeDial(this);
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
			controlLayout->addWidget(slider, 0, Qt::AlignCenter);
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
		connect(input, &QWidget::customContextMenuRequested, this,
			[this, input](const QPoint &p) { emit customContextMenuRequested(input->mapTo(this, p)); });
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

	QSize requiredRows() const
	{
		const int labelHeight =
			std::max(MacroButtonWidget::kLabelHeight, QFontMetrics(label->font()).height() + 2);
		auto *input = slider ? static_cast<QWidget *>(slider) : static_cast<QWidget *>(toggle);
		input->ensurePolished();
		// A dial has an intentional fixed size; native sliders and toggles
		// need their theme's actual size hint, not a guessed pixel height.
		const int height = qobject_cast<QDial *>(input)
					   ? input->height()
					   : std::max(input->sizeHint().height(), input->minimumSizeHint().height());
		return QSize(labelHeight, std::max(MacroButtonWidget::kControlHeight, height));
	}
	void setRowHeights(int labelHeight, int controlHeight)
	{
		label->setFixedHeight(labelHeight);
		controlRow->setFixedHeight(controlHeight);
		setFixedHeight(labelHeight + controlHeight);
		updateGeometry();
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
		if (value && isVisible()) {
			const bool written = MacroParameter::write(binding, *value);
			// Readback during a drag must not move the knob under the cursor.
			// A failed write still invalidates an unavailable binding immediately.
			if (!written || !(slider && slider->isSliderDown()))
				refresh(!written);
		}
	}
	void updateLabel(double value)
	{
		if (!spec)
			return;
		const QString display = spec->type == MacroParameterType::Boolean
						? (value != 0 ? tr("On") : tr("Off"))
						: QString::number(value, 'g', 7) + spec->suffix;
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
			if (qobject_cast<QDial *>(slider))
				slider->setToolTip(
					tr("Drag up/down · Shift: fine adjustment · Focus then scroll: one step"));
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
	QWidget *controlRow = nullptr;
	QString title;
	QTimer readTimer;
	QTimer writeTimer;
	int pollCount = 0;
};
} // namespace

QWidget *createMacroParameterWidget(const MacroButton &button, QWidget *parent)
{
	return new ParameterWidget(button, parent);
}

QSize macroParameterRequiredRows(QWidget *widget)
{
	if (auto *control = dynamic_cast<ParameterWidget *>(widget))
		return control->requiredRows();
	return QSize(0, 0);
}

void setMacroParameterRows(QWidget *widget, int labelHeight, int controlHeight)
{
	if (auto *control = dynamic_cast<ParameterWidget *>(widget))
		control->setRowHeights(labelHeight, controlHeight);
}

static std::optional<MacroButton> editParameter(QWidget *parent, const MacroButton &initial,
						std::optional<double> *definedValue)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(definedValue ? QObject::tr("Define parameter value")
					   : QObject::tr("Bind source or filter parameter"));
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
	auto *valueSpin = new QDoubleSpinBox(&dialog);
	valueSpin->setObjectName("parameter-defined-value");
	valueSpin->setKeyboardTracking(false);
	auto *boolValue = new QComboBox(&dialog);
	boolValue->setObjectName("parameter-defined-bool");
	boolValue->addItem(QObject::tr("Off"), 0);
	boolValue->addItem(QObject::tr("On"), 1);
	auto *valueRow = new QWidget(&dialog);
	auto *valueLayout = new QVBoxLayout(valueRow);
	valueLayout->setContentsMargins(0, 0, 0, 0);
	valueLayout->addWidget(valueSpin);
	valueLayout->addWidget(boolValue);
	auto *info = new QLabel(&dialog);
	info->setWordWrap(true);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	form->addRow(QObject::tr("Source / filter"), targetCombo);
	form->addRow(QObject::tr("Parameter"), propertyCombo);
	if (definedValue) {
		form->addRow(QObject::tr("Value"), valueRow);
		kindCombo->hide();
		label->hide();
	} else {
		form->addRow(QObject::tr("Control"), kindCombo);
		form->addRow(QObject::tr("Label"), label);
		valueRow->hide();
	}
	form->addRow(info);
	form->addRow(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QList<MacroParameter::Target> targets;
	QList<MacroParameter::Spec> specs;
	targetCombo->addItem(QObject::tr("Choose a source or filter"), -1);
	MacroControlKind preferred = initial.kind;
	auto selectedBinding = [&] {
		MacroParameterBinding result;
		const int targetIndex = targetCombo->currentData().toInt();
		if (targetIndex < 0 || targetIndex >= targets.size())
			return result;
		result.sourceUuid = targets[targetIndex].sourceUuid;
		result.filterUuid = targets[targetIndex].filterUuid;
		const int propertyIndex = propertyCombo->currentIndex();
		if (propertyIndex >= 0 && propertyIndex < specs.size()) {
			result.key = specs[propertyIndex].key;
			result.type = specs[propertyIndex].type;
		}
		return result;
	};
	MacroParameterBinding valueBinding = initial.binding;
	std::optional<double> previousValue = definedValue ? *definedValue : std::nullopt;
	auto sameBinding = [](const MacroParameterBinding &a, const MacroParameterBinding &b) {
		return a.sourceUuid == b.sourceUuid && a.filterUuid == b.filterUuid && a.key == b.key &&
		       a.type == b.type;
	};
	// Preserve a manually chosen value across catalog refreshes, including
	// changes in the selected property's declared range.
	if (definedValue) {
		QObject::connect(valueSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
				 [&](double v) { previousValue = v; });
		QObject::connect(boolValue, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int i) {
			if (i >= 0)
				previousValue = boolValue->currentData().toDouble();
		});
	}
	auto updateKind = [&] {
		const int index = propertyCombo->currentIndex();
		const QSignalBlocker block(kindCombo);
		kindCombo->clear();
		const bool valid = index >= 0 && index < specs.size();
		buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
		if (!valid) {
			if (definedValue)
				valueRow->setEnabled(false);
			info->setText(QObject::tr("Choose an exposed numeric or boolean property."));
			return;
		}
		const auto &spec = specs[index];
		if (definedValue) {
			const auto binding = selectedBinding();
			const bool same = sameBinding(binding, valueBinding);
			const double start = same && previousValue ? *previousValue
								   : MacroParameter::read(binding).value_or(spec.min);
			valueBinding = binding;
			const QSignalBlocker spinBlock(valueSpin);
			const QSignalBlocker boolBlock(boolValue);
			boolValue->setVisible(spec.type == MacroParameterType::Boolean);
			valueSpin->setVisible(spec.type != MacroParameterType::Boolean);
			valueRow->setEnabled(true);
			if (spec.type == MacroParameterType::Boolean) {
				boolValue->setCurrentIndex(start != 0 ? 1 : 0);
				previousValue = boolValue->currentData().toDouble();
			} else {
				// Preserve the precision of the declared range and increments.
				int decimals = 0;
				for (; decimals < 12; ++decimals) {
					const double scale = std::pow(10.0, decimals);
					auto integral = [scale](double v) {
						return std::abs(v * scale - std::round(v * scale)) < 1e-7;
					};
					if (integral(spec.step) && integral(spec.min) && integral(spec.max))
						break;
				}
				valueSpin->setDecimals(decimals);
				valueSpin->setRange(spec.min, spec.max);
				valueSpin->setSingleStep(spec.step);
				valueSpin->setSuffix(spec.suffix);
				valueSpin->setValue(start);
				previousValue = valueSpin->value();
			}
		}
		if (spec.type == MacroParameterType::Boolean)
			kindCombo->addItem(QObject::tr("Toggle"), static_cast<int>(MacroControlKind::Toggle));
		else {
			kindCombo->addItem(QObject::tr("Slider"), static_cast<int>(MacroControlKind::Slider));
			kindCombo->addItem(QObject::tr("Knob"), static_cast<int>(MacroControlKind::Knob));
		}
		const int kindIndex = kindCombo->findData(static_cast<int>(preferred));
		if (kindIndex >= 0)
			kindCombo->setCurrentIndex(kindIndex);
		info->setText(spec.type == MacroParameterType::Boolean ? spec.key
								       : QObject::tr("%1 · %2 to %3 · step %4")
										 .arg(spec.key)
										 .arg(spec.min)
										 .arg(spec.max)
										 .arg(spec.step));
	};
	auto updateProperties = [&](const MacroParameterBinding &selection, bool force) {
		const auto fresh = selection.sourceUuid.isEmpty() ? QList<MacroParameter::Spec>{}
								  : MacroParameter::properties(selection);
		if (!force && fresh == specs)
			return;
		const QSignalBlocker block(propertyCombo);
		propertyCombo->clear();
		specs = fresh;
		int selected = -1;
		for (int i = 0; i < specs.size(); ++i) {
			propertyCombo->addItem(specs[i].label);
			if (specs[i].key == selection.key && specs[i].type == selection.type)
				selected = i;
		}
		// Missing properties never silently bind to an adjacent list entry.
		propertyCombo->setCurrentIndex(selected);
		updateKind();
	};
	auto refreshCatalog = [&](const MacroParameterBinding &selection, bool force) {
		const auto fresh = MacroParameter::targets();
		if (force || !(targets == fresh)) {
			const QSignalBlocker block(targetCombo);
			targetCombo->clear();
			targetCombo->addItem(QObject::tr("Choose a source or filter"), -1);
			targets = fresh;
			int selected = 0;
			for (int i = 0; i < targets.size(); ++i) {
				const auto &t = targets[i];
				targetCombo->addItem(t.label, i);
				if (t.sourceUuid == selection.sourceUuid && t.filterUuid == selection.filterUuid)
					selected = i + 1;
			}
			targetCombo->setCurrentIndex(selected);
		}
		// Do not use a removed target's identity to enumerate properties.
		auto current = selectedBinding();
		current.key = selection.key;
		current.type = selection.type;
		updateProperties(current, force);
	};
	QObject::connect(targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) {
		auto selection = selectedBinding();
		selection.key.clear();
		updateProperties(selection, true);
	});
	QObject::connect(propertyCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
			 [&](int) { updateKind(); });
	QObject::connect(kindCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
		if (index >= 0)
			preferred = static_cast<MacroControlKind>(kindCombo->currentData().toInt());
	});
	refreshCatalog(initial.binding, true);
	QTimer catalogTimer(&dialog);
	catalogTimer.setInterval(1000);
	QObject::connect(&catalogTimer, &QTimer::timeout, &dialog, [&] {
		// Do not disrupt an open dropdown; refresh after it closes.
		if ((definedValue && valueSpin->hasFocus()) || targetCombo->view()->isVisible() ||
		    propertyCombo->view()->isVisible() || kindCombo->view()->isVisible() ||
		    (definedValue && boolValue->view()->isVisible()))
			return;
		refreshCatalog(selectedBinding(), false);
	});
	catalogTimer.start();
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
	const auto current = MacroParameter::describe(result.binding);
	if (!current)
		return std::nullopt;
	if (definedValue) {
		const double requested = spec.type == MacroParameterType::Boolean ? boolValue->currentData().toDouble()
										  : valueSpin->value();
		*definedValue = current->valueAt(current->indexOf(requested));
	}
	return result;
}

std::optional<MacroButton> editMacroParameter(QWidget *parent, const MacroButton &initial)
{
	return editParameter(parent, initial, nullptr);
}

std::optional<MacroButton> editDefinedParameter(QWidget *parent, const MacroButton &initial,
						std::optional<double> &value)
{
	return editParameter(parent, initial, &value);
}
