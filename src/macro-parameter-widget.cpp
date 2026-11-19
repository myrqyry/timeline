#include "macro-parameter-widget.h"
#include "macro-button-widget.h"
#include <QDoubleSpinBox>
#include <QFontMetrics>

#include <QAbstractSlider>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QPainter>
#include <QStylePainter>
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
#include <QPlainTextEdit>
#include <QInputDialog>
#include <QPointer>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace {
// Retain QDial's value model and accessibility; paint a compact, themed
// rotary control and use relative movement instead of absolute mouse angles.
class RelativeDial final : public QDial {
public:
	explicit RelativeDial(QWidget *parent) : QDial(parent)
	{
		setFocusPolicy(Qt::StrongFocus);
		setCursor(Qt::SizeVerCursor);
		setAttribute(Qt::WA_Hover);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto pal = palette();
		const auto group = !isEnabled() ? QPalette::Disabled
						: (isActiveWindow() ? QPalette::Active : QPalette::Inactive);
		const QColor accent = pal.color(group, QPalette::Highlight);
		const QColor face = pal.color(group, QPalette::Button);
		const QColor ink = pal.color(group, QPalette::ButtonText);
		const QColor edge = pal.color(group, QPalette::Mid);
		const QPointF center(width() / 2.0, height() / 2.0);
		const double radius = std::min(width(), height()) / 2.0;
		const double trackRadius = radius - 3.0;
		const QRectF arc(center.x() - trackRadius, center.y() - trackRadius, trackRadius * 2, trackRadius * 2);
		const double fraction = maximum() > minimum()
						? std::clamp((static_cast<double>(value()) - minimum()) /
								     (static_cast<double>(maximum()) - minimum()),
							     0.0, 1.0)
						: 0.0;
		constexpr int startAngle = 225 * 16;
		constexpr int sweep = 270 * 16;
		// Thin, low-contrast track with a full-opacity active arc.
		p.setPen(QPen(edge, 2.4, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(arc, startAngle, -sweep);
		if (fraction > 0) {
			p.setPen(QPen(accent, 2.4, Qt::SolidLine, Qt::RoundCap));
			p.drawArc(arc, startAngle, -qRound(sweep * fraction));
		}
		const double faceRadius = radius - 7;
		p.setBrush(face);
		p.setPen(QPen(isEnabled() && (underMouse() || isSliderDown() || hasFocus()) ? accent : edge, 0.8));
		p.drawEllipse(center, faceRadius, faceRadius);
		const double theta = (225.0 - 270.0 * fraction) * 3.14159265358979323846 / 180.0;
		const QPointF direction(std::cos(theta), -std::sin(theta));
		p.setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(center + direction * (faceRadius * 0.43), center + direction * (faceRadius - 2.0));
		if (hasFocus() && isEnabled()) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(accent, 0.8, Qt::DotLine));
			p.drawEllipse(center, radius - 0.9, radius - 0.9);
		}
	}

	void sliderChange(SliderChange change) override
	{
		QDial::sliderChange(change);
		update();
	}

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
		const bool handled = QDial::event(event);
		switch (event->type()) {
		case QEvent::HoverEnter:
		case QEvent::HoverLeave:
		case QEvent::FocusIn:
		case QEvent::FocusOut:
		case QEvent::PaletteChange:
		case QEvent::EnabledChange:
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::UngrabMouse:
			update();
			break;
		default:
			break;
		}
		return handled;
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

// Live text must not determine layout geometry. Keep the full QLabel text
// for accessibility/tooltips and elide only its painted representation.
class StableCaption final : public QLabel {
public:
	explicit StableCaption(QWidget *parent) : QLabel(parent)
	{
		setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QStylePainter painter(this);
		painter.setFont(font());
		const auto area = contentsRect();
		const QString visible = fontMetrics().elidedText(text(), Qt::ElideMiddle, area.width());
		painter.drawItemText(area, Qt::AlignCenter | Qt::TextSingleLine, palette(), isEnabled(), visible,
				     foregroundRole());
	}
};

class XYPad final : public QWidget {
public:
	explicit XYPad(QWidget *parent) : QWidget(parent)
	{
		setObjectName("macro-parameter-xy");
		setFixedSize(64, 64);
		setFocusPolicy(Qt::StrongFocus);
		setCursor(Qt::CrossCursor);
	}
	std::function<void(QPointF)> moved;
	std::function<void()> released;
	std::function<void(int, int)> stepped;
	void setPosition(QPointF position)
	{
		point = position;
		update();
	}
	bool dragging() const { return active; }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto group = isEnabled() ? (isActiveWindow() ? QPalette::Active : QPalette::Inactive)
					       : QPalette::Disabled;
		const auto pal = palette();
		const auto accent = pal.color(group, QPalette::Highlight);
		const QRectF box(1, 1, width() - 2, height() - 2);
		p.setPen(QPen(hasFocus() && isEnabled() ? accent : pal.color(group, QPalette::Mid), 1));
		p.setBrush(pal.color(group, QPalette::Base));
		p.drawRoundedRect(box, 3, 3);
		auto grid = pal.color(group, QPalette::Mid);
		grid.setAlpha(100);
		p.setPen(QPen(grid, 1));
		p.drawLine(QPointF(width() / 2.0, 4), QPointF(width() / 2.0, height() - 4));
		p.drawLine(QPointF(4, height() / 2.0), QPointF(width() - 4, height() / 2.0));
		const QPointF marker(4 + point.x() * (width() - 9), 4 + (1 - point.y()) * (height() - 9));
		p.setPen(QPen(accent, 1, Qt::DashLine));
		p.drawLine(QPointF(marker.x(), 4), QPointF(marker.x(), height() - 4));
		p.drawLine(QPointF(4, marker.y()), QPointF(width() - 4, marker.y()));
		p.setPen(QPen(pal.color(group, QPalette::HighlightedText), 1));
		p.setBrush(accent);
		p.drawEllipse(marker, 3.5, 3.5);
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		if (!isEnabled() || e->button() != Qt::LeftButton) {
			e->ignore();
			return;
		}
		setFocus(Qt::MouseFocusReason);
		active = true;
		moveTo(e->position());
		e->accept();
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (!active || !(e->buttons() & Qt::LeftButton)) {
			e->ignore();
			return;
		}
		moveTo(e->position());
		e->accept();
	}
	void mouseReleaseEvent(QMouseEvent *e) override
	{
		if (!active || e->button() != Qt::LeftButton) {
			e->ignore();
			return;
		}
		moveTo(e->position());
		active = false;
		if (released)
			released();
		e->accept();
	}
	void keyPressEvent(QKeyEvent *e) override
	{
		if (!isEnabled() || active || !stepped) {
			e->ignore();
			return;
		}
		switch (e->key()) {
		case Qt::Key_Left:
			stepped(-1, 0);
			break;
		case Qt::Key_Right:
			stepped(1, 0);
			break;
		case Qt::Key_Down:
			stepped(0, -1);
			break;
		case Qt::Key_Up:
			stepped(0, 1);
			break;
		default:
			QWidget::keyPressEvent(e);
			return;
		}
		e->accept();
	}
	bool event(QEvent *e) override
	{
		if (e->type() == QEvent::UngrabMouse || (e->type() == QEvent::EnabledChange && !isEnabled()))
			active = false;
		const bool handled = QWidget::event(e);
		if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut ||
		    e->type() == QEvent::PaletteChange || e->type() == QEvent::EnabledChange)
			update();
		return handled;
	}

private:
	void moveTo(QPointF pos)
	{
		if (moved)
			moved({std::clamp((pos.x() - 4) / (width() - 9), 0.0, 1.0),
			       1 - std::clamp((pos.y() - 4) / (height() - 9), 0.0, 1.0)});
	}
	QPointF point{0.5, 0.5};
	bool active = false;
};

class XYParameterWidget final : public QWidget {
public:
	XYParameterWidget(const MacroButton &button, QWidget *parent)
		: QWidget(parent),
		  x(button.binding),
		  y(button.bindingY),
		  title(button.label.isEmpty() ? tr("XY") : button.label)
	{
		setObjectName("macro-parameter-control");
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(3, 0, 3, 0);
		layout->setSpacing(0);
		caption = new StableCaption(this);
		caption->setObjectName("macro-parameter-caption");
		caption->setTextFormat(Qt::PlainText);
		caption->setAttribute(Qt::WA_TransparentForMouseEvents);
		QFont f = font();
		f.setPointSize(8);
		f.setBold(true);
		caption->setFont(f);
		layout->addWidget(caption);
		row = new QWidget(this);
		auto *controls = new QVBoxLayout(row);
		controls->setContentsMargins(0, 0, 0, 0);
		pad = new XYPad(row);
		pad->setAccessibleName(title + tr(" XY pad"));
		pad->setAccessibleDescription(tr("Arrow keys adjust individual axes."));
		controls->addWidget(pad, 0, Qt::AlignCenter);
		layout->addWidget(row);
		setContextMenuPolicy(Qt::CustomContextMenu);
		pad->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(pad, &QWidget::customContextMenuRequested, this,
			[this](QPoint p) { emit customContextMenuRequested(pad->mapTo(this, p)); });
		pad->moved = [this](QPointF p) {
			if (!xs || !ys)
				return;
			queue(xs->valueAt(qRound(p.x() * xs->steps())), ys->valueAt(qRound(p.y() * ys->steps())));
		};
		pad->released = [this] {
			flush();
		};
		pad->stepped = [this](int dx, int dy) {
			if (!xs || !ys)
				return;
			const auto xv = MacroParameter::read(x);
			const auto yv = MacroParameter::read(y);
			if (!xv || !yv) {
				refresh();
				return;
			}
			queue(xs->valueAt(std::clamp(xs->indexOf(*xv) + dx, 0, xs->steps())),
			      ys->valueAt(std::clamp(ys->indexOf(*yv) + dy, 0, ys->steps())));
			flush();
		};
		writer.setSingleShot(true);
		writer.setInterval(80);
		connect(&writer, &QTimer::timeout, this, [this] { flush(); });
		reader.setInterval(250);
		connect(&reader, &QTimer::timeout, this, [this] {
			if (isVisible() && !pad->dragging() && !pending)
				refresh();
		});
		const auto size = requiredRows();
		setRowHeights(size.width(), size.height());
		refresh();
		reader.start();
	}
	QSize requiredRows() const
	{
		return {std::max(MacroButtonWidget::kLabelHeight, QFontMetrics(caption->font()).height() + 2), 64};
	}
	void setRowHeights(int captionHeight, int controlHeight)
	{
		caption->setFixedHeight(captionHeight);
		row->setFixedHeight(std::max(64, controlHeight));
		setFixedHeight(captionHeight + row->height());
		setFixedWidth(
			std::clamp(QFontMetrics(caption->font()).horizontalAdvance(title + " · -0.000 / -0.000") + 8,
				   88, 220) +
			6);
	}

private:
	void display(double xv, double yv)
	{
		if (!xs || !ys)
			return;
		pad->setPosition({double(xs->indexOf(xv)) / xs->steps(), double(ys->indexOf(yv)) / ys->steps()});
		caption->setText(title + " · " + QString::number(xv, 'g', 5) + " / " + QString::number(yv, 'g', 5));
		pad->setToolTip(title + "\nX: " + xs->label + " = " + QString::number(xv, 'g', 12) + xs->suffix +
				"\nY: " + ys->label + " = " + QString::number(yv, 'g', 12) + ys->suffix);
	}
	void queue(double xv, double yv)
	{
		pending = QPointF(xv, yv);
		display(xv, yv);
		if (!writer.isActive())
			writer.start();
	}
	void flush()
	{
		writer.stop();
		const auto value = pending;
		pending.reset();
		if (value && isVisible() && !MacroParameter::writePair(x, value->x(), y, value->y())) {
			refresh();
			return;
		}
		if (!pad->dragging())
			refresh();
	}
	void refresh()
	{
		xs = MacroParameter::describe(x);
		ys = MacroParameter::describe(y);
		const auto xv = xs ? MacroParameter::read(x) : std::nullopt;
		const auto yv = ys ? MacroParameter::read(y) : std::nullopt;
		const bool valid = MacroParameter::numericPair(x, y) && xv && yv;
		pad->setEnabled(valid);
		if (valid)
			display(*xv, *yv);
		else {
			caption->setText(title + " · " + tr("Unavailable"));
			pad->setToolTip(tr("One or both XY bindings are unavailable. Right-click to edit."));
		}
	}
	MacroParameterBinding x, y;
	QString title;
	std::optional<MacroParameter::Spec> xs, ys;
	std::optional<QPointF> pending;
	StableCaption *caption;
	QWidget *row;
	XYPad *pad;
	QTimer writer, reader;
};

class ParameterWidget final : public QWidget {
public:
	ParameterWidget(const MacroButton &button, QWidget *parent)
		: QWidget(parent),
		  binding(button.binding),
		  kind(button.kind)
	{
		setObjectName("macro-parameter-control");
		setContextMenuPolicy(Qt::CustomContextMenu);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(3, 0, 3, 0);
		layout->setSpacing(0);
		setFixedHeight(MacroButtonWidget::kWidgetHeight);
		label = new StableCaption(this);
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
		if (button.kind == MacroControlKind::Text) {
			textEntry = new QLineEdit(this);
			textEntry->setObjectName("macro-parameter-text");
			textEntry->setFixedWidth(140);
			textEntry->setMaxLength(1000000);
			textEntry->setAccessibleName(title);
			textEntry->setToolTip(
				tr("Press Enter or Apply to submit text. Unsubmitted text is not sent to OBS."));
			editText = new QPushButton(tr("Edit text…"), this);
			editText->setObjectName("macro-parameter-multiline");
			editText->setAccessibleName(title);
			editText->setFixedWidth(160);
			textRow = new QWidget(this);
			auto *textLayout = new QHBoxLayout(textRow);
			textLayout->setContentsMargins(0, 0, 0, 0);
			textLayout->setSpacing(2);
			applyText = new QPushButton(tr("Apply"), textRow);
			applyText->setObjectName("macro-parameter-text-apply");
			applyText->setAutoDefault(false);
			textLayout->addWidget(textEntry);
			textLayout->addWidget(applyText);
			controlLayout->addWidget(textRow, 0, Qt::AlignCenter);
			controlLayout->addWidget(editText, 0, Qt::AlignCenter);
			editText->hide();
			auto apply = [this] {
				if (textEntry->isModified() && isVisible() &&
				    MacroParameter::writeText(binding, textEntry->text()))
					textEntry->setModified(false);
				refresh(true);
			};
			connect(textEntry, &QLineEdit::returnPressed, this, apply);
			connect(applyText, &QPushButton::clicked, this, apply);
			connect(editText, &QPushButton::clicked, this, [this] {
				auto current = MacroParameter::readText(binding);
				if (!current)
					return;
				QPointer<ParameterWidget> guard(this);
				bool accepted = false;
				const QString next = QInputDialog::getMultiLineText(this, tr("Edit text"), tr("Value"),
										    *current, &accepted);
				if (!guard)
					return;
				if (accepted && isVisible())
					MacroParameter::writeText(binding, next);
				refresh(true);
			});
		} else if (button.kind == MacroControlKind::Toggle) {
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
				dial->setFixedSize(32, 32);
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
		for (QWidget *input : {static_cast<QWidget *>(slider), static_cast<QWidget *>(toggle),
				       static_cast<QWidget *>(editText), static_cast<QWidget *>(applyText)}) {
			if (!input)
				continue;
			input->setContextMenuPolicy(Qt::CustomContextMenu);
			connect(input, &QWidget::customContextMenuRequested, this, [this, input](const QPoint &p) {
				emit customContextMenuRequested(input->mapTo(this, p));
			});
		}
		// Keep the line edit's standard cut/copy/paste menu; the caption
		// and wrapper still provide the strip's binding context menu.
		writeTimer.setSingleShot(true);
		writeTimer.setInterval(80);
		connect(&writeTimer, &QTimer::timeout, this, [this] { flush(); });
		readTimer.setInterval(250);
		connect(&readTimer, &QTimer::timeout, this, [this] {
			if (isVisible() && !pending && !(slider && slider->isSliderDown()))
				refresh(++pollCount % 8 == 0);
		});
		refresh(true);
		reserveColumnWidth();
		readTimer.start();
	}

	QSize requiredRows() const
	{
		const int labelHeight =
			std::max(MacroButtonWidget::kLabelHeight, QFontMetrics(label->font()).height() + 2);
		if (textEntry) {
			textEntry->ensurePolished();
			editText->ensurePolished();
			applyText->ensurePolished();
			return QSize(labelHeight,
				     std::max({MacroButtonWidget::kControlHeight, textEntry->sizeHint().height(),
					       editText->sizeHint().height(), applyText->sizeHint().height()}));
		}
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
		reserveColumnWidth();
		updateGeometry();
	}

private:
	void reserveColumnWidth()
	{
		const QFontMetrics fm(label->font());
		// Width depends on the assigned caption and control kind, never
		// the live value or current availability of the target.
		QString sample = title;
		if (kind != MacroControlKind::Text) {
			const QString value = kind == MacroControlKind::Toggle
						      ? tr("Off")
						      : (binding.domain == "mixer" ? QStringLiteral("-100.0 dB")
										   : QStringLiteral("-0.000000"));
			sample += " · " + value;
		}
		int width = std::clamp(fm.horizontalAdvance(sample) + 8, 52, 220);
		if (slider)
			width = std::max(width, slider->minimumWidth());
		if (toggle) {
			toggle->ensurePolished();
			width = std::max(width, toggle->sizeHint().width());
		}
		if (textRow) {
			textRow->ensurePolished();
			editText->ensurePolished();
			width = std::max({width, textRow->sizeHint().width(), editText->sizeHint().width(),
					  textRow->minimumSizeHint().width()});
		}
		setFixedWidth(width + layout()->contentsMargins().left() + layout()->contentsMargins().right());
	}
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
		label->setText(title + " · " +
			       (binding.domain == "mixer" && binding.type == MacroParameterType::Float
					? MacroParameter::mixerVolumeLabel(value)
					: display));
		label->setToolTip(label->text());
		setToolTip(label->text() + "\n" + spec->label + " (" + binding.key + ")");
	}
	void refresh(bool schema)
	{
		if (schema)
			spec = MacroParameter::describe(binding);
		if (textEntry) {
			auto value = spec ? MacroParameter::readText(binding) : std::nullopt;
			const bool valid = value.has_value();
			const bool multiline = spec && spec->multiline;
			textRow->setVisible(!multiline);
			editText->setVisible(multiline);
			textEntry->setEnabled(valid);
			applyText->setEnabled(valid);
			editText->setEnabled(valid);
			if (!valid) {
				textEntry->clear();
				textEntry->setModified(false);
				label->setText(title + " · " + tr("Unavailable"));
			} else {
				if (!textEntry->isModified() || multiline) {
					if (textEntry->text() != *value)
						textEntry->setText(*value);
					textEntry->setModified(false);
				}
				label->setText(title);
			}
			return;
		}
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

			if (qobject_cast<QDial *>(slider))
				slider->setToolTip(
					label->text() + "\n" +
					tr("Drag up/down · Shift: fine adjustment · Focus then scroll: one step"));
		} else {
			label->setText(title + " · " + tr("Unavailable"));
			label->setToolTip(label->text());
			setToolTip(tr("Bound source, filter or property is unavailable. Right-click to edit."));
		}
	}
	MacroParameterBinding binding;
	MacroControlKind kind;
	std::optional<MacroParameter::Spec> spec;
	std::optional<double> pending;
	QAbstractSlider *slider = nullptr;
	QCheckBox *toggle = nullptr;
	QLineEdit *textEntry = nullptr;
	QPushButton *applyText = nullptr;
	QWidget *textRow = nullptr;
	QPushButton *editText = nullptr;
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
	if (button.kind == MacroControlKind::XY)
		return new XYParameterWidget(button, parent);
	return new ParameterWidget(button, parent);
}

QSize macroParameterRequiredRows(QWidget *widget)
{
	if (auto *control = dynamic_cast<ParameterWidget *>(widget))
		return control->requiredRows();
	if (auto *control = dynamic_cast<XYParameterWidget *>(widget))
		return control->requiredRows();
	return QSize(0, 0);
}

void setMacroParameterRows(QWidget *widget, int labelHeight, int controlHeight)
{
	if (auto *control = dynamic_cast<ParameterWidget *>(widget))
		control->setRowHeights(labelHeight, controlHeight);
	else if (auto *xy = dynamic_cast<XYParameterWidget *>(widget))
		xy->setRowHeights(labelHeight, controlHeight);
}

static std::optional<MacroButton> editParameter(QWidget *parent, const MacroButton &initial,
						std::optional<QVariant> *definedValue, const QString &axis = {})
{
	QDialog dialog(parent);
	dialog.setWindowTitle(definedValue ? QObject::tr("Define parameter value")
					   : QObject::tr("Bind source or filter parameter"));
	if (!axis.isEmpty())
		dialog.setWindowTitle(axis);
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
	auto *textValue = new QPlainTextEdit(&dialog);
	textValue->setObjectName("parameter-defined-text");
	textValue->setMaximumHeight(120);
	auto *valueRow = new QWidget(&dialog);
	auto *valueLayout = new QVBoxLayout(valueRow);
	valueLayout->setContentsMargins(0, 0, 0, 0);
	valueLayout->addWidget(valueSpin);
	valueLayout->addWidget(boolValue);
	valueLayout->addWidget(textValue);
	auto *info = new QLabel(&dialog);
	info->setWordWrap(true);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	const bool chooseControl = !definedValue && axis.isEmpty();
	if (chooseControl) {
		kindCombo->addItem(QObject::tr("Slider"), static_cast<int>(MacroControlKind::Slider));
		kindCombo->addItem(QObject::tr("Knob"), static_cast<int>(MacroControlKind::Knob));
		kindCombo->addItem(QObject::tr("Toggle"), static_cast<int>(MacroControlKind::Toggle));
		kindCombo->addItem(QObject::tr("Text entry"), static_cast<int>(MacroControlKind::Text));
		const int index = kindCombo->findData(static_cast<int>(initial.kind));
		kindCombo->setCurrentIndex(index >= 0 ? index : 0);
		form->addRow(QObject::tr("Control"), kindCombo);
	}
	form->addRow(QObject::tr("Source / filter"), targetCombo);
	form->addRow(QObject::tr("Parameter"), propertyCombo);
	if (definedValue) {
		form->addRow(QObject::tr("Value"), valueRow);
		kindCombo->hide();
		label->hide();
	} else {
		if (axis.isEmpty()) {
			form->addRow(QObject::tr("Label"), label);
		} else {
			kindCombo->hide();
			label->hide();
		}
		valueRow->hide();
	}
	form->addRow(info);
	form->addRow(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QList<MacroParameter::Target> targets;
	QList<MacroParameter::Spec> specs;
	targetCombo->addItem(QObject::tr("Choose a source or filter"), -1);
	MacroControlKind preferred = chooseControl ? static_cast<MacroControlKind>(kindCombo->currentData().toInt())
						   : initial.kind;
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
			result.domain = specs[propertyIndex].domain;
		}
		return result;
	};
	MacroParameterBinding valueBinding = initial.binding;
	std::optional<QVariant> previousValue = definedValue ? *definedValue : std::nullopt;
	auto sameBinding = [](const MacroParameterBinding &a, const MacroParameterBinding &b) {
		return a.sourceUuid == b.sourceUuid && a.filterUuid == b.filterUuid && a.key == b.key &&
		       a.type == b.type && a.domain == b.domain;
	};
	// Preserve a manually chosen value across catalog refreshes, including
	// changes in the selected property's declared range.
	if (definedValue) {
		QObject::connect(valueSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
				 [&](double v) { previousValue = v; });
		QObject::connect(textValue, &QPlainTextEdit::textChanged, &dialog,
				 [&] { previousValue = textValue->toPlainText(); });
		QObject::connect(boolValue, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int i) {
			if (i >= 0)
				previousValue = boolValue->currentData().toDouble();
		});
	}
	auto updateKind = [&] {
		const int index = propertyCombo->currentIndex();
		const QSignalBlocker block(kindCombo);
		if (!chooseControl)
			kindCombo->clear();
		const bool valid = index >= 0 && index < specs.size();
		buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
		if (!valid) {
			if (definedValue)
				valueRow->setEnabled(false);
			info->setText(chooseControl
					      ? QObject::tr("Choose a compatible property.")
					      : QObject::tr("Choose an exposed numeric, boolean or text property."));
			return;
		}
		const auto &spec = specs[index];
		if (definedValue) {
			const auto binding = selectedBinding();
			const bool same = sameBinding(binding, valueBinding);
			const bool text = spec.type == MacroParameterType::Text;
			const QVariant start =
				same && previousValue
					? *previousValue
					: (text ? QVariant(MacroParameter::readText(binding).value_or(QString()))
						: QVariant(MacroParameter::read(binding).value_or(spec.min)));
			valueBinding = binding;
			const QSignalBlocker spinBlock(valueSpin);
			const QSignalBlocker boolBlock(boolValue);
			const QSignalBlocker textBlock(textValue);
			boolValue->setVisible(spec.type == MacroParameterType::Boolean);
			valueSpin->setVisible(!text && spec.type != MacroParameterType::Boolean);
			textValue->setVisible(text);
			valueRow->setEnabled(true);
			if (text) {
				textValue->setPlainText(start.toString());
				previousValue = textValue->toPlainText();
			} else if (spec.type == MacroParameterType::Boolean) {
				boolValue->setCurrentIndex(start.toDouble() != 0 ? 1 : 0);
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
				valueSpin->setValue(start.toDouble());
				previousValue = valueSpin->value();
			}
		}
		if (!chooseControl) {
			if (spec.type == MacroParameterType::Text)
				kindCombo->addItem(QObject::tr("Text entry"), static_cast<int>(MacroControlKind::Text));
			else if (spec.type == MacroParameterType::Boolean)
				kindCombo->addItem(QObject::tr("Toggle"), static_cast<int>(MacroControlKind::Toggle));
			else {
				kindCombo->addItem(QObject::tr("Slider"), static_cast<int>(MacroControlKind::Slider));
				kindCombo->addItem(QObject::tr("Knob"), static_cast<int>(MacroControlKind::Knob));
			}
			const int kindIndex = kindCombo->findData(static_cast<int>(preferred));
			if (kindIndex >= 0)
				kindCombo->setCurrentIndex(kindIndex);
		}
		info->setText((spec.type == MacroParameterType::Boolean || spec.type == MacroParameterType::Text)
				      ? spec.key
				      : QObject::tr("%1 · %2 to %3 · step %4")
						.arg(spec.key)
						.arg(spec.min)
						.arg(spec.max)
						.arg(spec.step));
	};
	auto updateProperties = [&](const MacroParameterBinding &selection, bool force) {
		auto fresh = selection.sourceUuid.isEmpty() ? QList<MacroParameter::Spec>{}
							    : MacroParameter::properties(selection);
		if (!axis.isEmpty() || chooseControl) {
			fresh.erase(std::remove_if(fresh.begin(), fresh.end(),
						   [&](const auto &spec) {
							   if (chooseControl && preferred == MacroControlKind::Toggle)
								   return spec.type != MacroParameterType::Boolean;
							   if (chooseControl && preferred == MacroControlKind::Text)
								   return spec.type != MacroParameterType::Text;
							   return spec.type != MacroParameterType::Integer &&
								  spec.type != MacroParameterType::Float;
						   }),
				    fresh.end());
		}
		if (!force && fresh == specs)
			return;
		const QSignalBlocker block(propertyCombo);
		propertyCombo->clear();
		specs = fresh;
		int selected = -1;
		for (int i = 0; i < specs.size(); ++i) {
			propertyCombo->addItem(specs[i].label);
			if (specs[i].key == selection.key && specs[i].type == selection.type &&
			    specs[i].domain == selection.domain)
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
		current.domain = selection.domain;
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
		if (chooseControl && index >= 0) {
			const auto selection = selectedBinding();
			preferred = static_cast<MacroControlKind>(kindCombo->currentData().toInt());
			updateProperties(selection, true);
		}
	});
	refreshCatalog(initial.binding, true);
	QTimer catalogTimer(&dialog);
	catalogTimer.setInterval(1000);
	QObject::connect(&catalogTimer, &QTimer::timeout, &dialog, [&] {
		// Do not disrupt an open dropdown; refresh after it closes.
		if ((definedValue && (valueSpin->hasFocus() || textValue->hasFocus())) ||
		    targetCombo->view()->isVisible() || propertyCombo->view()->isVisible() ||
		    kindCombo->view()->isVisible() || (definedValue && boolValue->view()->isVisible()))
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
	result.binding = {target.sourceUuid, target.filterUuid, spec.key, spec.type, spec.domain};
	result.sourceName = target.label;
	result.label = label->text().trimmed().isEmpty() ? spec.label.section(" / ", -1) : label->text().trimmed();
	const auto current = MacroParameter::describe(result.binding);
	if (!current)
		return std::nullopt;
	if (definedValue && spec.type == MacroParameterType::Text) {
		*definedValue = textValue->toPlainText();
	} else if (definedValue) {
		const double requested = spec.type == MacroParameterType::Boolean ? boolValue->currentData().toDouble()
										  : valueSpin->value();
		*definedValue = current->valueAt(current->indexOf(requested));
	}
	return result;
}

static std::optional<MacroButton> editXYParameter(QWidget *parent, const MacroButton &initial)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("Bind XY controller"));
	dialog.setMinimumWidth(420);
	auto *form = new QFormLayout(&dialog);
	auto *xButton = new QPushButton(&dialog);
	auto *yButton = new QPushButton(&dialog);
	xButton->setObjectName("macro-xy-bind-x");
	yButton->setObjectName("macro-xy-bind-y");
	auto *label = new QLineEdit(initial.label, &dialog);
	label->setPlaceholderText(QObject::tr("XY"));
	label->setObjectName("macro-xy-label");
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	form->addRow(QObject::tr("X axis (horizontal)"), xButton);
	form->addRow(QObject::tr("Y axis (vertical)"), yButton);
	form->addRow(QObject::tr("Label"), label);
	auto *hint = new QLabel(QObject::tr("Choose two different numeric parameters. Up increases Y."), &dialog);
	hint->setWordWrap(true);
	form->addRow(hint);
	form->addRow(buttons);
	MacroButton result = initial;
	auto describe = [](const MacroParameterBinding &b) {
		for (const auto &target : MacroParameter::targets())
			if (target.sourceUuid == b.sourceUuid && target.filterUuid == b.filterUuid)
				if (auto spec = MacroParameter::describe(b))
					return target.label + " / " + spec->label;
		return b.key.isEmpty() ? QObject::tr("Choose parameter…") : b.key + QObject::tr(" (unavailable)");
	};
	auto refresh = [&] {
		xButton->setText(describe(result.binding));
		yButton->setText(describe(result.bindingY));
		buttons->button(QDialogButtonBox::Ok)
			->setEnabled(MacroParameter::numericPair(result.binding, result.bindingY) &&
				     MacroParameter::describe(result.binding) &&
				     MacroParameter::describe(result.bindingY));
	};
	auto select = [&](MacroParameterBinding &binding, const QString &title) {
		MacroButton axis;
		axis.kind = MacroControlKind::Slider;
		axis.binding = binding;
		if (auto choice = editParameter(&dialog, axis, nullptr, title))
			binding = choice->binding;
		refresh();
	};
	QObject::connect(xButton, &QPushButton::clicked, &dialog,
			 [&] { select(result.binding, QObject::tr("Choose X parameter")); });
	QObject::connect(yButton, &QPushButton::clicked, &dialog,
			 [&] { select(result.bindingY, QObject::tr("Choose Y parameter")); });
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	refresh();
	if (dialog.exec() != QDialog::Accepted || !MacroParameter::numericPair(result.binding, result.bindingY) ||
	    !MacroParameter::describe(result.binding) || !MacroParameter::describe(result.bindingY))
		return std::nullopt;
	result.label = label->text().trimmed();
	if (result.label.isEmpty())
		result.label = QObject::tr("XY");
	return result;
}

std::optional<MacroButton> editMacroParameter(QWidget *parent, const MacroButton &initial)
{
	if (initial.kind == MacroControlKind::XY)
		return editXYParameter(parent, initial);
	return editParameter(parent, initial, nullptr);
}

std::optional<MacroButton> editDefinedParameter(QWidget *parent, const MacroButton &initial,
						std::optional<QVariant> &value)
{
	return editParameter(parent, initial, &value);
}
